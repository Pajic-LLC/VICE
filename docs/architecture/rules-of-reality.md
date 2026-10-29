# Rules of Reality: World-owned game logic

VICE defines generic laws and persistent state. Games supply names, meaning, content and policy. The [Constitution](../constitution.md) and [ADR 0025](../adr/0025-persistent-component-payloads.md) govern the first foundation.

## Slice A: available contracts

Register a `ComponentTypeDesc` with stable type/field IDs, a nonzero version and typed fields. Use `World::set_component`, `component`, `components` and `remove_component`. Setting validates the entire payload before publishing; schema changes are not inferred. Payloads live on ordinary World entities, spatial or nonspatial, through the common snapshot/restore boundary.

Source example (these names are game content):

```
component b7000000-0000-4000-8000-000000000100 1 "VehicleCondition" fields 2 1 real "condition" 2 integer "service_count"
entity b7000000-0000-4000-8000-000000000001 none state b7000000-0000-4000-8000-000000000100 1 2 1 real 0.75 2 integer 0
```

Numeric field IDs, not names/order, identify values. Wire kinds are `bool`, `integer`, `real`, `text`, `id`. Integer range is signed 64-bit; reals must be finite; text is printable UTF-8 up to 128 bytes. Identifier values are opaque, non-nil UUIDs rather than implicitly enforced entity pointers. Empty schemas support marker components. Every declared field must be present exactly once.

Native modules register/verify schemas and query/mutate through the append-only Host API. Source authoring supports revision-checked `register_component`, `set_component` and `remove_component`. See [game module API](gameplay-controls.md) and the presentation-playground example. Generic payloads have no automatic per-frame simulation: reduced/inactive state remains stored and explicit game writes are still allowed. Physical, rendered, animated and audible entities use the same payload ownership as any other entity.

Inspect with `vice world inspect` for authored state or `vice runtime debug` for running state. `component_payloads` contains typed field values; `component_schemas` in Runtime explains their field names/kinds. Integer debug values are strings to preserve exact int64 values. Editor Inspector displays payloads, and Play mutations affect the clone only. Checkpoints preserve module-created schemas and values; restart verifies schema compatibility without replacing saved values.

Validation commands:

```
ctest --test-dir build -R vice_component_ --output-on-failure
python3 scripts/validate_component_workflow.py --vice build/tools/vice_cli/vice --run-runtime
```

The CTest stress fixture stores 10000 owners and emits measured sizes/timings when run directly. See ADR 0025 for bounds, complexity, persistence and alternatives.

## Slice B: stateful relationships

[Stateful relationships](stateful-relationships.md) and [ADR 0026](../adr/0026-stateful-world-relationships.md) extend the same schema/value contracts to persistent directed edges, with indexed endpoint queries, bounded SDK cursor pages, persistence and tooling. Slice C is next.

## Subsequent independently verified slices

A is the payload foundation; it is not the whole Phase 7 logic framework. Continue in this order:

| Slice | Next contract | Boundary to preserve |
|---|---|---|
| B | Stable stateful relation instances | Reuse schema/value contracts and persistent endpoints; atomic relation validation |
| C | Typed World facts and short-lived signals | Extend existing causal history with explicit retention/provenance; no competing event database |
| D | Persistent authoritative simulation clock | Integer/deterministic time units, explicit pause/advance/scale, no calendar assumptions |
| E | Persistent scheduled data | Due-time index and stable tie-breaking; identifiers/data instead of executable pointers |
| F | Quantities, holders, atomic transfers | Precise arithmetic, capacities, explicit failures, causal event integration |
| G | Small declarative conditions/effects | Indexed relevant changes, bounded work and inspectable decisions; no per-rule full-frame scans |
| H | Complete game-facing contracts | Expose each finished primitive incrementally through the same size-checked ABI |

Physics results remain physical facts; games can translate them into component changes and, once C/G exist, generic causal events/effects. Names such as trust, ammunition, a purchase or an objective remain outside core APIs. Future perception will consume deliberately exposed observations; component storage is not an agent belief database and does not yet implement access grants or cognition.

Animation, vehicle quality, navigation, spatial audio, small-district streaming and time/weather should be advanced against concrete game scenes once these first substrates are stable. Do not expand into full dialogue, economy, police, military command, LLM cognition or visual scripting here. The transition target is building CRASHOUT on VICE and extending only reusable missing capabilities—not declaring the entire engine finished.
