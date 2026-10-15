# ADR 0007: RHI resource ownership and submission

Status: Accepted

## Context

The first rendering slice proved that VICE can create a native Metal or Direct3D 12 device, attach a swapchain to a VICE window, clear a frame, and present it.

The next renderer work needs resources and commands that higher-level code can use without knowing which native API is active.

Metal and Direct3D 12 do not have identical ownership, memory, binding, or synchronization models. The VICE API needs to preserve useful differences without leaking native types.

## Decision

VICE owns the public resource descriptions and resource interfaces.

Buffers declare:

- byte size
- usage flags
- memory location
- optional debug name

Textures declare:

- dimensionality
- extent
- mip count
- array count
- sample count
- format
- usage flags
- optional debug name

Samplers use a backend-neutral filtering and address description.

Native resources remain private to the backend implementation.

The first command-list contract supports buffer copies. More command types will be added only when the renderer needs them.

Submissions use a monotonic `SubmissionId`. The current backends complete the submitted work before returning. The public contract still treats the submission value as the lifetime boundary so asynchronous execution can replace the bootstrap behavior later.

Resources used by recorded commands must remain alive until the submission that references them has completed.

Capabilities report features VICE can actually expose through its current backend. They are not a raw dump of every hardware feature.

## Why this shape

A small common API keeps renderer code portable without forcing Metal to imitate Direct3D 12 or Direct3D 12 to imitate Metal.

Strict memory classes make upload and readback intent visible. They also keep invalid cross-platform combinations out of higher-level renderer code.

Submission values give us a path to deferred destruction, frame resources, upload retirement, and asynchronous command execution later.

## Consequences

The bootstrap submission path is slower than the final renderer because it waits for completion.

Texture upload is not part of this decision. It will use staging buffers and copy commands.

Pipeline objects and binding models are still open work.

The Direct3D 12 backend tracks the minimal buffer state needed by its current copy path. Resource-state ownership needs a broader design before render and compute passes depend on it.
