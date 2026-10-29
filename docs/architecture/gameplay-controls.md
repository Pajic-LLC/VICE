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
