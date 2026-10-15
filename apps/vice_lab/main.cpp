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

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
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

    const auto render_capabilities = render_device->capabilities();
    log(
        LogLevel::info,
        std::string{"Renderer: "} + std::string{vice::render::backend_name(render_device->backend())}
    );
    log(
        LogLevel::debug,
        std::string{"RHI unified memory: "} + (render_capabilities.unified_memory ? "yes" : "no")
    );

    std::array<std::byte, 64> probe_source{};
    vice::jobs::TaskDesc build_probe;
    build_probe.name = "vice_lab.build_rhi_probe";
    build_probe.function = [&] {
        for (std::size_t index = 0; index < probe_source.size(); ++index) {
            probe_source[index] = std::byte{
                static_cast<unsigned char>((index * 17U + 11U) & 0xFFU)
            };
        }
    };
    const auto probe_job = jobs.schedule(std::move(build_probe));
    if (jobs.wait(probe_job) != vice::jobs::Status::succeeded) {
        log(LogLevel::error, "VICE Lab could not prepare the RHI resource probe.");
        return 1;
    }

    vice::render::BufferDesc upload_desc;
    upload_desc.size = probe_source.size();
    upload_desc.usage = vice::render::BufferUsage::copy_source;
    upload_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
    upload_desc.debug_name = "VICE Lab upload probe";

    vice::render::BufferDesc readback_desc;
    readback_desc.size = probe_source.size();
    readback_desc.usage = vice::render::BufferUsage::copy_destination;
    readback_desc.memory = vice::render::MemoryLocation::gpu_to_cpu;
    readback_desc.debug_name = "VICE Lab readback probe";

    auto upload_buffer = render_device->create_buffer(upload_desc);
    auto readback_buffer = render_device->create_buffer(readback_desc);
    auto command_list = render_device->create_command_list();
    if (!upload_buffer || !readback_buffer || !command_list) {
        log(LogLevel::error, "VICE Lab could not create RHI probe resources.");
        return 1;
    }
    if (!upload_buffer->write(0, std::span<const std::byte>{probe_source})) {
        log(LogLevel::error, "VICE Lab could not write the RHI upload buffer.");
        return 1;
    }
    if (!command_list->copy_buffer(
            *upload_buffer,
            0,
            *readback_buffer,
            0,
            probe_source.size()
        )) {
        log(LogLevel::error, "VICE Lab could not record the RHI buffer copy.");
        return 1;
    }

    const auto probe_submission = render_device->submit(std::move(command_list));
    if (probe_submission == vice::render::invalid_submission ||
        !render_device->wait_for_submission(probe_submission)) {
        log(LogLevel::error, "VICE Lab RHI probe submission failed.");
        return 1;
    }

    std::array<std::byte, 64> probe_result{};
    if (!readback_buffer->read(0, std::span<std::byte>{probe_result}) ||
        !std::equal(probe_source.begin(), probe_source.end(), probe_result.begin())) {
        log(LogLevel::error, "VICE Lab RHI buffer transfer verification failed.");
        return 1;
    }
    log(LogLevel::debug, "VICE Lab RHI buffer transfer verified.");

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

    (void)render_device->wait_idle();
    swapchain.reset();
    readback_buffer.reset();
    upload_buffer.reset();
    render_device.reset();
    window.reset();
    jobs.shutdown();
    log(LogLevel::info, "VICE Lab closed.");
    return 0;
}
