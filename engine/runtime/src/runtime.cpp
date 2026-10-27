#include <vice/runtime/runtime.hpp>
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
#include <vice/jobs/jobs.hpp>
#include <vice/rigid_body/authored_simulation.hpp>
#endif

#include <vice/core/log.hpp>
#include <vice/core/platform.hpp>
#include <vice/core/time.hpp>
#include <vice/platform/services.hpp>
#include <vice/render/telemetry.hpp>
#include <vice/runtime/scene.hpp>
#include <vice/world/snapshot_file.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace vice::runtime {
namespace {

[[nodiscard]] std::filesystem::path absolute_normalized(
    const std::filesystem::path& path,
    std::string& diagnostics
) {
    if (path.empty()) {
        diagnostics = "Path is empty.";
        return {};
    }

    std::error_code error;
    auto resolved = std::filesystem::absolute(path, error);
    if (error) {
        diagnostics = "Could not resolve path: " + error.message();
        return {};
    }
    diagnostics.clear();
    return resolved.lexically_normal();
}

[[nodiscard]] bool same_window(
    const platform::WindowId event_window,
    const platform::WindowId game_window
) noexcept {
    return event_window == game_window;
}

} // namespace

LaunchPaths staged_launch_paths(
    const std::filesystem::path& executable,
    std::string* const diagnostics
) {
    std::string local_diagnostics;
    const auto resolved_executable = absolute_normalized(executable, local_diagnostics);
    if (resolved_executable.empty()) {
        if (diagnostics != nullptr) *diagnostics = std::move(local_diagnostics);
        return {};
    }

    const auto root = resolved_executable.parent_path();
    if (root.empty()) {
        if (diagnostics != nullptr) *diagnostics = "Could not resolve runtime output directory.";
        return {};
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return LaunchPaths{
        .manifest = root / "vice.runtime.viceproj",
        .cooked_cache_root = root / "content",
    };
}

LaunchPaths explicit_launch_paths(
    const std::filesystem::path& manifest,
    const std::filesystem::path& cooked_cache_root,
    std::string* const diagnostics
) {
    std::string local_diagnostics;
    const auto resolved_manifest = absolute_normalized(manifest, local_diagnostics);
    if (resolved_manifest.empty()) {
        if (diagnostics != nullptr) *diagnostics = std::move(local_diagnostics);
        return {};
    }
    const auto resolved_cache = absolute_normalized(cooked_cache_root, local_diagnostics);
    if (resolved_cache.empty()) {
        if (diagnostics != nullptr) *diagnostics = std::move(local_diagnostics);
        return {};
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return LaunchPaths{
        .manifest = resolved_manifest,
        .cooked_cache_root = resolved_cache,
    };
}

LoadResult load_project(const LaunchPaths& launch) {
    LoadResult result;
    if (launch.manifest.empty()) {
        result.diagnostics = "Runtime project manifest path is empty.";
        return result;
    }
    if (launch.cooked_cache_root.empty()) {
        result.diagnostics = "Runtime cooked-content root is empty.";
        return result;
    }

    const auto loaded_manifest = project::load_manifest(launch.manifest);
    if (!loaded_manifest.manifest.has_value()) {
        result.diagnostics = "Could not open runtime project: " + loaded_manifest.diagnostics;
        return result;
    }

    const auto startup_world = project::resolve_cooked_startup_world(
        launch.cooked_cache_root,
        *loaded_manifest.manifest
    );
    if (startup_world.empty()) {
        result.diagnostics = "Could not resolve the cooked startup World for the runtime project.";
        return result;
    }

    const auto loaded_world = world::load_world_snapshot(startup_world);
    if (!loaded_world.snapshot.has_value()) {
        result.diagnostics =
            "Could not load cooked startup World `" + startup_world.generic_string() + "`: " +
            loaded_world.diagnostics;
        return result;
    }

    auto runtime_world = std::make_unique<world::World>();
    if (!runtime_world->restore(*loaded_world.snapshot)) {
        result.diagnostics =
            "Could not restore cooked startup World `" + startup_world.generic_string() + "`.";
        return result;
    }

    result.project = LoadedProject{
        .manifest = *loaded_manifest.manifest,
        .manifest_path = launch.manifest,
        .cooked_content_root = project::resolve_cooked_root(
            launch.cooked_cache_root,
            *loaded_manifest.manifest
        ),
        .startup_world_path = startup_world,
        .world = std::move(runtime_world),
    };
    return result;
}

GameModuleLifecycle::GameModuleLifecycle(GameModule& module, GameContext context) noexcept
    : module_{&module}, context_{context} {}

GameModuleLifecycle::~GameModuleLifecycle() {
    stop();
}

bool GameModuleLifecycle::start() {
    if (started_ || stopped_ || module_ == nullptr) return false;
    if (!context_.world.begin_causal_recording()) return false;
    if (!module_->on_start(context_)) return false;
    started_ = true;
    return true;
}

void GameModuleLifecycle::event(const platform::Event& event) {
    if (!started_ || stopped_ || module_ == nullptr) return;
    module_->on_event(context_, event);
}

void GameModuleLifecycle::update(const double delta_seconds) {
    if (!started_ || stopped_ || module_ == nullptr) return;
    module_->on_update(context_, delta_seconds);
}

void GameModuleLifecycle::frame(render::Color& clear_color) {
    if (!started_ || stopped_ || module_ == nullptr) return;
    module_->on_frame(context_, clear_color);
}

void GameModuleLifecycle::stop() {
    if (stopped_) return;
    if (started_ && module_ != nullptr) module_->on_stop(context_);
    stopped_ = true;
}

EventDecision classify_event(
    const platform::Event& event,
    const platform::WindowId game_window
) noexcept {
    EventDecision decision;
    std::visit(
        [&](const auto& payload) {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, platform::QuitRequested>) {
                decision.quit = true;
            } else if constexpr (std::is_same_v<Payload, platform::WindowCloseRequested>) {
                decision.quit = same_window(payload.window, game_window);
            } else if constexpr (std::is_same_v<Payload, platform::WindowMetricsChanged>) {
                if (same_window(payload.window, game_window)) decision.resize = payload.metrics;
            } else if constexpr (std::is_same_v<Payload, platform::KeyEvent>) {
                decision.quit = same_window(payload.window, game_window) && payload.pressed &&
                    payload.key == platform::Key::escape;
            }
        },
        event.payload
    );
    return decision;
}

int run_native_game(
    LoadedProject& runtime_project,
    GameModule& module,
    const ApplicationConfig& config,
    RuntimeObserver* const observer
) {
    using core::LogLevel;
    using core::log;

    if (runtime_project.world == nullptr) {
        log(LogLevel::error, "VICE Runtime has no authoritative World.");
        return 1;
    }
    const bool has_authored_bodies = [&] {
        const auto snapshot = runtime_project.world->snapshot();
        return std::any_of(snapshot.entities.begin(), snapshot.entities.end(), [](const auto& entity) { return entity.rigid_box.has_value() || entity.character.has_value(); });
    }();
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    std::unique_ptr<jobs::Scheduler> physics_jobs;
    std::unique_ptr<rigid_body::AuthoredSimulation> physics;
    if (has_authored_bodies) {
        physics_jobs = std::make_unique<jobs::Scheduler>();
        physics = std::make_unique<rigid_body::AuthoredSimulation>();
        std::string physics_diagnostics;
        if (!physics->reset(*runtime_project.world, *physics_jobs, &physics_diagnostics)) {
            log(LogLevel::error, "Could not initialize authored physics: " + physics_diagnostics);
            return 1;
        }
    }
#else
    if (has_authored_bodies) {
        log(LogLevel::error, "This Runtime was built without the optional authored Rigid3D capability.");
        return 1;
    }
#endif
    if (core::current_platform() == core::Platform::Unsupported) {
        log(LogLevel::fatal, "This platform is not a VICE runtime target.");
        return 1;
    }

    auto platform_services = platform::PlatformServices::create();
    if (!platform_services) {
        log(LogLevel::error, "VICE Runtime could not initialize platform services.");
        return 1;
    }

    platform::WindowDesc window_desc;
    window_desc.title = runtime_project.manifest.game_name;
    window_desc.width = config.width;
    window_desc.height = config.height;
    window_desc.resizable = config.resizable;
    auto window = platform_services->create_window(window_desc);
    if (!window) {
        log(LogLevel::error, "VICE Runtime could not create the game window.");
        return 1;
    }

    auto base_render_device = render::Device::create();
    if (!base_render_device) {
        log(LogLevel::error, "VICE Runtime could not create a render device.");
        return 1;
    }
    auto render_device = render::instrument_device(std::move(base_render_device));
    if (!render_device || !render_device->valid()) {
        log(LogLevel::error, "VICE Runtime could not instrument the render device.");
        return 1;
    }
    auto swapchain = render_device->create_swapchain(*window);
    if (!swapchain) {
        log(LogLevel::error, "VICE Runtime could not create the game swapchain.");
        return 1;
    }

    RuntimeSceneRenderer scene_renderer;
    std::string scene_diagnostics;
    if (!scene_renderer.initialize(
            *render_device,
            swapchain->color_format(),
            runtime_project.cooked_content_root,
            &scene_diagnostics
        )) {
        log(LogLevel::error, "VICE Runtime could not initialize the scene: " + scene_diagnostics);
        return 1;
    }
    if (!scene_renderer.set_exposure(config.exposure, &scene_diagnostics)) {
        log(LogLevel::error, "VICE Runtime could not configure exposure: " + scene_diagnostics);
        return 1;
    }

    log(
        LogLevel::info,
        "Game: " + runtime_project.manifest.game_name +
            " | Target: " + runtime_project.manifest.game_target
    );
    log(LogLevel::info, "Render backend: " + std::string{render::backend_name(render_device->backend())});
    log(LogLevel::info, "Startup World: " + runtime_project.startup_world_path.generic_string());
    log(
        LogLevel::info,
        "World: " + std::to_string(runtime_project.world->entity_count()) + " entities, " +
            std::to_string(runtime_project.world->resource_count()) + " resources"
    );

    GameContext context{
        .project = runtime_project.manifest,
        .world = *runtime_project.world,
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
        .physics = physics.get(),
#endif
    };
    GameModuleLifecycle lifecycle{module, context};
    if (!lifecycle.start()) {
        log(LogLevel::error, "VICE Runtime game module rejected startup.");
        return 1;
    }

    bool running = true;
    auto metrics = window->metrics();
    bool drawable = metrics.drawable_width > 0U && metrics.drawable_height > 0U;
    if (drawable && !scene_renderer.resize(metrics, &scene_diagnostics)) {
        log(LogLevel::error, "VICE Runtime could not size the scene: " + scene_diagnostics);
        return 1;
    }

    std::uint64_t frame_index = 0U;
    double last_frame_seconds = 0.0;
    double last_update_seconds = 0.0;
    double last_render_seconds = 0.0;
    const auto observe = [&](
        const RuntimeExecutionState execution_state,
        const double frame_seconds,
        const double update_seconds,
        const double render_seconds
    ) {
        if (observer == nullptr) return;
        observer->on_runtime_observation(RuntimeFrameObservation{
            .execution_state = execution_state,
            .frame_index = frame_index,
            .frame_seconds = frame_seconds,
            .update_seconds = update_seconds,
            .render_seconds = render_seconds,
            .backend = render_device->backend(),
            .device_capabilities = render_device->capabilities(),
            .device_stats = render_device->stats(),
            .metrics = metrics,
            .scene_renderer = &scene_renderer,
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
            .physics = physics.get(),
#endif
        });
    };
    observe(RuntimeExecutionState::starting, 0.0, 0.0, 0.0);

    core::FrameClock clock;
    int exit_code = 0;

    while (running) {
        platform::Event event{platform::QuitRequested{}};
        while (platform_services->poll_event(event)) {
            lifecycle.event(event);
            const auto decision = classify_event(event, window->id());
            if (decision.quit) running = false;
            if (decision.resize.has_value()) {
                metrics = *decision.resize;
                drawable = metrics.drawable_width > 0U && metrics.drawable_height > 0U;
                if (drawable && (!render_device->wait_idle() || !swapchain->resize(metrics) ||
                    !scene_renderer.resize(metrics, &scene_diagnostics))) {
                    log(
                        LogLevel::error,
                        scene_diagnostics.empty()
                            ? "VICE Runtime could not resize the game scene."
                            : "VICE Runtime could not resize the game scene: " + scene_diagnostics
                    );
                    running = false;
                    exit_code = 1;
                }
            }
        }
        if (!running) break;

        clock.tick();
        const double delta_seconds = std::min(clock.delta_seconds(), 0.25);
        last_frame_seconds = delta_seconds;

        const auto update_begin = std::chrono::steady_clock::now();
        lifecycle.update(delta_seconds);
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
        if (physics) {
            std::string physics_diagnostics;
            if (!physics->advance(delta_seconds, &physics_diagnostics)) {
                log(LogLevel::error, "Authored physics update failed: " + physics_diagnostics);
                running = false;
                exit_code = 1;
                break;
            }
        }
#endif
        render::Color clear_color = config.clear_color;
        lifecycle.frame(clear_color);
        const auto render_begin = std::chrono::steady_clock::now();
        last_update_seconds = std::chrono::duration<double>(render_begin - update_begin).count();

        if (drawable && !scene_renderer.render(
                *runtime_project.world,
                *swapchain,
                metrics,
                clear_color,
                &scene_diagnostics
            )) {
            log(LogLevel::error, "VICE Runtime could not render the game frame: " + scene_diagnostics);
            running = false;
            exit_code = 1;
        }
        const auto render_end = std::chrono::steady_clock::now();
        last_render_seconds = std::chrono::duration<double>(render_end - render_begin).count();

        ++frame_index;
        observe(
            RuntimeExecutionState::running,
            delta_seconds,
            last_update_seconds,
            last_render_seconds
        );
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    observe(
        RuntimeExecutionState::stopping,
        last_frame_seconds,
        last_update_seconds,
        last_render_seconds
    );
    (void)render_device->wait_idle();
    lifecycle.stop();
    return exit_code;
}

} // namespace vice::runtime
