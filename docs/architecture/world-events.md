# World events and signals

See [ADR 0028](../adr/0028-typed-world-events.md) for invariants, persistence, retention, alternatives and scaling limits.

Register an immutable component schema by UUID/version, then publish `WorldEventRequest` with a structured payload of that type. Subject is required; target/object and region are optional. World assigns event UUID, sequence and simulation timestamp. A retained request appends to the existing causal journal, starting capture if necessary. A transient request enters the bounded signal window and never appears in a save. Core APIs interpret no gameplay meanings.

C++ `World::emit_world_event` returns `WorldEventEmission` with a result and `{history,id,sequence}` reference. Failures distinguish invalid input, missing entity/schema, version conflict, history mismatch, pruned/mismatched parents and sequence exhaustion. Facts may cite up to four retained parents, including physics evidence and earlier capture segments. Signals may cite facts, but are not parentable evidence themselves. Record the fact as retained when a later consequence must cite it.

`causal_event(sequence)` reads retained evidence. `causal_context(subject, latest, budget)` includes target matches and traverses all parents. `signal_info`, `world_signal(sequence)` and `signal_context(subject,budget)` inspect ephemeral delivery. Both windows report first/next sequence and bounded query truncation; stale stream/history IDs and pruned ranges are meaningful, not empty success. Signal lifetime is one simulation second; clock pause freezes age, and explicit clock steps expire signals. Capacity may evict younger signals. Runtime debug publishes `causal_history` and `world_signals` separately, with schema definitions in `component_schemas`.

A save restores facts and the authoritative clock transactionally. Signals start empty with a new stream when next emitted. Editor Play uses the same clone/restore behavior and normal gameplay module API. Full/reduced/inactive regions do not run event callbacks every frame: publication is explicit, and no event-driven rule evaluator is included in this slice. Future rule/agent consumers can incrementally follow sequence windows while handling missed/pruned evidence explicitly.

Run `vice_world_event_tests`, `vice_event_runtime_tests` and `scripts/validate_event_workflow.py`. Add `--run-runtime` to the workflow for the native animated character/vehicle checkpoint tour and cross-process causal continuation. Existing causal-history tests also derive typed gameplay facts from the physics/dependency evidence chain.
