#include "physics_debug_renderer.hpp"
#include "resource_mesh_renderer.hpp"
#include "scene_renderer.hpp"

#include <vice/core/build.hpp>
#include <vice/core/log.hpp>
#include <vice/core/platform.hpp>
#include <vice/core/time.hpp>
#include <vice/core/version.hpp>
#include <vice/editor/asset_index.hpp>
#include <vice/editor/physics_debug_ui.hpp>
#include <vice/editor/preview_runtime.hpp>
#include <vice/editor/profiler.hpp>
#include <vice/editor/profiler_ui.hpp>
#include <vice/editor/resource_browser_ui.hpp>
#include <vice/editor/state.hpp>
#include <vice/editor/ui.hpp>
#include <vice/editor/ui_renderer.hpp>
#include <vice/jobs/jobs.hpp>
#include <vice/platform/event.hpp>
#include <vice/platform/services.hpp>
#include <vice/project/project.hpp>
#include <vice/render/rhi.hpp>
#include <vice/render/telemetry.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>

namespace {

constexpr std::string_view player_id_text = "10000000-0000-4000-8000-000000000001";
constexpr std::string_view cube_id_text = "10000000-0000-4000-8000-000000000002";

[[nodiscard]] std::unique_ptr<vice::render::Texture> create_depth_target(
    vice::render::Device& device,
    const vice::platform::WindowMetrics metrics
) {
    if (metrics.drawable_width == 0U || metrics.drawable_height == 0U) return nullptr;
    vice::render::TextureDesc desc;
    desc.dimension = vice::render::TextureDimension::d2;
    desc.width = metrics.drawable_width;
    desc.height = metrics.drawable_height;
    desc.depth = 1;
    desc.mip_levels = 1;
    desc.array_layers = 1;
    desc.sample_count = 1;
    desc.format = vice::render::TextureFormat::depth32_float;
    desc.usage = vice::render::TextureUsage::depth_attachment;
    desc.debug_name = "VICE Editor depth";
    return device.create_texture(desc);
}

[[nodiscard]] bool build_fallback_world(vice::world::World& world) {
    vice::world::RegionDesc region_a;
    region_a.id = vice::world::RegionId{1};
    region_a.simulation_island = vice::world::SimulationIslandId{101};
    region_a.origin = {-8.0, 0.0, 0.0};
    region_a.streaming_cell = {0, 0, 0};
    region_a.fidelity = vice::world::RegionFidelity::full;
    region_a.bounds = vice::world::RegionBounds{
        .min = {0.0F, -32.0F, -32.0F},
        .max = {8.0F, 32.0F, 32.0F},
    };

    vice::world::RegionDesc region_b;
    region_b.id = vice::world::RegionId{2};
    region_b.simulation_island = vice::world::SimulationIslandId{202};
    region_b.origin = {0.0, 0.0, 0.0};
    region_b.streaming_cell = {1, 0, 0};
    region_b.fidelity = vice::world::RegionFidelity::full;
    region_b.bounds = vice::world::RegionBounds{
        .min = {0.0F, -32.0F, -32.0F},
        .max = {8.0F, 32.0F, 32.0F},
    };

    if (!world.register_region(region_a) || !world.register_region(region_b)) return false;

    const auto player_id = vice::world::PersistentId::parse(player_id_text);
    const auto cube_id = vice::world::PersistentId::parse(cube_id_text);
    if (!player_id.has_value() || !cube_id.has_value()) return false;
    const auto player = world.create_entity(*player_id);
    const auto cube = world.create_entity(*cube_id);
    if (!player.has_value() || !cube.has_value()) return false;
    return world.set_spatial_state(
        *player,
        vice::world::SpatialState{
            .global_position = {-4.0, 0.0, 0.0},
            .region = vice::world::RegionId{1},
        }
    ) && world.set_spatial_state(
        *cube,
        vice::world::SpatialState{
            .global_position = {-2.5, 1.1, -2.0},
            .region = vice::world::RegionId{1},
        }
    );
}

[[nodiscard]] std::filesystem::path editor_default_project_path() {
#if defined(VICE_EDITOR_DEFAULT_PROJECT)
    return std::filesystem::path{VICE_EDITOR_DEFAULT_PROJECT};
#else
    return {};
#endif
}

[[nodiscard]] std::filesystem::path editor_asset_cooked_root() {
#if defined(VICE_EDITOR_ASSET_COOKED_ROOT)
    return std::filesystem::path{VICE_EDITOR_ASSET_COOKED_ROOT};
#else
    return {};
#endif
}

[[nodiscard]] std::filesystem::path editor_state_root() {
#if defined(VICE_EDITOR_STATE_ROOT)
    return std::filesystem::path{VICE_EDITOR_STATE_ROOT};
#else
    return {};
#endif
}

[[nodiscard]] std::filesystem::path editor_legacy_save_path() {
#if defined(VICE_EDITOR_LEGACY_SAVE_FILE)
    return std::filesystem::path{VICE_EDITOR_LEGACY_SAVE_FILE};
#else
    return {};
#endif
}

[[nodiscard]] std::filesystem::path editor_layout_path() {
#if defined(VICE_EDITOR_LAYOUT_FILE)
    return std::filesystem::path{VICE_EDITOR_LAYOUT_FILE};
#else
    return {};
#endif
}

[[nodiscard]] std::filesystem::path project_save_path(const vice::project::Manifest& project) {
    const auto root = editor_state_root();
    if (root.empty() || !project.id) return {};
    return root / project.id.to_string() / "world.vworldc";
}

[[nodiscard]] std::filesystem::path project_cooked_startup_world(
    const vice::project::Manifest& project
) {
    auto relative = project.startup_world;
    relative.replace_extension(".vworldc");
    const auto cooked_root = editor_asset_cooked_root();
    return cooked_root.empty() ? std::filesystem::path{} : (cooked_root / relative).lexically_normal();
}

[[nodiscard]] bool restore_world_from_file(
    vice::world::World& world,
    const std::filesystem::path& path,
    const std::string_view label
) {
    const auto loaded = vice::world::load_world_snapshot(path);
    if (!loaded.snapshot.has_value() || !world.restore(*loaded.snapshot)) {
        vice::core::log(
            vice::core::LogLevel::error,
            "VICE Editor could not load " + std::string{label} + ": " + loaded.diagnostics
        );
        return false;
    }
    return true;
}

[[nodiscard]] bool load_editor_world(
    vice::world::World& world,
    const std::filesystem::path& save_path,
    const std::filesystem::path& legacy_save_path,
    const std::filesystem::path& cooked_startup_world,
    const bool allow_development_fallback
) {
    const auto try_saved_world = [&](const std::filesystem::path& path, const bool legacy) -> std::optional<bool> {
        if (path.empty()) return std::nullopt;
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (error) {
            vice::core::log(
                vice::core::LogLevel::error,
                "VICE Editor could not inspect its saved World: " + error.message()
            );
            return false;
        }
        if (!exists) return std::nullopt;
        if (!restore_world_from_file(world, path, legacy ? "the legacy saved World" : "the saved World")) {
            return false;
        }
        vice::core::log(
            vice::core::LogLevel::info,
            legacy
                ? "VICE Editor restored the legacy saved World. The next save will scope it to this project."
                : "VICE Editor restored the project World."
        );
        return true;
    };

    if (const auto saved = try_saved_world(save_path, false); saved.has_value()) return *saved;
    if (const auto legacy = try_saved_world(legacy_save_path, true); legacy.has_value()) return *legacy;

    if (!cooked_startup_world.empty()) {
        std::error_code error;
        const bool exists = std::filesystem::exists(cooked_startup_world, error);
        if (error) {
            vice::core::log(
                vice::core::LogLevel::error,
                "VICE Editor could not inspect the cooked startup World: " + error.message()
            );
            return false;
        }
        if (exists) {
            if (!restore_world_from_file(world, cooked_startup_world, "the project startup World")) return false;
            vice::core::log(vice::core::LogLevel::info, "VICE Editor loaded the project startup World.");
            return true;
        }
    }

    if (allow_development_fallback) {
        if (!build_fallback_world(world)) {
            vice::core::log(vice::core::LogLevel::error, "VICE Editor could not create its fallback World.");
            return false;
        }
        vice::core::log(
            vice::core::LogLevel::warning,
            "VICE Editor is using the development World fallback because the startup World is not cooked."
        );
        return true;
    }

    vice::core::log(
        vice::core::LogLevel::error,
        "VICE Editor could not open the project because its startup World is not available in the cooked asset cache."
    );
    return false;
}

[[nodiscard]] bool route_event_to_scene(
    const vice::platform::Event& event,
    const vice::editor::SceneViewport& viewport
) {
    return std::visit(
        [&](const auto& payload) {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, vice::platform::MouseButtonEvent>) {
                return !payload.pressed || viewport.hovered;
            }
            return true;
        },
        event.payload
    );
}

} // namespace

int main(const int argc, char** argv) {
    using vice::core::LogLevel;
    using vice::core::log;

    const auto platform = vice::core::current_platform();
    if (platform == vice::core::Platform::Unsupported) {
        log(LogLevel::fatal, "This platform is not a VICE development target.");
        return 1;
    }

    vice::editor::ConsoleHistory console{2000};
    if (!console.attach()) {
        log(LogLevel::warning, "VICE Editor console could not attach to Core logging.");
    }

    log(LogLevel::info, std::string{"VICE Editor · VICE "} + std::string{vice::core::version_string()});
    log(LogLevel::info, std::string{"Platform: "} + std::string{vice::core::platform_name(platform)});

    if (argc > 2) {
        log(LogLevel::error, "VICE Editor accepts at most one project manifest path.");
        return 1;
    }

    const bool using_default_project = argc < 2;
    std::filesystem::path project_path = using_default_project
        ? editor_default_project_path()
        : std::filesystem::path{argv[1]};
    if (project_path.empty()) {
        log(LogLevel::error, "VICE Editor has no project manifest to open.");
        return 1;
    }

    std::error_code project_path_error;
    project_path = std::filesystem::absolute(project_path, project_path_error).lexically_normal();
    if (project_path_error) {
        log(LogLevel::error, "VICE Editor could not resolve the project path: " + project_path_error.message());
        return 1;
    }

    const auto loaded_project = vice::project::load_manifest(project_path);
    if (!loaded_project.manifest.has_value()) {
        log(LogLevel::error, "VICE Editor could not open the project: " + loaded_project.diagnostics);
        return 1;
    }
    const vice::project::Manifest project = *loaded_project.manifest;
    const std::filesystem::path asset_source_root = vice::project::resolve_asset_root(project_path, project);
    const std::filesystem::path startup_world_source = vice::project::resolve_startup_world(project_path, project);
    if (asset_source_root.empty() || startup_world_source.empty()) {
        log(LogLevel::error, "VICE Editor could not resolve the project asset paths.");
        return 1;
    }

    std::error_code asset_error;
    if (!std::filesystem::is_directory(asset_source_root, asset_error) || asset_error) {
        log(
            LogLevel::error,
            asset_error
                ? "VICE Editor could not inspect the project asset root: " + asset_error.message()
                : "VICE Editor project asset root does not exist: " + asset_source_root.generic_string()
        );
        return 1;
    }
    asset_error.clear();
    if (!std::filesystem::is_regular_file(startup_world_source, asset_error) || asset_error) {
        log(
            LogLevel::error,
            asset_error
                ? "VICE Editor could not inspect the project startup World: " + asset_error.message()
                : "VICE Editor project startup World does not exist: " + startup_world_source.generic_string()
        );
        return 1;
    }

    const std::filesystem::path save_path = project_save_path(project);
    if (!save_path.empty()) {
        std::error_code save_directory_error;
        std::filesystem::create_directories(save_path.parent_path(), save_directory_error);
        if (save_directory_error) {
            log(LogLevel::error, "VICE Editor could not create the project state directory: " + save_directory_error.message());
            return 1;
        }
    }

    log(LogLevel::info, "Project: " + project.name + " · " + project_path.generic_string());

    vice::jobs::Scheduler jobs;
    auto platform_services = vice::platform::PlatformServices::create();
    if (!platform_services) {
        log(LogLevel::error, "VICE Editor could not initialize platform services.");
        return 1;
    }

    vice::platform::WindowDesc window_desc;
    window_desc.title = "VICE Editor · " + project.name;
    window_desc.width = 1600;
    window_desc.height = 980;
    auto window = platform_services->create_window(window_desc);
    if (!window) {
        log(LogLevel::error, "VICE Editor could not create its native window.");
        return 1;
    }

    auto render_device = vice::render::instrument_device(vice::render::Device::create());
    if (!render_device || !render_device->valid()) {
        log(LogLevel::error, "VICE Editor could not create a render device.");
        return 1;
    }
    auto swapchain = render_device->create_swapchain(*window);
    auto depth_target = create_depth_target(*render_device, window->metrics());
    if (!swapchain || !depth_target) {
        log(LogLevel::error, "VICE Editor could not create render targets.");
        return 1;
    }

    vice::editor::AssetIndex assets;
    if (!assets.refresh(asset_source_root, editor_asset_cooked_root())) {
        log(LogLevel::warning, "VICE Editor asset index: " + assets.diagnostics());
    }

    vice::world::World world;
    if (!load_editor_world(
            world,
            save_path,
            using_default_project ? editor_legacy_save_path() : std::filesystem::path{},
            project_cooked_startup_world(project),
            using_default_project
        )) {
        return 1;
    }

    vice::editor::SelectionModel selection;
    if (const auto cube_id = vice::world::PersistentId::parse(cube_id_text); cube_id.has_value() &&
        world.find(*cube_id).has_value()) {
        selection.select(*cube_id);
    }
    vice::editor::TransformHistory history;
    history.mark_saved();

    const std::filesystem::path layout_path = editor_layout_path();
    if (!layout_path.empty()) {
        std::error_code error;
        std::filesystem::create_directories(layout_path.parent_path(), error);
        if (error) {
            log(LogLevel::warning, "VICE Editor could not create its layout directory: " + error.message());
        }
    }

    vice::editor::EditorUi ui{layout_path};
    vice::editor::PhysicsDebugUi physics_debug_ui;
    vice::editor::PreviewRuntime preview;
    vice::editor::PreviewSnapshot preview_snapshot;
    vice::editor::ProfilerUi profiler_ui;
    vice::editor::ProfilerHistory profiler_history{180U};
    vice::editor::ResourceBrowserUi resource_browser;
    vice::editor::UiRhiRenderer ui_renderer;
    vice::editor::detail::SceneRenderer scene_renderer;
    vice::editor::detail::ResourceMeshRenderer resource_mesh_renderer;
    vice::editor::detail::PhysicsDebugRenderer physics_debug_renderer;
    if (!ui.valid() ||
        !ui_renderer.initialize(*render_device, swapchain->color_format()) ||
        !scene_renderer.initialize(*render_device, swapchain->color_format(), {}) ||
        !resource_mesh_renderer.initialize(*render_device, swapchain->color_format()) ||
        !physics_debug_renderer.initialize(*render_device, swapchain->color_format())) {
        log(LogLevel::error, "VICE Editor could not initialize its visual workspace.");
        return 1;
    }

    vice::editor::UiStatus status;
    status.world_name = project.name + " · " + project.startup_world.stem().string();
    status.render_backend = std::string{vice::render::backend_name(render_device->backend())};
    status.world_loaded = true;

    log(
        LogLevel::info,
        "VICE Editor ready: " + std::to_string(world.entity_count()) + " World entities, " + status.render_backend
    );

    vice::core::FrameClock clock;
    vice::editor::SceneViewport active_viewport{};
    std::size_t preview_history_size = 0U;
    std::size_t preview_history_cursor = 0U;
    bool running = true;

    while (running) {
        vice::platform::Event event{vice::platform::QuitRequested{}};
        while (platform_services->poll_event(event)) {
            ui.handle_event(event);
            if (route_event_to_scene(event, active_viewport)) {
                const auto selection_before_event = selection.selected();
                scene_renderer.handle_event(
                    event,
                    active_viewport,
                    world,
                    selection,
                    history,
                    ui.transform_tool()
                );
                resource_mesh_renderer.handle_event(
                    event,
                    active_viewport,
                    world,
                    selection,
                    assets,
                    selection_before_event
                );
                physics_debug_renderer.handle_event(
                    event,
                    active_viewport,
                    preview_snapshot,
                    selection
                );
            }

            std::visit(
                [&](const auto& payload) {
                    using Payload = std::decay_t<decltype(payload)>;
                    if constexpr (
                        std::is_same_v<Payload, vice::platform::QuitRequested> ||
                        std::is_same_v<Payload, vice::platform::WindowCloseRequested>
                    ) {
                        running = false;
                    } else if constexpr (std::is_same_v<Payload, vice::platform::WindowMetricsChanged>) {
                        if (payload.metrics.drawable_width == 0U || payload.metrics.drawable_height == 0U) return;
                        if (!render_device->wait_idle() || !swapchain->resize(payload.metrics)) {
                            log(LogLevel::error, "VICE Editor could not resize the swapchain.");
                            running = false;
                            return;
                        }
                        auto replacement_depth = create_depth_target(*render_device, payload.metrics);
                        if (!replacement_depth) {
                            log(LogLevel::error, "VICE Editor could not resize the depth target.");
                            running = false;
                            return;
                        }
                        depth_target = std::move(replacement_depth);
                    }
                },
                event.payload
            );
        }

        if (!running || ui.consume_quit_request()) break;

        (void)jobs.pump_main(64);
        clock.tick();
        const float delta_seconds = static_cast<float>(std::min(clock.delta_seconds(), 0.1));
        const auto metrics = window->metrics();
        if (metrics.width == 0U || metrics.height == 0U ||
            metrics.drawable_width == 0U || metrics.drawable_height == 0U) {
            std::this_thread::sleep_for(std::chrono::milliseconds{16});
            continue;
        }

        if (!preview.update(delta_seconds)) {
            log(LogLevel::error, "VICE Editor Live Preview could not advance simulation.");
            preview.pause();
        }
        preview_snapshot = preview.snapshot();

        profiler_history.push_frame(delta_seconds);
        const auto profiler_snapshot = vice::editor::sample_profiler(
            delta_seconds,
            jobs,
            preview.coordinator(),
            *render_device,
            world
        );

        scene_renderer.update(delta_seconds, active_viewport);
        resource_mesh_renderer.update(delta_seconds, active_viewport);
        physics_debug_renderer.update(delta_seconds, active_viewport);
        if (!ui.begin_frame(metrics, delta_seconds)) {
            log(LogLevel::error, "VICE Editor could not begin a UI frame.");
            break;
        }
        ui.draw(world, selection, history, console, status);
        resource_browser.draw(assets);
        resource_browser.draw_inspector_resources(world, selection, assets);
        profiler_ui.draw(profiler_snapshot, profiler_history);
        const bool preview_stale = preview.active() &&
            (history.size() != preview_history_size || history.applied_count() != preview_history_cursor);
        physics_debug_ui.draw(preview_snapshot, preview_stale);
        physics_debug_ui.draw_inspector_live(preview_snapshot, selection);
        ui.end_frame();
        active_viewport = ui.scene_viewport();

        if (const auto mesh_request = resource_browser.consume_add_mesh_request(); mesh_request.has_value()) {
            std::string diagnostics;
            const auto created = vice::editor::create_mesh_instance(
                world,
                history,
                *mesh_request,
                resource_mesh_renderer.suggested_spawn_position(world),
                &diagnostics
            );
            if (created.has_value()) {
                selection.select(*created);
                log(LogLevel::info, "VICE Editor added a mesh instance to World.");
            } else {
                log(LogLevel::error, "VICE Editor could not add the mesh instance: " + diagnostics);
            }
        }

        if (ui.consume_undo_request() && history.can_undo() && !history.undo(world)) {
            log(LogLevel::error, "VICE Editor could not undo the last authoring command.");
        }
        if (ui.consume_redo_request() && history.can_redo() && !history.redo(world)) {
            log(LogLevel::error, "VICE Editor could not redo the next authoring command.");
        }
        if (ui.consume_save_request()) {
            if (save_path.empty()) {
                log(LogLevel::error, "VICE Editor has no configured World save path.");
            } else {
                std::string diagnostics;
                if (vice::world::save_world_snapshot(save_path, world.snapshot(), &diagnostics)) {
                    history.mark_saved();
                    log(LogLevel::info, "VICE Editor saved the World for project " + project.name + ".");
                } else {
                    log(LogLevel::error, "VICE Editor could not save the World: " + diagnostics);
                }
            }
        }

        const bool stop_preview = physics_debug_ui.consume_stop_request();
        const bool start_preview = physics_debug_ui.consume_start_request();
        const bool reset_preview = physics_debug_ui.consume_reset_request();
        const bool toggle_preview = physics_debug_ui.consume_play_pause_request();
        const bool step_preview = physics_debug_ui.consume_step_request();

        if (stop_preview) preview.stop();
        if (start_preview || reset_preview) {
            std::string diagnostics;
            if (preview.reset(world, jobs, assets, &diagnostics)) {
                preview_history_size = history.size();
                preview_history_cursor = history.applied_count();
                log(LogLevel::info, "VICE Editor Live Preview reset from the authored World.");
            } else {
                log(LogLevel::error, "VICE Editor Live Preview could not start: " + diagnostics);
            }
        }
        if (toggle_preview && preview.active()) {
            if (preview.playback() == vice::editor::PreviewPlayback::running) preview.pause();
            else preview.play();
        }
        if (step_preview && preview.active() && preview.playback() != vice::editor::PreviewPlayback::running &&
            !preview.step_once()) {
            log(LogLevel::error, "VICE Editor Live Preview could not single-step simulation.");
        }
        preview_snapshot = preview.snapshot();

        auto commands = render_device->create_command_list();
        if (!commands) {
            log(LogLevel::error, "VICE Editor could not create a render command list.");
            break;
        }

        vice::render::RenderPassDesc pass;
        pass.clear_color = {0.020F, 0.024F, 0.032F, 1.0F};
        pass.depth_attachment = depth_target.get();
        pass.clear_depth = 1.0F;
        pass.clear_color_attachment = true;
        pass.clear_depth_attachment = true;

        if (!commands->begin_render_pass(*swapchain, pass) ||
            (active_viewport.drawable() &&
                !resource_mesh_renderer.render(
                    world,
                    selection,
                    assets,
                    *commands,
                    active_viewport,
                    metrics
                )) ||
            (active_viewport.drawable() &&
                !scene_renderer.render(
                    world,
                    selection,
                    ui.transform_tool(),
                    *commands,
                    active_viewport,
                    metrics
                )) ||
            (active_viewport.drawable() &&
                !physics_debug_renderer.render(
                    preview_snapshot,
                    physics_debug_ui.layers(),
                    selection,
                    *commands,
                    active_viewport,
                    metrics
                )) ||
            !ui_renderer.render(*render_device, *commands, metrics) ||
            !commands->end_render_pass() ||
            !commands->present(*swapchain) ||
            render_device->submit(std::move(commands)) == vice::render::invalid_submission) {
            log(LogLevel::error, "VICE Editor frame rendering failed.");
            break;
        }
    }

    (void)render_device->wait_idle();
    console.detach();
    log(LogLevel::info, "VICE Editor closed cleanly.");
    return 0;
}
