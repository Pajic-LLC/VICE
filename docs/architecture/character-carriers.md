# Character carriers and gameplay cameras

World characters can be carried by solid rigid bodies without stepping a second
capsule inside the carrier. The CharacterAttachment stores a persistent carrier
UUID and its local foot anchor. The same mechanism serves a vehicle seat, moving
platform seat or another game-defined carrier.

```text
entity CHARACTER_UUID spatial ... character ... character_attachment CARRIER_UUID 0 -0.4 0
```

For live gameplay, use the size-checked SDK methods:

```cpp
context.attach_character(player, car, {0, -0.4F, 0}, 3); // anchor and allowed reach
context.detach_character(player, {2, -0.5F, 0}, 1, 0.5F); // candidate foot, speed, drop
```

The example values fit the roughly 1,200 kg playground chassis. Games choose their
own anchors/exits and policy. Entry checks proximity and active shared-region state.
Exit checks speed, ground, slope, whole-capsule clearance and the swept path. A
blocked result leaves the character carried. A successful exit recreates the walking
controller in the carrier's current region and inherits carrier point velocity.

`read_character_attachment` reports the persistent carrier and offset. Attached
`read_character` remains available with unsupported ground and no supporting
body; it describes carried motion rather than an independent capsule. Walking
commands return attached. Carrier destruction/removal is blocked until users are
released. External component/physical edits require a simulation reset.

Camera game code uses `read_camera_pose` and `set_camera_pose` on an existing
nonphysical camera entity. CameraPose holds global double position and unit
quaternion; lens/primary settings remain authored through existing Camera data.
The methods are phase/thread guarded. Camera movement works without a rigid-body
backend. Following, orbit and collision policy are game/rig concerns.

Open `examples/interaction-playground/interaction.viceproj`. Its automatic tour
walks to the primitive car, enters, drives ten meters, brakes and exits. Arrows walk
or drive, E enters/exits, Space jumps/brakes, and mouse movement orbits the following
camera. Movement/E input takes over from the tour. Avatar/chassis meshes are primitive;
there is no seated animation, wheel art, audio or camera collision avoidance.

```sh
python3 scripts/validate_interaction_workflow.py --vice <executable>
python3 scripts/validate_interaction_workflow.py --vice <executable> --run-runtime
```

Checkpointing while mounted preserves the carrier/anchor, vehicle motion, controls,
distance and camera pose. Restart reconstructs the carried representation and
continues normal stepping. Module globals/device input/native caches are not saved.
The example's automatic tour derives progress from saved state instead of an
unsaved timer. See ADR 0023 for format 14, safety bounds and limitations.
