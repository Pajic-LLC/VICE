# Jolt rigid body backend

VICE uses Jolt Physics 5.6.0 as the first rigid-body solver backend.

Jolt is an implementation behind VICE-owned contracts. It does not define the simulation architecture.

## Public boundary

Public VICE rigid-body headers expose:

- `ParticipantId`
- static or dynamic motion
- box half extents
- local position and orientation
- linear and angular velocity
- friction and restitution
- VICE-owned body state
- explicit active-state control

Orientation uses `vice::math::Quat`. Solver state is converted to VICE math types before it leaves the backend.

No Jolt type, body ID, shape type, allocator, job system, or physics system appears in the public API.

`ParticipantId` maps to a native Jolt `BodyID` only inside the backend implementation.

## World ownership

`VICE::World` owns entity identity, persistent identity, global position, and spatial region ownership.

`VICE::SimulationWorld` associates a world entity with a simulation participant. The association does not own the solver body.

`VICE::RigidBodyWorld` coordinates rigid-body creation and transfer for World entities without making the local rigid-body backend depend on World.

Destroying a solver body does not destroy the world entity. Destroying a world entity does not implicitly call into Jolt.

## Coordinates

Jolt uses single-precision positions for each local rigid simulation domain.

VICE World stores authoritative global positions in double precision. A registered rigid region provides the local coordinate frame for one rigid simulation island. `VICE::RigidBodyWorld` converts between global World positions and local Jolt positions during creation, synchronization, and island handoff.

Rendering remains camera-relative and does not consume large global float coordinates.

VICE math uses column-major matrices and column vectors. The rigid-body pose is converted to a VICE model matrix before rendering.

## Island handoff

A bridge-created box can move from one registered rigid simulation island to another without changing its World entity handle or persistent ID.

The bridge reads the live source body pose and velocities, computes the current double-precision global position, creates the target body in the target local coordinate frame, moves the simulation binding, updates World spatial state, and only then removes the source body.

The transfer preserves box extents, motion type, orientation, linear velocity, angular velocity, friction, restitution, and active state.

Target-side failures roll back without destroying the source body.

See `docs/architecture/rigid-body-island-handoff.md` for the exact transfer contract.

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

The rigid-body foundation supports static and dynamic boxes, body destruction, position and orientation reads, linear and angular velocity reads, active-state control, fixed-step simulation, world binding, a solver-driven rendering proof, World position synchronization, and box handoff between local rigid simulation islands.

It does not yet define:

- general shape assets
- collision or trigger events
- constraints
- queries and casts
- continuous collision policy
- material mapping
- solver serialization
- constraint transfer between islands
- contact-cache or warm-start transfer between islands
