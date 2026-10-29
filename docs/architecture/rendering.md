# Rendering

VICE owns its rendering hardware interface. The first backends are Metal on macOS and Direct3D 12 on Windows.

## Current RHI

The RHI now covers the first usable resource and draw paths:

- native graphics device creation
- swapchain creation and presentation
- GPU-only, upload, and readback buffers
- buffer writes and readback
- 1D, 2D, and 3D textures
- sampler descriptions
- command-list creation
- buffer copy commands
- shader modules
- graphics pipeline creation
- swapchain render passes
- graphics pipeline binding
- non-indexed draw commands
- presentation recorded on the command list
- submission timeline values
- explicit submission waits and device idle waits
- conservative device capability reporting
- native debug names where supported

VICE Lab first verifies the buffer path with an upload, GPU copy, and readback check. It then creates vertex and fragment shader modules, builds a graphics pipeline, records a render pass, draws a triangle, and presents the result through the same VICE command interface on both backends.

## Backend boundaries

Public rendering headers contain VICE types only. They do not expose Metal objects, Direct3D interfaces, DXGI types, or platform headers.

Metal owns native resources, shader functions, render pipeline states, command encoders, command buffers, and the `CAMetalLayer` used by the swapchain.

Direct3D 12 owns committed resources, root signatures, pipeline state objects, command allocators, graphics command lists, queue submission, fence values, and the DXGI swapchain.

The public API describes intent. Each backend translates that intent into its native resource, pipeline, command, and synchronization model.

## Shader modules

The current source shader path exists to prove the runtime API before the production shader cooker lands.

Metal accepts MSL source and creates a Metal library and function at runtime. Direct3D 12 accepts HLSL source and compiles the development shader before creating the pipeline.

This is not the shipping shader architecture. Issue #16 tracks the production cooker. The intended production path authors HLSL once, compiles it to DXIL with DXC, converts the DXIL to metallib for Metal, and packages both backend artifacts with VICE reflection metadata.

Game code should depend on `ShaderModule` and `GraphicsPipeline`, not compiler details.

## First graphics pipeline

The first pipeline contract supports:

- one vertex shader
- one fragment shader
- one color target
- triangle-list topology
- cull mode
- front-face winding

The first VICE Lab triangle uses vertex IDs to create its three positions directly in the shader. This proves the shader, pipeline, render-pass, draw, and presentation path without forcing vertex-layout or binding decisions into the RHI too early.

Vertex layouts, indexed drawing, material binding, textures, uniforms, and storage resources should be added from real mesh and material requirements.

## Resource memory

Buffers use one of three memory classes:

- `gpu_only` for resources intended to stay on the GPU
- `cpu_to_gpu` for CPU-written upload data
- `gpu_to_cpu` for readback data

The first contract is intentionally strict. CPU-to-GPU buffers cannot be storage buffers or copy destinations. GPU-to-CPU buffers are readback buffers and must be copy destinations.

Textures are GPU-local for now. Texture uploads will use staging buffers and copy commands instead of exposing CPU-mapped texture memory.

## Submission and lifetime

`Device::submit` returns a monotonically increasing submission value. `completed_submission`, `wait_for_submission`, and `wait_idle` define the first synchronization contract.

The current Metal and Direct3D 12 implementations wait for submitted command lists before returning from `submit`. This keeps lifetime rules simple while the frame scheduler and deferred destruction system do not exist yet.

The API does not promise synchronous submission. Higher-level code must treat the returned submission value as the lifetime boundary. Future work can make submission asynchronous without changing that rule.

Resources and pipelines referenced by a command list must stay alive until its submission is complete.

## Capabilities

Capabilities describe what the current VICE backend exposes, not every feature that the physical GPU may support.

Ray tracing and mesh shaders remain false until VICE has working public contracts for them. This avoids advertising hardware features that engine code cannot use yet.

The current macOS and Direct3D 12 backends report a conservative 16,384-pixel maximum 2D texture dimension. Newer hardware may exceed that value.

## Next RHI work

The next rendering slices should add:

- the production shader cooker from issue #16
- texture upload and texture copy commands
- vertex and index buffer binding
- indexed draws
- resource binding tables or argument layouts
- uniform and storage binding
- compute pipelines and dispatch
- explicit resource state ownership above the Direct3D 12 backend
- asynchronous submission and deferred destruction
- multiple frames in flight
- render-graph integration

These APIs should continue to come from real renderer requirements instead of copying the full surface area of Metal or Direct3D 12.


## Runtime meshes without a render material

The native Runtime now shades vertex-colored meshes without an explicit render
material using a built-in diffuse fallback. It reads the same bounded World
light list as the material path: directional lights and range-limited point
lights, with a fixed hemispheric ambient fill. The selected authored directional
light receives the existing four cascaded shadow maps with 3x3 PCF. Point shadows,
specular/metallic response, textures and image-based lighting still require an
explicit render material; this fallback is a prototype presentation path.

Both ordinary instanced draws and streamed virtual-geometry cluster draws bind
the full lighting frame. Depth-only draws retain the original matrix prefix.
Normals use the inverse transpose of the model basis, including nonuniform and
negative scales. The color vertex shader preserves the depth shader's full
world-position calculation before projection: reconstructing a float4 from its
xyz can change rounding enough to reject fragments against a depth prepass.

`vice_runtime_fallback_lighting_tests` compiles the actual native fallback and
depth shaders, renders through the depth prepass, and reads HDR pixels through
compute. It covers directional orientation, inverse-transpose normals, sun
occlusion, point-light range and surviving interior pixels. It runs on the
host's native RHI; a local Metal pass is not evidence of Windows execution.
