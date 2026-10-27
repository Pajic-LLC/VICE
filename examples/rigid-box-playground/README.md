# Rigid Box Playground

An arbitrary authored project with a static floor, controllable dynamic box,
automatic kinematic platform, camera and directional light. All entity IDs belong
to this project; no Lab fixture setup is required. It uses only the public module
SDK and authored assets, at a global origin of 10^12 meters.

Build the engine with tools, Player and Rigid3D enabled, then:

```sh
build/tools/vice_cli/vice project build examples/rigid-box-playground/playground.viceproj
build/tools/vice_cli/vice project run examples/rigid-box-playground/playground.viceproj
```

Use arrow keys to move the dynamic box and Space to jump near the floor. The
platform moves automatically. Losing focus clears held inputs. Movement sets
velocity through the shared simulation service, preserving vertical velocity
between jumps. This is a box-control demonstration, not a character controller;
the jump condition is a height heuristic. The camera is fixed and leaving the
floor is possible. Relaunch to reset.

The build command compiles `game/game.cpp` as a separate shared module and cooks
and stages the same World and mesh data used by normal projects. Runtime debug
can inspect the actor `94000000-0000-4000-8000-000000000202` and platform
`94000000-0000-4000-8000-000000000203`. Playing leaves authored source unchanged.
