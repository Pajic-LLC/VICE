# World-owned character handoff

## Decision

World identity and rigid-domain character identity are separate.

A playable character is owned by a World entity through the entity's persistent `world::PersistentId`. The local `rigid_body::CharacterId` only identifies the controller inside one rigid simulation domain.

The World persistent ID does not change when the character moves between regions or when the transient generational `EntityId` changes after World restore or rebinding.

## Ownership

`WorldBridge` stores character ownership by `PersistentId`, not by the transient `EntityId` handle.

A World entity can create one bridge-owned character. Creation uses the entity's current spatial region and converts its global World position into the local position of the registered rigid domain.

The bridge record keeps the controller creation policy and the currently active local `CharacterId`.

## Local character IDs

`CharacterId` remains local to a rigid domain.

During handoff, VICE first tries to reuse the source local ID in the target domain. If that ID is already occupied, the bridge deterministically probes forward for a free ID. Zero and Jolt's reserved invalid 32-bit value are never selected.

This means two islands can use different local controller IDs for the same persistent World entity.

Gameplay that needs the current local ID asks `WorldBridge::character_id` instead of assuming the ID survives an island transfer.

## Transfer state

Character handoff preserves the live controller state that affects continued motion:

- foot position through global World coordinates
- current linear velocity
- current horizontal target velocity
- a pending one-shot jump request
- standing capsule dimensions
- mass and maximum push strength
- maximum walkable slope
- step height
- stick-to-floor distance
- static, dynamic, and trigger collision policy

`CharacterState` exposes the horizontal target and pending jump state so the transfer can capture live input state instead of relying on the original creation descriptor.

`Domain::set_character_velocity` provides a VICE-owned way to restore live controller velocity without exposing Jolt `CharacterVirtual`.

## Coordinate conversion

The source controller remains in the source island's local float coordinate frame.

For handoff, VICE converts the live source foot position to `GlobalPosition` with the source region origin. It then converts that global position into the target region's local coordinate frame.

The World entity's spatial state is updated with the same global position and the target region only after the target controller is fully recreated.

This keeps large global coordinates out of the local character solver while preserving exact World ownership semantics.

## Transaction order

Character handoff is transactional.

The order is:

1. validate the World entity, persistent owner, source region, target region, and registered domains
2. capture live source character state
3. calculate the global foot position and target-local foot position
4. choose an available target-local `CharacterId`
5. create the target controller with the stored VICE controller policy
6. restore live velocity, horizontal target velocity, and pending jump state
7. update the World spatial state to the target region
8. destroy the source controller
9. commit the bridge record to the new local ID

If target creation or state restoration fails, the target controller is removed and the source remains untouched.

If the World spatial update fails, the target controller is removed and the source remains untouched.

If source destruction fails after the World update, the previous World spatial state is restored and the target controller is removed.

The bridge record changes only after the source controller is successfully destroyed.

## Sync back to World

`sync_character_to_world` reads the live character foot position in the current rigid domain and writes it back through `World::set_local_position`.

That operation preserves the entity's current region and updates its global position using the region origin.

This is the same World authority model used by rigid-body synchronization.

## Persistent handle rebinding

Because bridge ownership is keyed by `PersistentId`, a transient World `EntityId` can be destroyed and recreated with the same persistent ID while the character remains live.

After the replacement entity receives the correct spatial state, `character_id` and `sync_character_to_world` resolve the existing bridge-owned controller through the persistent owner key.

This is foundation behavior for save and restore integration. It does not by itself serialize the native controller.

## Current boundaries

This milestone does not define:

- automatic region-boundary detection
- automatic streaming-driven handoff scheduling
- character versus character collision across islands
- network prediction, rollback, or reconciliation
- native character serialization
- crouching or shape switching during transfer
- root-motion transfer policy
- arbitrary up vectors or planetary gravity

Those systems can build on persistent ownership and transactional handoff without changing the public identity model.
