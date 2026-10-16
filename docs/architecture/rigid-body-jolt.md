# Jolt rigid body backend

VICE uses Jolt Physics 5.6.0 as the first rigid-body solver backend.

Jolt is an implementation behind VICE-owned contracts. It does not define the simulation architecture.

## Public boundary

Public VICE rigid-body headers expose:

- `ParticipantId`
- static or dynamic motion
- box half extents
- local position and orientation
- linear velocity
- friction and restitution
- VICE-owned body state

Orientation uses `vice::math::Quat`. Solver state is converted to VICE math types before it leaves the backend.

No Jolt type, body ID, shape type, allocator, job system, or physics system appears in the public API.

`ParticipantId` maps to a native Jolt `BodyID` only inside the backend implementation.

## World ownership

`VICE::World` owns entity identity and lifetime.

`VICE::SimulationWorld` can associate a world entity with a rigid participant, but the association does not own the solver body. Destroying a solver body does not destroy the world entity. Destroying a world entity does not implicitly call into Jolt.

Lifecycle coordination stays explicit so streamed worlds and specialized simulation domains can choose their own teardown policy later.

## Coordinates

This first backend uses Jolt single-precision positions for local simulation state.

That does not define VICE large-world coordinates. Global world positions, simulation islands, and camera-relative rendering remain separate work. A future large-world layer can move entities between local simulation spaces without changing persistent world identity.

VICE math uses column-major matrices and column vectors. The rigid-body pose is converted to a VICE model matrix before rendering.

## Fixed stepping

The backend implements the existing `simulation::Domain` interface and consumes the coordinator fixed timestep.

VICE Lab steps the rigid domain at 120 Hz with a frame-time accumulator. Rendering samples the latest completed solver state.

Jolt cross-platform deterministic build mode is enabled for the pinned backend. This is a starting policy, not a promise that every future physics query has deterministic ordering. VICE must define its own ordering rules for any query where ordering becomes authoritative.

## Rendering proof

VICE Lab contains the first complete simulation-to-rendering vertical slice.

The Lab creates a `World` entity, binds it to a dynamic rigid participant, steps the Jolt-backed domain, reads the VICE-owned body pose, converts that pose to a model matrix, and uploads the matrix through a per-instance vertex stream.

The cube shaders consume the same model-matrix stream in cooked and development fallback modes. The renderer therefore does not depend on Jolt and the physics backend does not depend on the renderer.

The old elapsed-time cube animation is not part of this path. The visible cube transform comes from simulation state.

## Jobs

The first backend uses Jolt's reference thread-pool job system internally.

Jolt supports replacing this with an engine job-system implementation. A later performance slice should adapt Jolt jobs onto `VICE::Jobs` so VICE owns scheduling policy across simulation domains.

## Current scope

The first slice supports static and dynamic boxes, body destruction, position and orientation reads, linear velocity reads, fixed-step simulation, world binding, and a solver-driven rendering proof.

It does not yet define:

- general shape assets
- collision or trigger events
- constraints
- queries and casts
- continuous collision policy
- sleeping policy
- material mapping
- solver serialization
- simulation islands
- large-world handoff
