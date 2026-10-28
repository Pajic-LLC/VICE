# Gameplay quantities

See [ADR 0030](../adr/0030-generic-quantity-transactions.md) for atomicity, persistence, receipt protocol, limits and alternatives.

QuantityTypeId is distinct from the engine's asset ResourceId. Register a game-defined UUID/name/version and positive `quanta_per_unit` metadata. Amounts are exact int64 quanta, never floating-point values or automatically converted units. Core does not know whether a type represents money, grain, ammunition or fuel.

A QuantityAccount is keyed by persistent holder entity UUID and type UUID, with nonnegative amount/capacity. Runtime creation starts empty. `set_quantity_capacity` rejects shrinking below the amount; removal and holder destruction reject nonempty quantities. To discard quantities intentionally, consume them. To move them, transfer them. Multiple containers can be ordinary entities related to their owner.

`World::transact_quantity` accepts produce/consume/transfer, type, source/target UUIDs, positive amount and optional causal parents. Produce requires a nil source; consume a nil target; transfer requires distinct existing accounts. Failure changes neither balance nor receipt history. Success returns exact before/after balances and a retained event reference. A failed receipt publication reports the underlying World-event reason. Type definitions remain alive while accounts or retained receipts use them.

The reserved `vice.quantity.transaction` schema is a generic engine receipt, not game content. Normal event/schedule/component APIs cannot fabricate it. Its structured fields explain operation, type/version, quantity and exact source/target deltas. Historical receipts can reference deleted entities; their catalog definition remains guarded until retention pruning. The journal is bounded diagnostic/causal evidence, not the source of balance truth or a permanent ledger.

Source grammar:

```
quantity_type TYPE_UUID VERSION QUANTA_PER_UNIT "Name"
quantity_account HOLDER_UUID TYPE_UUID AMOUNT CAPACITY
```

Revision-safe edits use `register_quantity_type` with the type body, `set_quantity_account` with the account body, and `remove_quantity_account HOLDER TYPE`. These are explicit authoring operations for initial/reconstructed state. A conflicting registered definition or invalid second operation rejects the whole file transaction. Runtime uses transactions instead of authoring balance overrides.

Snapshot v21 reads v1–20 with empty quantity tables. World clone/reset/checkpoint/save semantics are identical in Editor and standalone Runtime. Full restore validates a replacement World transactionally. Render projections omit these tables. Generic quantities need no per-entity tick and follow persistent IDs through region fidelity changes/handoff.

The presentation module seeds 100 game-defined ExampleUnits once, then transfers 25 on startup. A checkpoint resumes 75/25 holdings and a fresh process continues to 50/50, with the new receipt referencing the previous transfer and startup cause. `scripts/validate_quantity_workflow.py --run-runtime` verifies this alongside physical walking/driving, animation, audio and scheduling. `vice_world_quantity_tests` and `vice_quantity_runtime_tests` cover failure atomicity, schema guards, exact maximum quantities, identity/persistence, SDK restrictions and Editor parity.
