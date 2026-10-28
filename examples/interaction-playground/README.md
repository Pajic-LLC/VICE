# Walk and Drive

A bounded engine interaction prototype: walk to a primitive vehicle, attach, drive,
brake and restore a physical walking controller at a clear exit. An authored
nonzero walking command enables the automatic tour, which stops after 10 meters.

Arrows walk or drive; E enters/exits; Space jumps on foot and brakes in the car.
Move the mouse to orbit/pitch the following camera. Movement/E input takes over
from the automatic tour. Focus the game view in Editor. If an exit is blocked,
stop and choose clear ground. The prototype has one authored right-side exit;
seat/door selection is game policy, not an engine feature.

The avatar and vehicle are primitive meshes. There is no entry animation, seated
pose, wheel art, audio, traffic or camera collision avoidance. A carried character
has no independent physical capsule. The checkpoint preserves carrier/anchor,
body motion, vehicle state and camera pose. Module globals/device input are not saved.

Use `vice project build examples/interaction-playground/interaction.viceproj` or
open the project in Editor and Play. Runtime debug entity IDs end in 2 (vehicle),
3 (character) and 4 (camera), with prefix a2000000-0000-4000-8000-.
