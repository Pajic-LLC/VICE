#pragma once

#include <vice/platform/window.hpp>

namespace vice::platform {

enum class NativeWindowSystem {
    unknown,
    cocoa,
    win32,
};

struct NativeWindowHandle {
    NativeWindowSystem system = NativeWindowSystem::unknown;
    void* window = nullptr;
    void* view = nullptr;
};

class NativeWindowAccess {
public:
    [[nodiscard]] static NativeWindowHandle get(const Window& window) noexcept {
        return window.native_handle();
    }
};

} // namespace vice::platform
