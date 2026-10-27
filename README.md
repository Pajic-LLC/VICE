# VICE

VICE is Pajic Studios' proprietary game engine project, developed by Pajic LLC.

The source is temporarily public during early foundation development so the project can use public continuous integration and be inspected as it takes shape. Public visibility does not make VICE open source. See `LICENSE` for the terms that apply to this repository.

The goal is broad on purpose: developers should be able to build any kind of game without inheriting one genre, one simulation model, or one visual style from the engine.

VICE starts with macOS ARM64 and Windows x64 as first-class platforms.

## Current status

Foundation development has started. The current work establishes the build, platform boundaries, world model, simulation contracts, rendering architecture, tests, and developer tooling before larger features are added.

The first executable is `vice_lab`. It is an integration target for engine systems, not a game template.

The [vehicle playground](examples/vehicle-playground/README.md) demonstrates authored
ray-suspension chassis, shared-material traction, SDK controls and checkpoint resume.
See [authored vehicles](docs/architecture/authored-vehicles.md) for the engine contract.

## Build

VICE requires CMake 3.28 or newer and a C++23 compiler.

With Ninja installed:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Without presets:

```sh
cmake -S . -B build -DVICE_BUILD_TESTS=ON -DVICE_BUILD_LAB=ON
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

## Project rules

- VICE models interactions, not genres.
- Gameplay has final authority over simulation.
- Rendering, simulation, audio, AI, and gameplay share an authoritative world model.
- Expensive systems need lower-fidelity paths or an off state where practical.
- macOS and Windows stay first-class targets.
- Platform-specific code stays behind platform interfaces.
- Game-specific assumptions stay outside engine core.
- Third-party implementations do not define public VICE APIs without a documented reason.
- Documentation uses direct human language. No em dashes and no filler.

Read `docs/constitution.md` before making architecture changes.
