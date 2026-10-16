# Collision import handoff

VICE keeps authoring formats outside runtime physics. An importer may parse glTF, FBX, OBJ, a custom level format, or editor data, but it hands collision geometry to the engine through VICE-owned records before cooking.

## Boundary

`VICE::CollisionImport` accepts `ImportedMesh` records. Each record contains:

- a stable source key supplied by the importer
- source positions and triangle indices
- the source mesh local transform
- whether the mesh is also render geometry
- collision authoring metadata

The collision metadata currently selects one of three representations:

- none
- indexed triangle mesh
- convex hull

A render-only mesh uses `none`. A dedicated collision mesh can set `render_mesh` to false and select a collision representation. A mesh may also be both render and collision geometry.

The bridge does not parse any file format. It does not know about glTF nodes, FBX objects, OBJ groups, or editor-specific types.

## Stable shape identity

An importer may provide an explicit `ShapeId` when its source format already has a stable collision resource identifier.

Otherwise the bridge derives a deterministic nonzero `ShapeId` from the stable source key and collision representation. Reordering imported meshes therefore does not renumber collision shapes.

Collision-enabled source keys and resulting shape IDs must be unique in one import batch. Collisions are rejected instead of silently renumbered.

## Source transforms

Source transforms are validated and baked into generated collision vertices before cooking.

The bridge applies local scale, normalized quaternion rotation, then local translation. Zero scale and non-finite transforms are rejected.

If the scale has a negative determinant, triangle winding is reversed while baking indexed meshes. This preserves surface orientation for mirrored source nodes.

The original source transform is also retained in the returned tooling binding for diagnostics and importer bookkeeping. Runtime collision assets contain only the resulting VICE geometry.

## Cooking

`cook_collision` performs this path:

1. validate imported mesh records
2. generate VICE `CollisionAssetShape` records
3. call the existing collision cooker library
4. decode the result through the runtime decoder as an integration check
5. return cooked bytes, fingerprint, asset version, and source bindings

Canonical ordering and the collision asset fingerprint remain owned by the cooker. The same imported source data therefore produces the same cooked bytes even when the importer enumerates meshes in a different order.

## Future importers

Format-specific importers should convert their own scene representation into this boundary directly. They should not generate the developer text manifest and should not call Jolt.

Authored convex decomposition can be represented as multiple convex collision meshes. A later grouping layer may turn those pieces into compounds when the authoring format provides that intent.

Terrain import should map regular height grids to `HeightFieldShapeDesc` through a terrain-specific handoff instead of flattening them into generic meshes.
