# World primitives

VICE World owns runtime entity identity, persistent identity, relationships, spatial state, component type registration, resource handles, and the serialization-facing snapshot contract.

It still does not commit VICE to a full ECS storage model.

## Entity identity

`EntityId` is a compact runtime handle made from a slot index and generation. Destroying an entity invalidates stale handles even when the slot is reused.

`PersistentId` is the serialization-facing identity. It is explicit, stable, and independent of memory addresses or runtime slot allocation.

Save systems and cross-system references should store persistent IDs where identity must survive a reload.

## Component registration

`ComponentTypeId` wraps a persistent ID. `ComponentTypeDesc` adds a human-readable name and a nonzero schema version.

World can register, query, and unregister component types. Registration currently describes type identity only. It does not choose archetypes, sparse sets, reflection storage, or another ECS implementation.

Component IDs and names are unique within one World registry.

The version field is reserved for serialization and migration decisions once component payload storage exists.

## Resource handles

Resources use the same identity split as entities.

`ResourceHandle` is a compact runtime index and generation. It becomes stale after the resource is destroyed, even if the slot is reused.

`ResourceId` wraps a persistent ID and is used for lookup and serialization-facing identity.

A restored World rebuilds resource handles from resource IDs. Runtime handle values are not part of the snapshot contract.

## World snapshots

`WorldSnapshot` is structured VICE data, not a file format.

A snapshot contains:

- registered component type descriptors
- persistent resource IDs
- registered regions
- entities by persistent ID with optional spatial state
- relations expressed entirely with persistent IDs

Snapshot collections are emitted in deterministic order where their source storage is unordered.

`World::restore` is transactional. It builds a temporary World and validates the complete snapshot before replacing the current World. Duplicate IDs, invalid regions, missing relation endpoints, bad component registrations, and other invalid records fail without partially modifying the destination World.

Runtime entity and resource handles are reconstructed. Persistent IDs, spatial state, relationships, component type identities, and resource identities survive the round trip.

## Boundaries

World does not expose a third-party ECS type.

World snapshots do not define JSON, binary, database, or network encoding. A later serialization layer can encode the snapshot without changing runtime identity rules.

World resource handles identify engine resources but do not define ownership, loading, streaming, or asset package policy. Those systems can build on the persistent `ResourceId` contract.
