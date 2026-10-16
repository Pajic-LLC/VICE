# Jolt rigid body backend

VICE uses Jolt Physics 5.6.0 as the first rigid-body solver backend.

Jolt is an implementation behind VICE-owned contracts. It does not define the simulation architecture.

## Public boundary

Public VICE rigid-body headers expose:

- `ParticipantId`
- shared `MaterialId`
- rigid `ShapeId`
- box, sphere, capsule, convex hull, indexed triangle mesh, and compound shape descriptions
- static or dynamic motion
- local position and orientation
- linear and angular velocity
- friction and restitution
- trigger state
- VICE-owned body state
- explicit active-state control
- VICE-owned contact and trigger events
- VICE-owned material-pair interaction rules
- VICE-owned raycast, overlap, and sweep results

Orientation uses `vice::math::Quat`. Solver state, contact data, and query results are converted to VICE types before they leave the backend.

No Jolt type, body ID, shape pointer, allocator, job system, contact manifold, collector, mesh tree, sub-shape ID, or physics system appears in the public API.

`ParticipantId` maps to a native Jolt `BodyID` only inside the backend implementation. `ShapeId` maps to a native Jolt shape only inside one rigid domain.

## World ownership

`VICE::World` owns entity identity, persistent identity, global position, and spatial region ownership.

`VICE::SimulationWorld` associates a world entity with a simulation participant. The association does not own the solver body.

`VICE::RigidBodyWorld` coordinates rigid-body creation and transfer for World entities without making the local rigid-body backend depend on World.

Destroying a solver body does not destroy the world entity. Destroying a world entity does not implicitly call into Jolt.

## Coordinates

Jolt uses single-precision positions for each local rigid simulation domain.

VICE World stores authoritative global positions in double precision. A registered rigid region provides the local coordinate frame for one rigid simulation island. `VICE::RigidBodyWorld` converts between global World positions and local Jolt positions during creation, synchronization, and island handoff.

Contact points and spatial query results reported by a rigid domain are local to that domain. Systems that need authoritative global positions combine the result with the World region that owns the simulation island.

Rendering remains camera-relative and does not consume large global float coordinates.

VICE math uses column-major matrices and column vectors. The rigid-body pose is converted to a VICE model matrix before rendering.

## Island handoff

A bridge-created box can move from one registered rigid simulation island to another without changing its World entity handle or persistent ID.

The bridge reads the live source body pose and velocities, computes the current double-precision global position, creates the target body in the target local coordinate frame, moves the simulation binding, updates World spatial state, and only then removes the source body.

The transfer preserves box extents, motion type, orientation, linear velocity, angular velocity, material ID, trigger state, fallback friction and restitution, and active state.

A material used by the transferred body must be registered in the target rigid domain. If the target domain is not prepared for that material, target body creation fails and the source side stays intact.

Target-side failures roll back without destroying the source body.

See `docs/architecture/rigid-body-island-handoff.md` for the exact transfer contract.

## Contacts and materials

Rigid domains use Jolt contact callbacks internally and publish VICE-owned events only after the physics update completes.

Jolt can invoke contact callbacks on worker threads. The backend copies the callback data into VICE types under synchronization and sorts the public event list deterministically before returning from the simulation step.

Shared `simulation::Material` data can provide rigid friction and restitution. Material-pair rules can override the combined response and attach a project-defined interaction label to the event.

Impact strength is reported as an estimated normal impulse from Jolt's pre-solve collision response estimator. It is not an exact post-solve impulse.

See `docs/architecture/rigid-body-contacts.md` for the event and material contract.

## Shapes and queries

Rigid domains can register immutable box, sphere, capsule, convex hull, indexed triangle mesh, and compound shape resources through VICE-owned `ShapeId` values. Multiple bodies can share one registered shape.

A registered shape cannot be removed while a live body references it. A child shape also cannot be removed while a registered compound references it. The older `create_box` entry point remains available as a compatibility path and does not require a public shape registration.

Convex shapes can be used by static and dynamic bodies and as moving overlap or sweep probes. Triangle meshes are environment geometry and are static-only. A compound inherits static-only behavior when any child requires it.

The spatial query set is closest-hit raycast, all-hit shape overlap, and closest-hit shape sweep. Filters can include or exclude static bodies, dynamic bodies, triggers, and one participant.

VICE defines public query ordering independently from native collector order. Equal ray or sweep fractions resolve by participant ID. Overlaps are reduced to one hit per participant and sorted by participant ID.

See `docs/architecture/rigid-body-shapes-queries.md` for the base shape and query contract and `docs/architecture/rigid-body-game-geometry.md` for authored geometry rules.

## Fixed stepping

The backend implements the existing `simulation::Domain` interface and consumes the coordinator fixed timestep.

VICE Lab steps the rigid domain at 120 Hz with a frame-time accumulator. Rendering samples the latest completed solver state.

Jolt cross-platform deterministic build mode is enabled for the pinned backend. This is a starting policy, not a promise that every future physics operation has deterministic ordering. VICE defines public contact and query ordering independently from Jolt callback or collector order.

## Rendering proof

VICE Lab contains the first complete simulation-to-rendering vertical slice.

The Lab creates a `World` entity, binds it to a dynamic rigid participant, steps the Jolt-backed domain, reads the VICE-owned body pose, converts that pose to a model matrix, and uploads the matrix through a per-instance vertex stream.

The cube shaders consume the same model-matrix stream in cooked and development fallback modes. The renderer therefore does not depend on Jolt and the physics backend does not depend on the renderer.

The old elapsed-time cube animation is not part of this path. The visible cube transform comes from simulation state.

## Jobs

The first backend uses Jolt's reference thread-pool job system internally.

Jolt supports replacing this with an engine job-system implementation. A later performance slice should adapt Jolt jobs onto `VICE::Jobs` so VICE owns scheduling policy across simulation domains.

## Current scope

The rigid-body foundation supports static and dynamic primitive and convex bodies, reusable box, sphere, capsule, convex hull, triangle mesh, and compound shape resources, static authored level meshes, general bodies created from registered shapes, body destruction, position and orientation reads, linear and angular velocity reads, active-state control, fixed-step simulation, shared material mapping, material-pair response overrides, collision events, trigger events, raycasts, shape overlaps, shape sweeps, world binding, a solver-driven rendering proof, World position synchronization, and box handoff between local rigid simulation islands.

It does not yet define:

- heightfield terrain shapes
- offline collision asset cooking
- constraints
- continuous collision policy
- exact post-solve impulse reporting
- solver serialization
- general registered-shape transfer between large-world islands
- constraint transfer between islands
- contact-cache or warm-start transfer between islands
