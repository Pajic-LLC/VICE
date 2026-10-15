# ADR 0004: Platform service boundary

Status: accepted

## Context

VICE needs native macOS and Windows support from the start. Rendering, input, paths, application lifecycle, and editor integration all need operating system services, but those APIs must not spread through the engine.

The renderer will need an AppKit view for Metal and an HWND for Direct3D 12. Game code should never need either type.

## Decision

VICE owns a small `VICE::Platform` module.

The public contract uses VICE-owned window IDs, window metrics, event types, paths, and opaque native handles. Platform-specific implementation files are compiled only on their matching operating system.

The macOS backend uses AppKit. The Windows backend uses Win32. No cross-platform window toolkit is required for the first platform layer.

Native handles are available only through the dedicated `native_window.hpp` escape hatch. Higher-level engine modules should use that header only when direct operating system integration is required.

Window creation sizes are expressed in logical units. VICE also reports drawable pixel dimensions and display scale so rendering and UI code do not guess about DPI.

Input events are queued by the platform backend and consumed through a non-blocking poll API. Event timestamps use the monotonic clock from `VICE::Core`.

## Consequences

Metal and Direct3D 12 can attach directly to native windows without changing the public application loop.

macOS and Windows behavior can differ internally without leaking those differences into gameplay code.

VICE is responsible for maintaining two native backends instead of delegating that work to a window toolkit.

Gamepad support, IME composition, drag and drop, clipboard access, accessibility hooks, and other platform features can be added behind the same boundary when they are needed.
