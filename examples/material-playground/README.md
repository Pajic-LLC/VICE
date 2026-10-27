# Shared material playground

Build `materials.viceproj` through `vice project build` and run the staged game.
The authored World has four shared materials:

- Slippery: density 250 kg/m³, zero friction.
- Grip: density 2,000 kg/m³, friction 1.
- Elastic: density 500 kg/m³, restitution 0.9.
- Ground: density 1,000 kg/m³, friction 1.

Two one-metre boxes begin at 4 m/s. The slippery box continues with ordinary solver
damping while the grippy box stops. An elastic box rebounds beside a nonelastic
one. After 1.5 seconds the SDK stresses the hanging payload's link; its lamp loses
supply and the physical event retains both endpoint material UUIDs. The independent
lamp stays supplied. The SDK queries actual mass through the optional material API.
A resumed broken link is recognized without replaying the stress command.

The example uses the built-in mesh renderer. Shared definitions can also bind a
registered/cooked render material resource; the engine validates and resolves that
binding in both rendering paths. The sample does not require DXC or Apple's shader
converter and does not demonstrate separately cooked material shaders.

Run acceptance from the repository root:

```sh
python3 scripts/validate_material_workflow.py --vice build/tools/vice_cli/vice --run-runtime
```

The script edits Slippery's shared density to 300 kg/m³, verifies both users and
source revision behavior, builds the real SDK module, observes native mass/sliding/
rebound and material-aware failure, saves the running World, then restarts from it.
Omit `--run-runtime` for headless authoring/cook/build acceptance. Material edits in
an active simulation require reset. Source files remain unchanged by Runtime.
