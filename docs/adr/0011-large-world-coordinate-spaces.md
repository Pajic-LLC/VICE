# ADR 0011: Large-world coordinate spaces

## Status

Accepted.

## Context

VICE needs to support small games and very large worlds without making rendering or simulation backends carry large global floating-point coordinates.

A single float coordinate space is not sufficient for large distances because local precision degrades as magnitude grows. A later floating-origin retrofit would force coordinate policy into rendering, physics, gameplay, streaming, networking, and saved data after those systems already depend on one representation.

## Decision

VICE separates authoritative world position from local simulation and rendering coordinates.

### Global space

Authoritative world position uses `GlobalPosition`, which stores three double-precision values in meters.

Global position belongs to the World layer. It is independent from rendering backends, simulation solvers, streaming residency, and local region ownership.

Persistent entity identity does not change when an entity moves through global space or changes region.

### Regions

A `RegionDesc` defines a local coordinate frame with:

- a stable `RegionId`
- a `SimulationIslandId`
- a double-precision global origin
- a `StreamingCellId`
- a region fidelity state

A region is an ownership and coordinate boundary. It is not entity identity.

Small worlds use the same model with one region whose origin is zero.

### Local simulation space

Simulation backends may use `LocalPosition`, which stores three floats.

A local position is derived by subtracting the region origin from the authoritative global position. Solver output is converted back by adding the local float position to the region origin in double precision.

No physics backend owns or stores the authoritative VICE global position.

### Rendering space

Rendering consumes camera-relative float positions derived from the difference between an object's global position and the camera's global position.

The renderer does not need a large global float coordinate.

### Island handoff

Moving an entity between regions changes the entity's region ownership only. Its global position and persistent identity remain unchanged.

The new local simulation position is derived from the target region origin after the handoff.

A backend may later destroy and recreate a native solver body during this operation, but that native lifetime does not affect VICE entity identity.

### Streaming references

Cross-cell entity references use `PersistentId` as the authoritative identity. `StreamingCellId` may be stored as a last-known routing hint, but it is never identity and may become stale.

### Fidelity

Regions carry `RegionFidelity` metadata with `inactive`, `reduced`, and `full` states.

This first foundation records policy state only. Individual systems decide how their work scales with that state through their own VICE contracts.

## Consequences

- Large-world precision policy is fixed before higher-level systems depend on one coordinate space.
- Physics can remain float-based and local.
- Rendering can remain camera-relative and float-based.
- Streaming can move entities between cells and islands without changing persistent identity.
- Small worlds pay only for one region and optional per-entity spatial state.
- Systems must be explicit about whether a value is global, local simulation, or camera-relative.

## Rejected alternatives

### One global float space

This loses local precision at large distances and would require rebasing later.

### Store solver coordinates as world truth

This would make the selected physics backend define VICE world architecture and would make multi-domain simulation harder.

### Make streaming cells entity identity

Cells are residency and routing information. Entity identity must survive movement between cells and streaming out and back in.
