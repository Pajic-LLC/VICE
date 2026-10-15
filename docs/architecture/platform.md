# Platform services

VICE treats macOS and Windows as first-class development targets. Engine modules use one platform contract and do not include AppKit, Cocoa, Win32, or other operating system headers.

## Ownership

`PlatformServices` owns process-level operating system integration. A platform service must outlive every window it creates. Window objects own their native window lifetime.

The first platform contract covers:

- application event pumping
- native window creation
- keyboard and mouse events
- UTF-32 text input events
- window focus and size changes
- executable, user data, and temporary paths
- explicit quit requests

The engine clock remains in `VICE::Core`. Platform events use that monotonic clock when VICE receives them.

## Window units

Window descriptions use logical units. `WindowMetrics` reports both logical and drawable sizes.

On a Retina Mac, a 1280 by 720 logical window can have a 2560 by 1440 drawable surface. On Windows, the backend enables per-monitor DPI awareness and performs the same separation.

Rendering code should use drawable dimensions for swapchain and render-target allocation. UI layout should normally use logical dimensions.

## Native handles

Normal engine and game code should not use native window handles. Renderer integration sometimes needs them, so `vice/platform/native_window.hpp` provides a narrow escape hatch.

The handle contains a platform tag and opaque pointers. It does not expose AppKit or Win32 types in public VICE headers.

## Input

Keyboard events carry a VICE key value and the original native code. The VICE key value is portable. The native code exists for diagnostics and advanced input handling.

Text input is separate from physical key input. The first implementation emits Unicode code points for ordinary text. Full IME composition is not part of this slice and needs its own contract before editor text fields depend on it.

Mouse coordinates use the top-left of the content area as the origin. Coordinates are logical units. Scroll events state whether their delta is expressed as lines or pixels.

## Threading

The platform service and its windows are owned and pumped from one thread. On macOS that thread must be the process main thread because AppKit requires it.

The platform layer does not create a hidden worker thread. The future VICE job system stays separate from operating system event ownership.

## Current backends

macOS uses AppKit directly.

Windows uses Win32 directly and enables per-monitor DPI awareness where the operating system allows it.

This layer does not depend on SDL or another window toolkit. That keeps renderer attachment, DPI behavior, lifecycle, and native integration under VICE control.
