# Collision assets

VICE collision assets move authored rigid geometry from import or tooling into the runtime without exposing an authoring format or solver format to the engine.

## Ownership

The collision cooker consumes VICE-owned `CollisionAssetShape` records. Each record contains a stable `ShapeId` and one VICE `ShapeDesc`.

The runtime consumes the cooked VICE collision binary through `decode_collision_asset` and `register_collision_asset`.

Neither side serializes Jolt shapes, body IDs, sub-shape IDs, acceleration trees, pointers, allocator state, or platform ABI data.

## Binary header

VICECOLL files are explicitly little-endian.

The header is:

1. 8 bytes: `VICECOLL`
2. 32-bit format version
3. 32-bit shape count
4. 64-bit canonical payload fingerprint
5. shape records

Every shape record starts with:

1. 64-bit `ShapeId`
2. 32-bit shape tag
3. shape-specific payload

Float values are stored as IEEE 754 32-bit bit patterns in little-endian order. The format never writes a C++ struct directly, so compiler padding and host ABI do not affect the asset.

## Versions

The runtime accepts VICECOLL versions 1 and 2.

Version 1 contains the original six shape tags. Its meaning is frozen.

Version 2 adds heightfield terrain as tag `7`. No version 1 tag was changed or reused.

The cooker chooses the oldest format that can represent the source. Geometry containing no heightfield is still written as version 1. Geometry containing a heightfield is written as version 2. Existing version 1 source therefore keeps the same payload encoding, ordering, and fingerprint behavior after version 2 support is added.

A version 1 asset containing tag `7` is invalid. A manually constructed version 1 `CollisionAsset` containing a `HeightFieldShapeDesc` is also rejected during registration.

## Shape payloads

The shape tags are:

- `1`: box
- `2`: sphere
- `3`: capsule
- `4`: convex hull
- `5`: indexed triangle mesh
- `6`: compound
- `7`: heightfield, version 2 only

Boxes store three half extents.

Spheres store one radius.

Capsules store half height and radius.

Convex hulls store a 32-bit point count followed by local `Vec3` points.

Triangle meshes store 32-bit vertex and index counts, then local vertices and 32-bit indices.

Compounds store a 32-bit child count. Each child stores a `ShapeId`, local position, and quaternion orientation.

Heightfields store a 32-bit square sample count, local `Vec3` offset, X sample spacing, Z sample spacing, vertical scale, then `sample_count * sample_count` row-major float samples. `heightfield_hole_sample` is serialized as its canonical positive-infinity float bit pattern. Other non-finite samples are invalid.

## Canonical ordering

Cooking is deterministic for identical geometry and IDs even when the input shape order changes.

The cooker first validates every shape and builds compound dependencies. It then performs a deterministic topological order. Shapes that are simultaneously ready are ordered by `ShapeId`.

This guarantees that every compound appears after all of its referenced child shapes. The runtime can therefore register one decoded asset in a single forward pass.

Missing child references and dependency cycles are rejected during cooking.

## Validation

The cooker rejects invalid primitive dimensions, non-finite geometry, hulls without volume, malformed mesh counts, out-of-range mesh indices, degenerate mesh triangles, invalid heightfield dimensions or samples, invalid compound poses, duplicate IDs, missing compound children, self references, and compound cycles.

The runtime decoder independently validates magic, format version, count limits, shape IDs, version-specific tags, heightfield allocation bounds, dependency order, trailing data, and the payload fingerprint before returning a `CollisionAsset`.

`Domain::register_shape` remains the final solver-side validation boundary when decoded shapes enter a rigid domain.

## Fingerprint

The source fingerprint is 64-bit FNV-1a over the canonical shape payload.

It is deterministic and independent of input record order because the payload is dependency-sorted before hashing.

The fingerprint is for cache keys, change detection, reproducibility checks, and accidental corruption detection. It is not a cryptographic signature and must not be used as an authenticity or security boundary.

## Transactional registration

`register_collision_asset` registers decoded shapes in dependency order.

If a shape cannot be registered, every shape added by that call is removed in reverse order. Existing domain shapes are left untouched.

This prevents a failed asset load from leaving a partially registered collision package in the domain.

## Developer source manifest

`vice_collision_cooker` accepts a small line-oriented source manifest for direct tool use and tests.

Version 1 remains valid:

```text
vice-collision-source 1
```

Version 2 enables heightfield input:

```text
vice-collision-source 2
```

The forms shared by both source manifest versions are:

```text
box <id> <hx> <hy> <hz>
sphere <id> <radius>
capsule <id> <half-height> <radius>
hull <id> <point-count> <x y z>...
mesh <id> <vertex-count> <index-count> <x y z>... <indices>...
compound <id> <child-count> <child-id px py pz qx qy qz qw>...
```

Source manifest version 2 also accepts:

```text
heightfield <id> <sample-count> <offset-x> <offset-y> <offset-z> <spacing-x> <spacing-z> <vertical-scale> <samples>...
```

A height sample may be a finite float or the literal `hole`.

`#` starts a comment.

The source manifest is a developer interchange format, not a runtime format and not the long-term authoring interface. Importers tracked by #61 should call the collision cooker library directly with structured VICE geometry instead of generating text.

## Command line

The standalone tool is:

```text
vice_collision_cooker --input source.vcollision --output asset.vcollisionc
```

The output file is the versioned binary described above.
