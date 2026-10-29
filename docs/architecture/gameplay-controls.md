# Gameplay controls

A project builds `game/game.cpp` as a shared module using only
`vice/runtime/module_api.hpp`. Use `Context::read_entity` with persistent UUID
bytes to obtain copied World state, then `Context::set_body_velocity` to control
a dynamic or kinematic authored box. Velocities use meters/second and
radians/second. Global query positions use double precision.

See `examples/rigid-box-playground` for a complete runnable project. Its SDK-owned
`Key` values interpret input without including platform or solver headers.

`read_entity` returns spatial and rigid-box presence flags, region/fidelity,
position, quaternion, scale, box half extents, motion and velocities. A nonspatial
entity remains queryable with flags unset. Half extents precede authored scale.
Results distinguish unavailable capabilities, missing identity/body, invalid
arguments, static motion, inactive regions, required reset and wrong phase.

Controls execute synchronously during start, event and update. Queries also work
in frame and stop. Calls from background threads or retained contexts outside
callbacks fail. Frame callbacks cannot mutate physics. Successful writes publish
actual solver velocities immediately into World, preserving the next step's
ownership checks. Source files remain unchanged.

Existing ABI 1 modules retain their original host prefix. New SDK methods check
host size and report unavailable when an older host lacks a callback. The frozen
legacy-module fixture guards binary compatibility. See ADR 0014 for the record
and threading contract.

```sh
python3 scripts/validate_gameplay_workflow.py --vice build/tools/vice_cli/vice
python3 scripts/validate_gameplay_workflow.py --vice build/tools/vice_cli/vice --run-runtime
```

The first command compiles the real project module and stages its authored scene.
The second launches a native game and observes module-driven platform motion and
dynamic floor contact. Automated loaded-module tests additionally deliver input
and verify control, callback restrictions and saved-state resume. Editor Play/Reset compiles the same project module and runs its callbacks against
an isolated clone of the current World, including unsaved changes. Focus the
Scene viewport to send game input. Pause freezes callbacks and physics; Step runs
one fixed gameplay/physics tick. Reset rebuilds the module with fresh globals; a
failed build or start leaves the current preview intact. The scene renderers show
live preview poses. Stop restores authoring interaction. See ADR 0015.

`Context::read_body_support` queries solid support using the body's current
geometry, a caller-selected up vector and minimum normal alignment. It returns
support identity, global contact point, normal and linear-plus-angular surface
velocity. The query includes sleeping bodies, excludes triggers and uses a 2 cm
near-contact margin. No support state is serialized or inferred from an entity's
absolute height. The playground uses this query to gate jumping and inherit
platform velocity. See ADR 0016 for semantics and limits.

## Persistent game-defined components (Phase 7, Slice A)

`Context::register_component_schema` registers or verifies an immutable `ComponentSchema` by UUID/version. Field IDs are stable numeric IDs; names are descriptive game content. Supported `ComponentKind` values are boolean, integer, real, text and opaque identifier. `read_component_schema` queries the definition. `ComponentData` has the schema type/version and up to 16 fixed-layout `ComponentValue` fields. Use `read_component`, `set_component` and `remove_component` with an owner's persistent ID.

Each schema field must appear once with the correct kind. Integers are signed 64-bit, doubles finite, text printable UTF-8 up to 128 bytes with a NUL terminator in the 129-byte SDK buffer, and identifiers non-nil UUIDs. Every DTO's `size` must describe its complete known prefix. Registration is idempotent for an identical definition after reload; conflicting definitions/versions return `schema_conflict`. Reads distinguish missing owners (`not_found`) from missing instances (`no_component`). Malformed writes leave existing payloads unchanged.

Writes are allowed in start/event/update callbacks under the existing owner-thread guard; frame and stop are read-only. Generic component writes do not implicitly wake a region or override a physics solver. Values are World truth, stored even for inactive or nonspatial owners. They survive checkpoints/restart; Play writes affect the clone and reset with Play. Query outputs are copied DTOs. The SDK never exports `std::variant`, strings, vectors or World internals.

See [Rules of Reality](rules-of-reality.md), [ADR 0025](../adr/0025-persistent-component-payloads.md), and the game-defined SessionState in `examples/presentation-playground/game/game.cpp`. The example preserves launch/interaction counters on a physical vehicle while normal character animation/audio continue. No SessionState interpretation exists in engine core.

## Stateful relationship API (Phase 7, Slice B)

`RelationKey` contains persistent subject/kind/object UUIDs. Use `Context::read_relation`, `add_relation` and `remove_relation` for the unique directed edge. Adding an existing edge is idempotent and keeps its state. `read_relation_component`, `set_relation_component` and `remove_relation_component` reuse `ComponentData` and registered schemas. Missing edges return `no_relation`; missing payloads return `no_component`. No game meaning is assigned to kinds or fields.

`read_relations` returns a bounded `RelationPage` for an endpoint, direction and optional kind (nil means any). Retain the returned cursor and continue while `has_more`; a filtered page may be empty. Reset the page when changing filters. At most 256 indexed edges are scanned and 64 matches returned per call. Edits or same-process restore invalidate retained revisions with `reset_required`. Page metadata is process-local, not save data. Size/thread/phase guards and old-host `unavailable` behavior match the existing SDK. See [stateful relationships](stateful-relationships.md).

## World time

`read_world_clock` returns integer microsecond ticks, fixed-point `rate_milli` (1000 = 1x), pause/automatic flags and saved remainders. `configure_world_clock` changes policy without resetting time. `step_world_clock` adds an exact tick duration even while paused; overflow rejects the entire step. Automatic Runtime/Editor updates advance this World clock before game update. Clock pause controls time only, not every engine subsystem. Queries work in read-only callbacks; writes retain normal owner-thread/phase guards. Tick values are strings in development JSON. See ADR 0027; no calendar is imposed.

## Typed World events (Phase 7, Slice C)

`Context::emit_world_event(WorldEventRequest, EventRef&)` publishes validated structured facts. Set the request's payload UUID/version to a registered `ComponentSchema`, subject to a live entity, optional target/object/region, `retained` to 1 (history) or 0 (transient), and up to four retained parent references. All references use UUIDs, never process handles. `EventRef` contains lineage/stream UUID, event UUID and sequence. World sets time and gameplay origin. Failed publication leaves the output reference unchanged.

`read_world_event(history, sequence, WorldEventSnapshot&)` reads the rich envelope of either typed facts or existing physical evidence. `has_payload` distinguishes them; `parents` and `parent_retained` explain all causes. `read_signal_window` returns stream/range/capacity/lifetime; `read_world_signal` reads that stream. Reads distinguish `history_mismatch`, `event_pruned` and `not_found`; writes additionally report `invalid_parent`, `schema_conflict` and `sequence_exhausted`. A missing schema or live entity returns `not_found`. No signal is restored or accepted as a causal parent. Read a snapshot's exact event UUID before reusing a sequence across a fork.

These fixed-size value DTOs/functions append to HostApi under size checks; old CausalEventSnapshot layout is frozen. Writes remain start/event/update only on the lifecycle thread, reads work in frame/stop, and old hosts return `unavailable`. No callbacks, STL containers or backend types cross the ABI. See [World events](world-events.md) for retention and persistence semantics.

## Persistent scheduled events (Phase 7, Slice E)

`Context::schedule_event(ScheduleRequest, EntityId&)` creates one-shot or recurring typed events. Request contains an optional stable schedule ID, absolute World due ticks, interval (0 means one-shot), replay/coalesce catch-up policy and the existing WorldEventRequest. No work fires inside creation. `cancel_schedule` removes pending or failed records; missing records return `no_schedule`, duplicate explicit IDs return `duplicate_id`, and the 16384-record limit returns `capacity_exceeded`. Nested event/schema/parent validation uses existing Result values and leaves output IDs unchanged on failure.

`read_schedule` returns a copied summary/template. `read_schedules` returns up to 64 summaries after an exclusive UUID cursor, updating `after` and `has_more`; this is a range over current state rather than a frozen snapshot. `read_scheduler` reports counts, next deadline, budget and overdue backlog. `dispatch_schedules` accepts a budget of 1–256 and requires a write callback. Ordinary World-clock advancement already dispatches one batch. SDK summary `event_result` uses the SDK Result enum; the persistence wire uses the World event-result enum.

`read_event_schedule(EventRef, retained, EventScheduleSnapshot&)` reads firing provenance from a retained fact or transient signal using its exact lineage/stream, sequence and event UUID. Unscheduled events return `has_schedule=0`. Existing WorldEventSnapshot layout remains unchanged. All operations follow existing size/thread/phase guards, with reads allowed in frame/stop and writes restricted to start/event/update. Old hosts return `unavailable`. See [World scheduler](world-scheduler.md) for persistence, reduced fidelity and catch-up semantics.

## Generic resource quantities (Phase 7, Slice F)

Quantity APIs append to HostApi without changing asset-resource functions. `register_quantity_type` takes QuantityTypeSnapshot with UUID/name/version/positive quantum metadata. Identical registration is idempotent; conflict returns `schema_conflict`. Query individual definitions or up to 64 types per UUID-cursor page. A QuantityKey holds persistent holder/type IDs. Create an empty account, adjust capacity, read one/all holder accounts, or remove an empty account. Holder lists contain at most 64 entries.

`transact_quantity` accepts produce/consume/transfer, source/target, type, positive int64 amount and up to four retained causal parents. It returns a QuantityReceiptSnapshot with Result, underlying event Result, event reference and exact before/after balances. Business failures write that result record; malformed DTO/phase/thread failures leave output untouched. Failures distinguish `invalid_quantity_type`, `invalid_quantity_owner`, `no_quantity_account`, `insufficient_quantity`, `quantity_capacity_exceeded`, `quantity_not_empty`, `quantity_account_exists`, `quantity_type_in_use`, `capacity_exceeded` (storage limit) and `quantity_event_rejected`. The last case exposes the causal publication reason in `event_result`.

Receipt events are readable through the existing event API and structured schema; engine receipt type `c7000000-0000-4000-8000-000000000001` is reserved. No World internals, STL containers, solver types or process handles cross this ABI. Existing size/thread/write-phase checks apply; reads work in frame/stop and writes in start/event/update. Old hosts return `unavailable`. See [Gameplay quantities](world-quantities.md) for persistence and authoring distinctions.


## Declarative World rules (Phase 7, first Slice G increment)

`Context::add_rule(RuleDefinition)` accepts a stable UUID/version, enabled flag, typed trigger/optional subject, up to eight fixed-layout conditions and one effect. Condition/effect operation enums are append-only persisted tags; compare scalars have explicit kinds. DTO fields irrelevant to the selected operation are ignored. Definition creation cannot inject pending work or decisions. Duplicate IDs return `rule_exists`; invalid data/schema/version returns `invalid_argument`; the 512-rule bound returns `capacity_exceeded`.

`read_rule` copies the full typed definition plus latest RuleSummary. `read_rules` returns up to 64 summaries after an exclusive UUID cursor, updating `after`/`has_more`. Summaries expose first pending trigger, latest trigger/output event references, optional resulting schedule ID, evaluation/coalesced counters, first failed condition index, outcome and operation-specific detail. `detail` uses the corresponding **World** QuantityResult/WorldEventResult/ScheduleResult wire values (zero for simple component/relation effects), not the SDK Result enum. Reading the latest decision does not evaluate conditions or mutate World.

`read_rule_info` reports count/enabled/pending/capacity/budget. `remove_rule` cancels pending work and releases definition dependencies. `set_rule_enabled` takes 0/1, with disable cancelling pending wakeups. `dispatch_rules` accepts 1–64 and returns evaluated/applied/rejected/backlog; it never recurses into newly produced events in the same pump. Ordinary World clock advancement pumps schedules then rules. Write/thread/read-only-frame/stop guards and old-host `unavailable` behavior follow the other append-only operations. Malformed calls leave outputs untouched.

The first retained event remains pending while repeated matching events increment a coalesced counter. This is explicit state-reconciliation behavior, not guaranteed per-event effect delivery. Each rule has one effect through an atomic World operation; there is no multi-effect transaction promise. See [World rules](world-rules.md) and [ADR 0031](../adr/0031-declarative-world-rules.md).

## Weighted animation assets

The existing animation read/control callbacks also operate on weighted rig v2 assets. DTO sizes and ABI 1 remain unchanged: games retain stable rig/root/part identities and control the clip clock; they do not access renderer palettes or generated mesh handles. [Weighted skeletal presentation](weighted-skeletal-presentation.md) documents import, diagnostics, fidelity and persistence. The skinning playground exercises these same callbacks through physical walk/drive and saved continuation.

## Powered joint drives

`read_powered_hinge` copies endpoint identities, limits, drive intent, enable/active flags and available solver angle/error/impulses. `control_powered_hinge` accepts a size-checked JointDrive and enabled flag. New callbacks append to HostApi ABI 1; existing DTOs and callbacks retain their layouts. Mutation is limited to writable owner-thread lifecycle phases and an active simulation region. Structural frames/endpoints are authored through World source/edit contracts, then reconstructed by reset. See [powered articulation](powered-articulation.md) and ADR 0033.
