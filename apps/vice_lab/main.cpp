#include <vice/core/build.hpp>
#include <vice/core/log.hpp>
#include <vice/core/platform.hpp>
#include <vice/core/time.hpp>
#include <vice/core/version.hpp>
#include <vice/jobs/jobs.hpp>
#include <vice/platform/event.hpp>
#include <vice/platform/services.hpp>
#include <vice/render/rhi.hpp>
#include <vice/world/world.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <span>
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

    vice::jobs::Scheduler jobs;
    log(LogLevel::info, std::string{"Job workers: "} + std::to_string(jobs.worker_count()));

    std::atomic<bool> background_ready{false};
    vice::jobs::TaskDesc background_startup;
    background_startup.name = "vice_lab.background_startup";
    background_startup.priority = vice::jobs::Priority::high;
    background_startup.function = [&] {
        background_ready.store(true, std::memory_order_release);
    };
    const auto background_job = jobs.schedule(std::move(background_startup));

    vice::jobs::TaskDesc main_startup;
    main_startup.name = "vice_lab.main_startup";
    main_startup.lane = vice::jobs::Lane::main_thread;
    main_startup.function = [&] {
        if (background_ready.load(std::memory_order_acquire)) {
            log(LogLevel::debug, "VICE Lab job dependency completed.");
        }
    };
    const std::array startup_dependencies{background_job};
    const auto main_job = jobs.schedule(
        std::move(main_startup),
        std::span<const vice::jobs::TaskHandle>{startup_dependencies}
    );
    if (jobs.wait(main_job) != vice::jobs::Status::succeeded) {
        log(LogLevel::error, "VICE Lab job system startup check failed.");
        return 1;
    }

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
                        (void)swapchain->resize(payload.metrics);
                    } else if constexpr (std::is_same_v<Payload, vice::platform::KeyEvent>) {
                        if (payload.pressed && payload.key == vice::platform::Key::escape) {
                            running = false;
                        }
                    }
                },
                event.payload
            );
        }

        (void)jobs.pump_main(64);

        if (running) {
            (void)swapchain->clear_and_present(clear_color);
        }

        clock.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    swapchain.reset();
    render_device.reset();
    window.reset();
    jobs.shutdown();
    log(LogLevel::info, "VICE Lab closed.");
    return 0;
}
