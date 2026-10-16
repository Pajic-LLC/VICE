# Rigid shapes and spatial queries

VICE rigid domains own reusable collision shape resources through `ShapeId`.

The first registered shape kinds are box, sphere, and capsule. A registered shape is immutable. Bodies can share one shape resource without sharing body state.

`ShapeId` is a VICE identifier. It does not contain or expose a Jolt shape pointer.

## Shape lifetime

A shape is registered in one rigid domain.

A body created through the general `BodyDesc` path references a registered shape. The domain rejects removal of a shape while a live body still references it.

The existing `create_box` API remains available for the VICE Lab and WorldBridge compatibility path. Those legacy boxes use an internal solver shape and do not consume a public `ShapeId`.

## Queries

The first query set is:

- closest-hit raycast
- all-hit shape overlap
- closest-hit shape sweep

Queries operate in the local coordinate frame of one rigid simulation domain. Large-world callers choose the correct simulation island and convert authoritative World positions into that island's local frame before querying.

A query never returns a Jolt body ID, sub-shape ID, collector, or shape pointer.

A `QueryHit` contains only VICE data:

- participant ID
- material ID
- trigger state
- local contact point
- local contact normal
- distance
- normalized cast fraction
- penetration depth when the query begins overlapped

## Filtering

`QueryFilter` can include or exclude static bodies, dynamic bodies, and triggers. It can also exclude one participant, which covers common self-query cases without exposing solver handles.

Filtering is evaluated against VICE body metadata after native collision detection.

## Ordering

Raycasts and sweeps return the closest accepted hit. Equal cast fractions are resolved by lower `ParticipantId`.

Overlap queries return one result per participant sorted by ascending `ParticipantId`. If the solver reports more than one sub-shape result for one participant, VICE keeps the deepest result before removing duplicates.

This makes public ordering independent from Jolt collector order.

## Current limits

This slice does not yet add mesh, convex-hull, height-field, or compound shape assets.

Registered shapes are local resources inside a rigid domain. Asset-file serialization and cross-domain shape resource transfer belong to a later resource system milestone.

The large-world `RigidBodyWorld` bridge still transfers its existing box records. General registered-shape body handoff is not part of this slice.
