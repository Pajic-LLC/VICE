# Rendering

VICE owns its rendering hardware interface. The first backends are Metal on macOS and Direct3D 12 on Windows.

## First vertical slice

The current RHI is intentionally small. It can:

- create the native graphics device
- create the primary graphics command queue
- attach a swapchain to a VICE window
- track logical window changes through drawable size updates
- clear a drawable or back buffer
- present the frame

This proves the platform to renderer boundary before resource APIs and render-graph design are locked.

## Backend boundaries

Public rendering headers contain VICE types only. They do not expose Metal objects, Direct3D interfaces, DXGI types, or platform headers.

The Metal backend receives an opaque AppKit view through `NativeWindowAccess`. It installs a `CAMetalLayer`, gets a drawable only when a frame is ready to be encoded, clears it through a Metal render pass, registers presentation, and commits the command buffer.

The Direct3D 12 backend receives an opaque HWND. It creates a flip-model DXGI swapchain from the direct command queue, creates render target views for the back buffers, records resource transitions and the clear command, presents, and uses a fence before reusing command resources.

## Frame synchronization

The Direct3D 12 bootstrap waits for the GPU after every frame. That is deliberately conservative and keeps command allocator reuse correct while the frame scheduler does not exist yet.

This is not the final performance model. The renderer will move to multiple frames in flight with per-frame resources and explicit latency control once the job system and frame graph exist.

Metal relies on the drawable and command buffer lifetime for this first slice. Its final frame pacing will be designed alongside the same frame scheduler.

## Next RHI work

The next RHI contracts need buffers, textures, samplers, pipelines, fences, command lists, descriptor or argument binding, capability queries, debug naming, and resource lifetime rules.

Those APIs should be added from real renderer requirements instead of copying the complete surface area of either native API.
