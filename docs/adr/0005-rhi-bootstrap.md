# ADR 0005: Native RHI bootstrap

Status: accepted

## Context

VICE needs high-end rendering on macOS and Windows. The engine cannot let Metal or Direct3D 12 types leak into higher-level renderer, editor, or game code.

The platform layer now provides native windows and an opaque native handle escape hatch. The next boundary must prove that both graphics APIs can attach to the same VICE window contract.

## Decision

VICE owns the rendering hardware interface.

The first RHI slice exposes a device, device capabilities, a swapchain, clear color, resize, and presentation. Metal and Direct3D 12 implement that contract directly.

The RHI is not modeled after one backend. New API surface will be added only when at least one higher-level renderer feature needs it and the mapping to both Tier 1 backends is understood.

Metal uses a `CAMetalLayer` attached to the AppKit content view.

Direct3D 12 uses a flip-model DXGI swapchain created from the direct command queue. The bootstrap waits for GPU completion before command allocator reuse.

## Consequences

VICE Lab can display its first GPU-rendered frame through one engine API on either Tier 1 platform.

The renderer is free to grow into a render graph, Forward+, deferred, or hybrid ray-traced architecture without changing platform window code.

The first Direct3D 12 path is intentionally conservative and not representative of final frame throughput. Multi-frame scheduling comes later.

Buffers, textures, pipelines, samplers, command lists, and richer capability queries remain part of issue #6 and are not considered complete by this ADR.
