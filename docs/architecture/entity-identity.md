# Entity identity

VICE separates runtime entity handles from persistent identity.

## Runtime handles

`EntityId` is a compact generational handle. The index selects a slot and the generation changes when that slot is reused. A stale handle therefore fails the world's `alive` check instead of silently referring to a new entity that reused the same index.

Runtime handles are process-local. They are not save file IDs, network identities, or asset IDs.

## Persistent identity

`PersistentId` is a 128-bit identifier with canonical text encoding. Randomly created IDs use the UUID version 4 bit layout. The text representation is suitable for files, debugging, and data exchange.

A world maps persistent identity to the current runtime handle. Loading the same persistent identity in a new world does not require the runtime handle to match the previous session.

## Relationships

The first `Relation` type contains a subject entity, a persistent relationship kind, and an object entity. The initial world implementation stores relationships in a simple vector because the correct high-performance layout has not been measured yet.

This representation is intentionally basic. It proves stable semantics before the ECS and relationship query architecture are selected.

## What this does not decide

This work does not select the final ECS storage model, component layout, archetype strategy, serialization format, or relationship index. Those decisions need workloads and profiling data.
