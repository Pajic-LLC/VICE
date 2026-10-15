#pragma once

#include <vice/platform/event.hpp>
#include <vice/platform/window.hpp>

#include <filesystem>
#include <memory>

namespace vice::platform {

struct PlatformPaths {
    std::filesystem::path executable;
    std::filesystem::path user_data;
    std::filesystem::path temporary;
};

class PlatformServices {
public:
    virtual ~PlatformServices() = default;

    [[nodiscard]] static std::unique_ptr<PlatformServices> create();

    [[nodiscard]] virtual std::unique_ptr<Window> create_window(const WindowDesc& desc) = 0;
    virtual bool poll_event(Event& event) = 0;
    virtual void request_quit() = 0;

    [[nodiscard]] virtual PlatformPaths paths() const = 0;
};

} // namespace vice::platform
