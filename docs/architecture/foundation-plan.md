# Foundation plan

The first goal is not to build a game. It is to prove that the engine boundaries work on macOS and Windows before feature count grows.

## Phase 0: repository and build spine

Status: in progress.

- C++23 project builds on macOS ARM64 and Windows x64.
- CMake owns project configuration.
- CI builds and tests both targets on every pull request.
- Core library has no platform SDK dependency.
- VICE Lab is the first executable used for integration work.

## Phase 1: platform and runtime

- Platform service interfaces.
- Logging and assertions.
- Monotonic time and frame clock.
- Job system contract and implementation.
- Memory tracking hooks.
- Crash context and diagnostic metadata.

## Phase 2: world and data

- Entity and component model.
- Relationships.
- Stable identifiers.
- Scene serialization contract.
- Global and local coordinate model.
- Basic resource handles.

## Phase 3: graphics foundation

- Rendering hardware interface.
- Metal backend.
- Direct3D 12 backend.
- Shared HLSL shader pipeline.
- Render graph.
- Buffers, textures, samplers, pipelines, command submission, fences, and swapchains.
- First PBR mesh and camera in VICE Lab.

## Phase 4: simulation foundation

- Material schema.
- Interaction events.
- Simulation domain contract.
- Fixed-step scheduling.
- Fidelity policy.
- First rigid 3D backend behind the VICE interface.
- Determinism and replay test harness.

## Phase 5: editor bootstrap

- Dockable editor shell.
- Scene view.
- Hierarchy and inspector.
- Console and profiler panels.
- Resource browser.
- Physics and world debugging views.

## Exit condition

The foundation is healthy when the same small scene can load on macOS and Windows, render through native graphics backends, simulate through a replaceable physics domain, expose state to the editor, save and reload, and pass the same automated behavior tests.
