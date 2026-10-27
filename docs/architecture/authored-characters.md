# Authored capsule characters

A spatial entity can carry a capsule controller instead of a rigid box:

```text
entity 9a000000-0000-4000-8000-000000000002 spatial 1000000000000 50 0 1 character 1.8 0.3 70 100 0.872664626 0.4 0.5 0 0 0 0 0 0 0
```

After `character`, fields are height, radius, mass, max strength, max slope angle
(radians), step height, floor-stick distance, actual velocity xyz, desired
horizontal velocity xyz, and pending jump speed (zero means no jump). Dimensions
use physical meters, independent of visual AuthoredTransform. World spatial state
is the capsule foot point. The capsule is Y-up and solid-body collision excludes
triggers. A mesh authored from y=0 to y=height will align with the physical feet.

Use `set_character <entity-id> <same fields>` or `clear_character <entity-id>` in a
revision-checked `.viceedit`. The whole candidate must be valid, including the
one-physical-representation rule. Changing profile, physical pose or adding/removing
controllers while running requires reset. Visual-only transforms are independent
of collision. Playing preserves authored source; saving a running World snapshot
preserves current velocity, desired motion and a pending jump.

SDK gameplay uses `Context::read_character` and `Context::control_character`.
Movement is meters/second in world XZ. Jump speed is a positive one-shot upward
speed, accepted only on walkable ground when not separating upward or already
queued. Zero jump keeps any previously queued request. A failed jump rejects the
whole movement/jump command; retry movement alone if desired. Desired motion is
published immediately and executed on fixed simulation steps. Read support state
and supporting body identity from the character query rather than a height test.

World inspect shows the component. Native Runtime debug also shows
`character_state` with ground classification, normal, velocity and supporting
identity when physics is available. Preview debug snapshots include capsule shape,
live position and transfer events. Both preview and standalone bootstrap from the
same authored data and execute the same SDK module. Ready-region handoff preserves
identity and motion; dynamic physical-region activation remains separate work.

Open `examples/character-playground/character.viceproj`. Arrows move the capsule,
Space jumps, and B physically stresses the hanging link, cutting one lamp's supply.
A moving platform and low step/tall wall are included. The camera is fixed; there
is no third-person camera or animation layer. The acceptance script enables the
sample's automatic tour by authoring nonzero desired X movement, then observes the
same movement, jumping, collision and failure through Runtime debug.

```sh
python3 scripts/validate_character_workflow.py --vice <executable>
python3 scripts/validate_character_workflow.py --vice <executable> --run-runtime
```

See ADR 0019 for persistence, guards and limits. The first controller does not
provide crouching, character-peer policy, navigation, animation or network state.
