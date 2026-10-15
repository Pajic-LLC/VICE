# Technology baseline

This file records the current starting point. A library listed here is not automatically a permanent dependency.

## Engine language and build

- C++23 for engine and low-level runtime code.
- CMake for project configuration.
- Ninja is the preferred local generator when available.

## Platform targets

- macOS ARM64 is a first-class target.
- Windows x64 is a first-class target.
- Metal is the planned native macOS rendering backend.
- Direct3D 12 is the planned native Windows rendering backend.

## Shaders

The planned common shader source is HLSL.

Windows can compile HLSL through DXC to DXIL. The planned macOS path uses DXC output with Apple's Metal Shader Converter to produce Metal libraries. This path needs a working prototype before it becomes a permanent build dependency.

## Simulation

VICE owns simulation contracts, materials, interaction data, scheduling, fidelity, and cross-domain behavior.

Jolt is the leading candidate for the first rigid 3D backend. Box2D is the leading candidate for the first rigid 2D backend. They are implementation candidates, not public VICE APIs.

## Navigation

Recast and Detour are the leading baseline for navmesh generation and path queries. Higher-level VICE navigation must also represent actions such as climbing, jumping, opening, pushing, swimming, driving, and destruction-aware traversal.

## Audio

Steam Audio is a useful reference and possible initial backend for geometry-aware propagation. VICE must own its acoustic material and propagation contracts so gameplay and agent perception can consume the same world information.

## Scripting

Luau is the leading first scripting language. Engine core remains native C++.

## Assets

OpenUSD is the leading scene and production interchange format. glTF is a useful runtime-oriented interchange format. VICE runtime data should use cooked engine formats instead of loading authoring formats directly in performance-sensitive paths.

## Tooling

Dear ImGui docking is the leading bootstrap choice for editor and debug tooling. It is not a commitment to the final editor UI.

Tracy is the leading cross-platform CPU and GPU profiling integration for development builds. Native Metal and Windows GPU tools remain part of graphics debugging.

## Rule

No candidate becomes architectural just because it is convenient. Third-party code stays behind VICE interfaces when a future replacement is realistic.
