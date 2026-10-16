# Rigid body game geometry

VICE rigid simulation supports authored collision geometry without exposing the solver backend.

## Shape data

A rigid domain can register these VICE-owned shape descriptions:

- box
- sphere
- capsule
- convex hull
- indexed triangle mesh
- compound

All geometry is local to the rigid simulation domain. It is not an authoritative large-world coordinate representation.

## Convex hulls

`ConvexHullShapeDesc` stores only a list of local VICE points.

The backend validates that the input is finite and contains enough points to form a volume. The native solver performs the final hull construction and rejects degenerate input that cannot produce a valid convex shape.

Convex hulls can be used by static and dynamic bodies. They can also be used as overlap and sweep query shapes.

## Triangle meshes

`TriangleMeshShapeDesc` stores local vertices and 32-bit triangle indices.

VICE rejects:

- fewer than three vertices
- index arrays that are not complete triangles
- indices outside the vertex array
- repeated indices inside one triangle
- non-finite vertices
- zero-area triangles

Triangle meshes are environment geometry. VICE currently allows them only on static rigid bodies.

Raycasts can hit triangle mesh bodies. Triangle meshes are not accepted as moving overlap or sweep probes.

The mesh contract does not contain Jolt materials, tree nodes, native sub-shape IDs, or other solver build data.

## Compounds

`CompoundShapeDesc` references already registered child `ShapeId` values and gives each child a local position and orientation.

A compound is immutable after registration.

A child shape cannot be unregistered while a registered compound references it. This dependency rule applies even when no body currently uses the compound.

A compound can be dynamic when all of its children support dynamic motion. If any child requires static motion, the compound also requires static motion.

Because compound children must already exist and shapes are immutable, a newly registered compound cannot create a dependency cycle.

## Queries and events

The existing VICE query and contact contracts do not change for authored geometry.

Raycasts, overlaps, sweeps, contact events, trigger events, material IDs, participant IDs, filtering, and deterministic result ordering remain VICE-owned.

Native body IDs, shapes, collectors, mesh trees, and sub-shape IDs stay inside the Jolt backend.

## Offline cooking

Runtime shape registration is not the final asset pipeline.

Issue #59 defines the offline collision cooker. That tool will own validation, deterministic cooking, format versioning, source fingerprints, and runtime asset packaging.

The rigid-body runtime will not parse OBJ, glTF, FBX, or other authoring formats. Importers and editor tooling will produce source geometry for the collision cooker, and runtime code will consume the cooked VICE collision asset.

Heightfield terrain is tracked separately in #60 because terrain has a more compact and specialized data contract than a generic triangle mesh.
