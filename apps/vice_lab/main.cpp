#include <vice/core/build.hpp>
#include <vice/core/log.hpp>
#include <vice/core/platform.hpp>
#include <vice/core/time.hpp>
#include <vice/core/version.hpp>
#include <vice/platform/event.hpp>
#include <vice/platform/services.hpp>
#include <vice/render/rhi.hpp>
#include <vice/world/world.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <type_traits>
#include <variant>

int main() {
    using namespace vice::core;

    const auto platform = current_platform();
    if (platform == Platform::Unsupported) {
        log(LogLevel::fatal, "This platform is not a VICE development target.");
        return 1;
    }

    log(LogLevel::info, std::string{"VICE "} + std::string{version_string()});
    log(LogLevel::info, std::string{"Platform: "} + std::string{platform_name(platform)});
    log(LogLevel::info, std::string{"Architecture: "} + std::string{architecture_name()});
    log(LogLevel::info, std::string{"Compiler: "} + std::string{compiler_name()});
    log(
        LogLevel::info,
        std::string{"Build: "} + std::string{build_configuration_name(current_build_configuration())}
    );

    auto platform_services = vice::platform::PlatformServices::create();
    if (!platform_services) {
        log(LogLevel::error, "VICE could not initialize platform services.");
        return 1;
    }

    const auto paths = platform_services->paths();
    log(LogLevel::debug, std::string{"Executable: "} + paths.executable.string());
    log(LogLevel::debug, std::string{"User data: "} + paths.user_data.string());

    vice::platform::WindowDesc window_desc;
    window_desc.title = "VICE Lab";
    window_desc.width = 1280;
    window_desc.height = 720;

    auto window = platform_services->create_window(window_desc);
    if (!window) {
        log(LogLevel::error, "VICE Lab could not create a native window.");
        return 1;
    }

    const auto metrics = window->metrics();
    log(
        LogLevel::info,
        std::string{"Window: "} + std::to_string(metrics.width) + "x" +
            std::to_string(metrics.height) + " logical, " +
            std::to_string(metrics.drawable_width) + "x" +
            std::to_string(metrics.drawable_height) + " drawable"
    );

    auto render_device = vice::render::Device::create();
    if (!render_device) {
        log(LogLevel::error, "VICE Lab could not create a render device.");
        return 1;
    }
    log(
        LogLevel::info,
        std::string{"Renderer: "} + std::string{vice::render::backend_name(render_device->backend())}
    );

    auto swapchain = render_device->create_swapchain(*window);
    if (!swapchain) {
        log(LogLevel::error, "VICE Lab could not create a swapchain.");
        return 1;
    }

    vice::world::World world;
    const auto entity = world.create_entity();
    if (const auto persistent_id = world.persistent_id(entity)) {
        log(LogLevel::info, std::string{"World entity: "} + persistent_id->to_string());
    }

    constexpr vice::render::Color clear_color{0.018F, 0.022F, 0.030F, 1.0F};
    FrameClock clock;
    bool running = true;
    while (running) {
        vice::platform::Event event{vice::platform::QuitRequested{}};
        while (platform_services->poll_event(event)) {
            std::visit(
                [&](const auto& payload) {
                    using Payload = std::decay_t<decltype(payload)>;
                    if constexpr (
                        std::is_same_v<Payload, vice::platform::QuitRequested> ||
                        std::is_same_v<Payload, vice::platform::WindowCloseRequested>
                    ) {
                        running = false;
                    } else if constexpr (std::is_same_v<Payload, vice::platform::WindowMetricsChanged>) {
                        swapchain->resize(payload.metrics);
                    } else if constexpr (std::is_same_v<Payload, vice::platform::KeyEvent>) {
                        if (payload.pressed && payload.key == vice::platform::Key::escape) {
                            running = false;
                        }
                    }
                },
                event.payload
            );
        }

        if (running) {
            swapchain->clear_and_present(clear_color);
        }

        clock.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    swapchain.reset();
    render_device.reset();
    window.reset();
    log(LogLevel::info, "VICE Lab closed.");
    return 0;
}
