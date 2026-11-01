# 0037: Persistent blended intent, structural failure, and equipment exclusions

Date: 2026-10-30
Status: Proposed in the CRASHOUT capability candidate

## Context

An external game needs to blend imported character motion, sample actuator intent,
and retain solver-driven connection failure across cold reconstruction. Physical
footwear must collide with the ground without feeding back into its owner's
coarse capsule. These requirements do not justify a second game-owned pose or
physics database.

## Decision

Store the secondary animation clip, clock and blend weight in World. Snapshot
version 26 persists them; earlier snapshots load as unmixed. Blend local TRS before
hierarchy composition and physical rig overrides. Keep both animation clocks in
presentation cache keys.

Append guarded `control_animation_mix` and `read_rig_node` callbacks to module ABI
version 1. Callers and hosts check structure size. Queries explicitly select intent
or the physically overridden pose, in world coordinates, during the supported
lifecycle phase. These operations do not grant a game direct solver access.

Extend swing/twist source records with optional linear impulse failure and
persistent measured failure state, retaining version 1 input. The native solver
owns failure detection. World receives the break identity, impulse and step;
dependency state derives from the failed connection. Loading a broken connection
must not repair it or emit a second failure.

Represent character/equipment collision exclusions with a symmetric typed World
relation. Derive native query filters at reconstruction and reject stale live
structural edits before advance. Apply the same filter to character movement and
carrier exit preflight. Equipment keeps its contacts with the rest of the world.

## Consequences

Games author rigs, clips, connection strengths, appearance and actuator intent.
VICE owns generic state, lifecycle validation, solver events and persistence.
These additions do not provide a biped balance controller, terrain-aware whole-leg
IK, plastic deformation, material fatigue or soft rubber simulation. They must not
be described as completing those capabilities.

Regression coverage includes local-space length preservation, blended clocks,
old-host and thread/phase guards, checkpoint continuity, solver failure and cold
wreck reconstruction, and owner-specific equipment exclusion. Native appearance
and physical feel remain separate acceptance requirements.

## Deforming asset cook policy

Imported rig meshes explicitly request `virtual_geometry dense` in the existing
mesh source format. The default stays `streamed`. Dense cooking uses the supported
v5 archive, retaining exact vertices, indices, clusters and page metadata while
skipping static proxy simplification. A static simplified surface is not an
equivalent representation of a weighted animated mesh. This policy also prevents
pathological simplification cost for disconnected hair clumps. It is not a polygon
reduction or a substitute for animated LODs and GPU skinning. Unknown policy values
fail parsing. Regression coverage decodes the dense archive and checks exact
geometry retention; existing v6/v7 streaming regressions continue to run.
