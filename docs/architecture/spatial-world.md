# Spatial world model

VICE keeps authoritative world position separate from the coordinate spaces used by simulation and rendering.

## Types

`GlobalPosition` is the authoritative double-precision position in meters.

`LocalPosition` is a float position relative to a region origin. Simulation backends may consume and produce this representation.

`RegionId` identifies a local coordinate and ownership region.

`SimulationIslandId` identifies the simulation ownership domain associated with a region.

`StreamingCellId` identifies the streaming cell associated with a region. It is routing and residency metadata, not entity identity.

`RegionFidelity` records whether a region is inactive, reduced, or full fidelity.

`SpatialState` stores an entity's global position and current region.

`SpatialReference` stores a persistent entity ID with an optional last-known cell hint for cross-cell references.

## World ownership

Spatial state is optional per entity. Entities without spatial state remain valid World entities and do not pay for a required transform object.

Regions are registered with `World`. An entity can only be assigned to a registered region.

Destroying an entity removes its spatial state. Reusing the runtime slot does not transfer spatial state to the new entity generation.

A region cannot be removed while a live entity is assigned to it.

## Coordinate conversion

`to_local_position(global, region_origin)` subtracts in double precision and converts the result to floats.

`to_global_position(local, region_origin)` adds a local float position to a double-precision region origin.

`camera_relative_position(global, camera_global)` subtracts the camera position in double precision and returns a float result for rendering.

Conversions fail when inputs are not finite or when a relative value cannot fit in a float.

## Simulation island handoff

`World::handoff_spatial` changes the entity's region while preserving its authoritative global position.

After handoff, `World::local_position` derives a new local float position from the target region origin. This is the value a local simulation backend should consume when it creates or transfers its native body.

Persistent entity identity is unaffected by handoff.

## Small worlds

A small game uses one region with a zero global origin, one simulation island, and one streaming cell. The same API works without a separate small-world mode.

## Current boundary

This layer does not yet schedule streaming, create or destroy solver-native bodies during handoff, replicate spatial state, or manage hierarchical transforms. It establishes the coordinate and ownership contract those systems will use.
