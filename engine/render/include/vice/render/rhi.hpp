#pragma once

#include <vice/platform/types.hpp>
#include <vice/platform/window.hpp>

#include <cstdint>
#include <memory>
#include <string_view>

namespace vice::render {

enum class Backend {
    metal,
    direct3d12,
};

[[nodiscard]] constexpr std::string_view backend_name(const Backend backend) noexcept {
    switch (backend) {
    case Backend::metal: return "Metal";
    case Backend::direct3d12: return "Direct3D 12";
    }
    return "Unknown";
}

struct Color {
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    float alpha = 1.0F;
};

struct DeviceCapabilities {
    bool ray_tracing = false;
    bool mesh_shaders = false;
    std::uint32_t recommended_frames_in_flight = 2;
};

class Swapchain {
public:
    virtual ~Swapchain() = default;

    [[nodiscard]] virtual platform::WindowId window_id() const noexcept = 0;
    virtual bool resize(platform::WindowMetrics metrics) = 0;
    virtual bool clear_and_present(Color color) = 0;
};

class Device {
public:
    virtual ~Device() = default;

    [[nodiscard]] static std::unique_ptr<Device> create();

    [[nodiscard]] virtual Backend backend() const noexcept = 0;
    [[nodiscard]] virtual DeviceCapabilities capabilities() const noexcept = 0;
    [[nodiscard]] virtual std::unique_ptr<Swapchain> create_swapchain(platform::Window& window) = 0;
};

} // namespace vice::render
