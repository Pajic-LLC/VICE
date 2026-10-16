# Rigid-body island handoff

VICE keeps rigid-body solver ownership separate from World identity and global position.

## Boundary

`VICE::RigidBody` remains a local simulation backend. It knows about `ParticipantId`, local float poses, velocities, body shape data, and solver state. It does not depend on `VICE::World`.

`VICE::RigidBodyWorld` is the bridge between World spatial state, simulation bindings, and rigid-body domains.

The bridge does not own the World, the simulation coordinator, or rigid-body domains. It records which registered spatial region and simulation island are serviced by each rigid domain.

Each registered rigid region currently represents one solver coordinate frame. One simulation island may not be registered to two rigid regions in the same bridge.

## Creation

A box created through `WorldBridge` gets its solver position from the entity's current World region. The caller cannot make the body disagree with authoritative World position by supplying another local position.

The bridge records the VICE-owned creation data needed to recreate the box later. Jolt body IDs never enter this record.

## Live synchronization

`sync_body_to_world` reads the current local solver position and converts it back to double-precision global World position through the current region origin.

Orientation and velocity remain solver state in this slice. World currently owns authoritative position only.

## Cross-island handoff

`handoff_box` transfers one bridge-created box between two registered rigid regions with different simulation islands and different rigid domains.

The sequence is:

1. Read the live source body state.
2. Convert the live source local position to double-precision global position.
3. Convert that global position to the target region's local float position.
4. Create the target solver body with the same participant, shape, motion type, orientation, linear velocity, angular velocity, friction, and restitution.
5. Restore inactive state when the source dynamic body was sleeping or explicitly inactive.
6. Bind the World entity to the target rigid domain.
7. Update World spatial state to the live global position and target region.
8. Remove the source binding.
9. Destroy the source solver body.

The World entity handle and persistent ID do not change.

## Failure behavior

The source body is not destroyed before the target side is valid.

If target body creation, active-state restoration, target binding, or World spatial update fails, the temporary target body is removed and the source side remains authoritative.

If source cleanup fails after the target side was prepared, the bridge restores the previous World spatial state, removes the target side, and restores the source binding where possible.

This keeps handoff failure from silently orphaning a World entity between simulation islands.

## Current limits

This slice transfers bridge-created box bodies only.

It does not yet transfer constraints, contact caches, broad-phase history, solver warm-start state, compound shapes, or arbitrary shape assets. Those features need explicit VICE-owned transfer contracts before they can move between solver domains.
