# ADR 0010: Cooked shader modules

Status: Accepted

## Context

The shader cooker can produce DXIL for Direct3D 12 and metallib output for Metal. The RHI still only accepts source text, so cooked artifacts cannot yet enter the renderer through the normal shader module path.

The RHI should not own file-system policy. Asset loading belongs above the renderer, while the renderer only needs the binary bytes, shader stage, binary format, and entry point metadata.

## Decision

VICE adds `ShaderBinaryDesc` beside the existing development source descriptor.

A binary descriptor contains:

- shader stage
- binary format
- a read-only byte span
- entry point
- debug name

Direct3D 12 accepts DXIL. The backend copies the bytecode into the shader module so callers do not need to keep the input span alive after creation.

Metal accepts metallib data. The backend creates a Metal library from the supplied bytes, resolves the requested function, and stores the resulting library and function in the shader module.

The RHI does not accept file paths. Higher-level asset code is responsible for reading `shader.dxil` or `shader.metallib` and passing the bytes to the renderer.

Runtime source compilation remains available as a development fallback until VICE Lab and the asset layer load cooked shaders by default.

## Consequences

Cooked and development shaders produce the same `ShaderModule` interface after creation.

Graphics pipeline code does not need to know whether a shader came from source or a cooked asset.

The input binary span only needs to remain valid for the duration of `create_shader_module`.

Backend binary formats remain explicit. Passing DXIL to Metal or metallib to Direct3D 12 is rejected instead of silently translated at runtime.
