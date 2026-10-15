#pragma once

#include <vice/platform/types.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace vice::platform {

struct NativeWindowHandle;
class NativeWindowAccess;

struct WindowDesc {
    std::string title = "VICE";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool resizable = true;
    bool visible = true;
};

class Window {
public:
    virtual ~Window() = default;

    [[nodiscard]] virtual WindowId id() const noexcept = 0;
    [[nodiscard]] virtual WindowMetrics metrics() const noexcept = 0;
    [[nodiscard]] virtual bool visible() const noexcept = 0;

    virtual void set_title(std::string_view title) = 0;
    virtual void show() = 0;
    virtual void hide() = 0;

private:
    friend class NativeWindowAccess;
    [[nodiscard]] virtual NativeWindowHandle native_handle() const noexcept = 0;
};

} // namespace vice::platform
