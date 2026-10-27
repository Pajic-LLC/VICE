# Character Playground

This SDK-only project combines an authored upright capsule, a low stair, a tall
wall, a moving platform, a breakable link and supplied lights near a 10^12-meter
global origin. Floor height is y=50. The rendered capsule is a simple procedural
mesh aligned to the physical feet; it is not an animated character asset.

```sh
build/tools/vice_cli/vice project build examples/character-playground/character.viceproj
build/tools/vice_cli/vice project run examples/character-playground/character.viceproj
```

Open the project in Editor and use Play for the same isolated game. Focus Scene
for gameplay input. Arrow keys move in world XZ; Space requests a grounded jump;
B applies real physical stress to the hanging payload, breaking the finite-strength
link and removing the orange lamp's supply. The independent blue lamp stays
supplied. The platform at the left moves horizontally and can carry a grounded
capsule. The camera remains fixed. Pause, Step, Reset and Stop use the existing
preview controls. Reset/relaunch begins with authored state.

For normal runtime inspection, the capsule ID is
`9b000000-0000-4000-8000-000000000202`. Debug shows actual/desired velocity,
pending jump, ground classification, support identity/normal/velocity. The failed
lamp is `9b000000-0000-4000-8000-000000000211`; its root cause is physical link
`9b000000-0000-4000-8000-000000000208` through harness `...0210`.

The acceptance script authors step height 0.45 and desired X velocity 2 through a
revision-safe transaction. Nonzero initial desired X enables this sample's automatic
tour: climb the stair and pause, walk into the tall wall, jump and land, then stress
the physical supply gate. A key press returns to manual control. This behavior
belongs to the sample module; the engine has no fixture IDs or tour logic.

```sh
python3 scripts/validate_character_workflow.py --vice <executable> --run-runtime
```

Native acceptance verifies the tour and unchanged authored source. Omitting
`--run-runtime` exercises editing, validation, cooking, visual-mesh staging and
native module compilation without opening a game window. This also works in a
backend-disabled authoring build; running the physical game requires Rigid3D.
