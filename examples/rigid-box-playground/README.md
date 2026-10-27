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

Use arrow keys to move the dynamic box and Space to jump from physical support. The
platform moves automatically. Losing focus clears held inputs. Movement sets
velocity through the shared simulation service, preserving vertical velocity
between jumps. Support comes from narrow-phase solid-contact queries, including sleeping bodies
and moving/rotating platform velocity. This is a box-control demonstration, not a
capsule character controller. The camera is fixed and leaving the
floor is possible. Relaunch to reset.

The build command compiles `game/game.cpp` as a separate shared module and cooks
and stages the same World and mesh data used by normal projects. Runtime debug
can inspect the actor `94000000-0000-4000-8000-000000000202` and platform
`94000000-0000-4000-8000-000000000203`. Playing leaves authored source unchanged.

Open this project in VICE Editor and use **Debug → Play In Editor → Play**.
Click the Scene viewport to send arrow-key/Space input. Pause/Resume freezes or
continues gameplay; Step runs one tick; Reset rebuilds code and starts fresh.
The preview uses an isolated clone, including unsaved World edits, and displays
live poses through the editor camera. Stop returns to authoring. Build/start
errors preserve the previous preview session.
