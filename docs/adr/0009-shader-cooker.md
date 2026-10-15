# ADR 0009: Shader cooker and backend shader binaries

Status: Accepted

## Context

VICE now has a portable graphics pipeline and draw API, but its first triangle still compiles source shaders at runtime. Runtime compilation is useful for bootstrap work, but it is not the shipping asset model.

The renderer needs one authoring language, reproducible build outputs, backend-native shader binaries, readable diagnostics, and a cache key that changes when shader inputs or compiler versions change.

## Decision

VICE shaders are authored in HLSL.

The shader cooker compiles HLSL to validated DXIL with DXC. The DXIL is the Direct3D 12 artifact and is also the input to Apple Metal Shader Converter. Metal Shader Converter produces the metallib artifact used by the Metal backend.

The first cooker supports vertex, fragment, and compute stages. Shader Model 6.0 is the default baseline and can be raised per invocation when a shader needs newer features.

The cooker can target Direct3D 12, Metal, or both. A Metal target always produces DXIL first because Metal Shader Converter consumes DXIL.

Tool discovery follows this order:

1. explicit command-line path
2. `VICE_DXC` or `VICE_METAL_SHADER_CONVERTER`
3. the process `PATH`

Every cooked shader gets a deterministic SHA-256 cache key. The key includes source bytes, entry point, shader stage, target, shader model, build mode, and detected tool version output.

The cooker writes a versioned JSON manifest beside the binary artifacts. The manifest records the cache key, shader profile, entry point, target, tool versions, artifact filenames, and reflection schema version.

The first reflection block is structurally present but contains no resource bindings yet. Resource reflection will expand when the RHI binding model is implemented.

## Consequences

Game code does not need to know how DXC or Metal Shader Converter are invoked.

A source or toolchain change invalidates the shader cache automatically.

The Direct3D 12 shader path can be validated in normal Windows CI using DXC from the Windows SDK.

Metal conversion requires Apple Metal Shader Converter. CI can enable full DXIL to metallib validation once the converter package is installed on the runner.

Runtime source compilation remains available as a development fallback until cooked binary loading is fully integrated and VICE Lab runs from cooked assets.
