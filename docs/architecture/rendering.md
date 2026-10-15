# Rendering

VICE owns its rendering hardware interface. The first backends are Metal on macOS and Direct3D 12 on Windows.

## Current RHI

The RHI now covers the first usable resource path:

- native graphics device creation
- swapchain creation and presentation
- buffer creation with GPU-only, upload, and readback memory classes
- buffer writes and readback
- 1D, 2D, and 3D texture creation
- color, depth, sampled, storage, and copy texture usage flags
- sampler descriptions
- command-list creation
- buffer copy commands
- submission timeline values
- explicit submission waits and device idle waits
- conservative device capability reporting
- debug names passed to native resources where the backend supports them

VICE Lab verifies the buffer path by generating data on the job system, writing an upload buffer, copying it through an RHI command list, reading the result back, and comparing the bytes.

## Backend boundaries

Public rendering headers contain VICE types only. They do not expose Metal objects, Direct3D interfaces, DXGI types, or platform headers.

The Metal backend owns `MTLBuffer`, `MTLTexture`, sampler state, command buffers, and the `CAMetalLayer` used by the swapchain.

The Direct3D 12 backend owns committed resources, command allocators, graphics command lists, queue submission, fence values, and the DXGI swapchain.

The public API describes intent. Each backend translates that intent into its native resource and synchronization model.

## Resource memory

Buffers use one of three memory classes:

- `gpu_only` for resources intended to stay on the GPU
- `cpu_to_gpu` for CPU-written upload data
- `gpu_to_cpu` for readback data

The first contract is intentionally strict. CPU-to-GPU buffers cannot be storage buffers or copy destinations. GPU-to-CPU buffers are readback buffers and must be copy destinations.

Textures are GPU-local in this slice. Texture uploads will use staging buffers and copy commands instead of exposing CPU-mapped texture memory.

## Submission and lifetime

`Device::submit` returns a monotonically increasing submission value. `completed_submission`, `wait_for_submission`, and `wait_idle` define the first synchronization contract.

The current Metal and Direct3D 12 implementations wait for submitted command lists before returning from `submit`. This is conservative and makes resource lifetime unambiguous while the frame scheduler and deferred destruction system do not exist yet.

The API does not promise synchronous submission. Higher-level code must treat the returned submission value as the lifetime boundary. Future work can make submission asynchronous without changing that rule.

Resources referenced by a command list must stay alive until its submission is complete.

## Capabilities

Capabilities describe what the current VICE backend exposes, not every feature that the physical GPU may support.

For example, ray tracing and mesh shaders remain false until VICE has working public contracts for them. This avoids advertising hardware features that engine code cannot use yet.

The current macOS and Direct3D 12 backends report a conservative 16,384-pixel maximum 2D texture dimension. This matches the Metal Mac2 and Metal 3 limits and the Direct3D 12 required 2D texture dimension. Newer hardware may exceed the conservative VICE value.

## Frame synchronization

The Direct3D 12 swapchain bootstrap still waits for the GPU after every presented frame before reusing its command allocator and back buffers.

Metal presentation remains independent from the general RHI submission timeline for now.

Neither path is the final performance model. The renderer will move to multiple frames in flight with per-frame command resources and explicit latency control after the render graph exists.

## Next RHI work

The next slice should add:

- shader modules
- pipeline descriptions and native pipeline objects
- texture upload and copy commands
- render and compute command encoding
- binding tables or argument layouts
- explicit resource state ownership above the Direct3D 12 backend
- asynchronous submission and deferred destruction
- multiple frames in flight
- render-graph integration

These APIs should continue to come from real renderer requirements instead of copying the full surface area of Metal or Direct3D 12.
