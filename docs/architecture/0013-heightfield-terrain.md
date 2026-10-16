# ADR 0013: Heightfield terrain collision

Status: Accepted

## Decision

VICE represents regular terrain grids with a dedicated heightfield collision shape instead of expanding them into generic triangle meshes.

`HeightFieldShapeDesc` is VICE-owned data. It stores a square row-major grid, a local offset, X and Z sample spacing, a vertical scale, and float height samples. `heightfield_hole_sample` marks samples that do not participate in collision.

Heightfield coordinates are local to one rigid simulation region. Large-world global position remains owned by the World layer. Terrain data must not contain authoritative global float coordinates.

Heightfield bodies are static. They participate in raycasts, overlap and sweep queries as query targets, contact and trigger events, and the existing body-level material system. A heightfield is not valid as a moving overlap or sweep probe.

The Jolt backend converts the VICE hole sentinel to Jolt's private no-collision value. Jolt heightfield types and constants do not appear in public VICE headers.

## Precision

The backend uses 16 bits per height sample in Jolt to minimize runtime quantization error for the first terrain implementation. The public contract does not expose this backend choice.

## Collision assets

`VICECOLL` version 1 predates heightfields and is not extended silently. Heightfield cooking requires a new collision asset format version with an explicit compatibility path. Runtime heightfields can be registered directly until that format extension lands.

## Consequences

Regular terrain stays compact and query-efficient. General authored concave geometry remains an indexed triangle mesh. Games can use either representation without changing the World coordinate model or exposing a physics backend type.
