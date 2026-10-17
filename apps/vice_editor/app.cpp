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
#include <vice/editor/project_build.hpp>
#include <vice/editor/project_session.hpp>
#include <vice/editor/project_workflow.hpp>
#include <vice/editor/project_workflow_ui.hpp>
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

[[nodiscard]] bool build_new_project_world(vice::world::World& world) {
    return world.register_region(vice::world::RegionDesc{
        .id = vice::world::RegionId{1},
        .simulation_island = vice::world::SimulationIslandId{1},
        .origin = {0.0, 0.0, 0.0},
        .streaming_cell = {0, 0, 0},
        .fidelity = vice::world::RegionFidelity::full,
        .bounds = vice::world::RegionBounds{
            .min = {-100.0F, -100.0F, -100.0F},
            .max = {100.0F, 100.0F, 100.0F},
        },
    });
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

[[nodiscard]] std::filesystem::path editor_asset_cache_root() {
#if defined(VICE_EDITOR_ASSET_CACHE_ROOT)
    return std::filesystem::path{VICE_EDITOR_ASSET_CACHE_ROOT};
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

[[nodiscard]] std::filesystem::path editor_player_executable() {
#if defined(VICE_EDITOR_PLAYER_EXECUTABLE)
    return std::filesystem::path{VICE_EDITOR_PLAYER_EXECUTABLE};
#else
    return {};
#endif
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
    const bool allow_development_fallback,
    const bool allow_new_project_fallback
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

    if (allow_new_project_fallback) {
        if (!build_new_project_world(world)) {
            vice::core::log(
                vice::core::LogLevel::error,
                "VICE Editor could not create the starter project World."
            );
            return false;
        }
        vice::core::log(vice::core::LogLevel::info, "VICE Editor created the starter project World.");
        return true;
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

[[nodiscard]] std::string world_display_name(
    const vice::editor::WorldDocument& document,
    const vice::editor::ProjectSession& session
) {
    if (document.has_path()) return document.path().stem().string();
    return session.manifest().startup_world.stem().string();
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
    if (argc > 2) {
        log(LogLevel::error, "VICE Editor accepts at most one project manifest path.");
        return 1;
    }

    std::filesystem::path requested_project_path = argc < 2
        ? editor_default_project_path()
        : std::filesystem::path{argv[1]};
    if (requested_project_path.empty()) {
        log(LogLevel::error, "VICE Editor has no development project manifest for first launch.");
        return 1;
    }

    bool using_development_project = argc < 2;
    bool show_start_surface = argc < 2;
    bool newly_created_project = false;

    vice::editor::RecentProjects recent_projects{editor_state_root() / "recent_projects.txt"};
    std::string recent_diagnostics;
    if (!recent_projects.load(&recent_diagnostics)) {
        log(LogLevel::warning, "VICE Editor recent projects: " + recent_diagnostics);
    }

    for (;;) {
        vice::editor::ConsoleHistory console{2000};
        if (!console.attach()) {
            log(LogLevel::warning, "VICE Editor console could not attach to Core logging.");
        }

        log(LogLevel::info, std::string{"VICE Editor · VICE "} + std::string{vice::core::version_string()});
        log(LogLevel::info, std::string{"Platform: "} + std::string{vice::core::platform_name(platform)});

        std::string project_diagnostics;
        auto project_session = vice::editor::ProjectSession::open(
            requested_project_path,
            editor_asset_cache_root(),
            editor_state_root(),
            &project_diagnostics
        );
        if (!project_session.has_value()) {
            log(LogLevel::error, "VICE Editor could not open the project: " + project_diagnostics);
            return 1;
        }

        const auto& project = project_session->manifest();
        const auto project_path = project_session->manifest_path();
        const auto asset_source_root = project_session->asset_root();
        const auto startup_world_source = project_session->startup_world_source();
        const auto cooked_asset_root = project_session->cooked_root();
        const auto default_save_path = project_session->working_world_path();

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

        std::error_code state_directory_error;
        std::filesystem::create_directories(project_session->editor_state_root(), state_directory_error);
        if (state_directory_error) {
            log(
                LogLevel::error,
                "VICE Editor could not create the project state directory: " + state_directory_error.message()
            );
            return 1;
        }

        if (!show_start_surface && !using_development_project) {
            if (!recent_projects.remember(project_path, &recent_diagnostics)) {
                log(LogLevel::warning, "VICE Editor recent projects: " + recent_diagnostics);
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
        if (!assets.refresh(asset_source_root, cooked_asset_root)) {
            log(LogLevel::warning, "VICE Editor asset index: " + assets.diagnostics());
        }

        vice::editor::WorldDocument document;
        auto& world = document.world();
        if (!load_editor_world(
                world,
                default_save_path,
                using_development_project ? editor_legacy_save_path() : std::filesystem::path{},
                project_session->cooked_startup_world(),
                using_development_project,
                newly_created_project
            )) {
            return 1;
        }
        document.mark_clean();
        bool document_intrinsic_dirty = false;

        vice::editor::SelectionModel selection;
        if (using_development_project) {
            if (const auto cube_id = vice::world::PersistentId::parse(cube_id_text); cube_id.has_value() &&
                world.find(*cube_id).has_value()) {
                selection.select(*cube_id);
            }
        }
        vice::editor::TransformHistory history;
        history.mark_saved();

        const std::filesystem::path layout_path = project_session->layout_path();
        if (!layout_path.empty()) {
            std::error_code error;
            std::filesystem::create_directories(layout_path.parent_path(), error);
            if (error) {
                log(LogLevel::warning, "VICE Editor could not create its layout directory: " + error.message());
            }
        }

        vice::editor::EditorUi ui{layout_path};
        vice::editor::ProjectWorkflowUi project_workflow_ui;
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
        status.world_name = project.name + " · " + world_display_name(document, *project_session);
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
        std::string workflow_diagnostics;
        std::optional<std::filesystem::path> next_project_path;
        bool next_project_is_new = false;
        bool running = true;

        const auto reset_document_state = [&]() {
            selection.clear();
            history.clear();
            history.mark_saved();
            preview.stop();
            preview_snapshot = {};
            preview_history_size = 0U;
            preview_history_cursor = 0U;
        };

        const auto sync_document_dirty = [&]() {
            document.set_dirty(document_intrinsic_dirty || history.dirty());
        };

        const auto save_current_world = [&](const std::optional<std::filesystem::path>& explicit_path) {
            std::string diagnostics;
            const bool saved = explicit_path.has_value()
                ? document.save_as(*explicit_path, &diagnostics)
                : (document.has_path()
                    ? document.save(&diagnostics)
                    : document.save_as(default_save_path, &diagnostics));
            if (!saved) {
                workflow_diagnostics = diagnostics;
                log(LogLevel::error, "VICE Editor could not save the World: " + diagnostics);
                return false;
            }
            history.mark_saved();
            document_intrinsic_dirty = false;
            document.mark_clean();
            workflow_diagnostics.clear();
            log(LogLevel::info, "VICE Editor saved the World for project " + project.name + ".");
            return true;
        };

        const auto switch_project = [&](const std::filesystem::path& path) {
            std::string diagnostics;
            const auto candidate = vice::editor::ProjectSession::open(
                path,
                editor_asset_cache_root(),
                editor_state_root(),
                &diagnostics
            );
            if (!candidate.has_value()) {
                workflow_diagnostics = diagnostics;
                return false;
            }
            next_project_path = candidate->manifest_path();
            next_project_is_new = false;
            running = false;
            workflow_diagnostics.clear();
            return true;
        };

        const auto replace_with_new_world = [&](const vice::editor::DirtyReplacePolicy policy) {
            vice::world::World replacement;
            if (!build_new_project_world(replacement)) {
                workflow_diagnostics = "Could not create a new World region.";
                return false;
            }
            std::string diagnostics;
            if (!document.new_empty(policy, &diagnostics) || !document.world().restore(replacement.snapshot())) {
                workflow_diagnostics = diagnostics.empty() ? "Could not create the new World." : diagnostics;
                return false;
            }
            reset_document_state();
            document_intrinsic_dirty = true;
            sync_document_dirty();
            workflow_diagnostics.clear();
            log(LogLevel::info, "VICE Editor created a new World document.");
            return true;
        };

        const auto replace_with_open_world = [&](
            const std::filesystem::path& path,
            const vice::editor::DirtyReplacePolicy policy
        ) {
            std::string diagnostics;
            if (!document.open(path, policy, &diagnostics)) {
                workflow_diagnostics = diagnostics;
                return false;
            }
            reset_document_state();
            document_intrinsic_dirty = false;
            document.mark_clean();
            workflow_diagnostics.clear();
            log(LogLevel::info, "VICE Editor opened World document " + document.path().generic_string() + ".");
            return true;
        };

        while (running) {
            vice::platform::Event event{vice::platform::QuitRequested{}};
            while (platform_services->poll_event(event)) {
                ui.handle_event(event);
                if (!show_start_surface && route_event_to_scene(event, active_viewport)) {
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

            if (!running || (!show_start_surface && ui.consume_quit_request())) break;

            (void)jobs.pump_main(64);
            clock.tick();
            const float delta_seconds = static_cast<float>(std::min(clock.delta_seconds(), 0.1));
            const auto metrics = window->metrics();
            if (metrics.width == 0U || metrics.height == 0U ||
                metrics.drawable_width == 0U || metrics.drawable_height == 0U) {
                std::this_thread::sleep_for(std::chrono::milliseconds{16});
                continue;
            }

            if (!show_start_surface) {
                if (!preview.update(delta_seconds)) {
                    log(LogLevel::error, "VICE Editor Live Preview could not advance simulation.");
                    preview.pause();
                }
                preview_snapshot = preview.snapshot();
                profiler_history.push_frame(delta_seconds);
                scene_renderer.update(delta_seconds, active_viewport);
                resource_mesh_renderer.update(delta_seconds, active_viewport);
                physics_debug_renderer.update(delta_seconds, active_viewport);
            }

            if (!ui.begin_frame(metrics, delta_seconds)) {
                log(LogLevel::error, "VICE Editor could not begin a UI frame.");
                break;
            }

            if (!show_start_surface) {
                const auto profiler_snapshot = vice::editor::sample_profiler(
                    delta_seconds,
                    jobs,
                    preview.coordinator(),
                    *render_device,
                    world
                );
                status.world_name = project.name + " · " + world_display_name(document, *project_session);
                ui.draw(world, selection, history, console, status);
                resource_browser.draw(assets);
                resource_browser.draw_inspector_resources(world, selection, assets);
                profiler_ui.draw(profiler_snapshot, profiler_history);
                const bool preview_stale = preview.active() &&
                    (history.size() != preview_history_size || history.applied_count() != preview_history_cursor);
                physics_debug_ui.draw(preview_snapshot, preview_stale);
                physics_debug_ui.draw_inspector_live(preview_snapshot, selection);
            }

            sync_document_dirty();
            vice::editor::ProjectWorkflowStatus workflow_status;
            workflow_status.project_loaded = !show_start_surface;
            workflow_status.show_start_surface = show_start_surface;
            workflow_status.project_name = project.name;
            workflow_status.project_manifest = project_path;
            workflow_status.world_name = world_display_name(document, *project_session);
            workflow_status.world_dirty = document.dirty();
            workflow_status.world_has_path = document.has_path();
            workflow_status.world_path = document.path();
            workflow_status.development_project = editor_default_project_path();
            workflow_status.recent_projects = recent_projects.entries();
            workflow_status.diagnostics = workflow_diagnostics;
            project_workflow_ui.draw(workflow_status);

            ui.end_frame();
            active_viewport = show_start_surface ? vice::editor::SceneViewport{} : ui.scene_viewport();

            if (!show_start_surface && project_workflow_ui.consume_build_project_request()) {
                const auto result = vice::editor::build_project(*project_session);
                if (result) {
                    workflow_diagnostics.clear();
                    if (!assets.refresh(asset_source_root, cooked_asset_root)) {
                        log(LogLevel::warning, "VICE Editor asset index: " + assets.diagnostics());
                    }
                    log(
                        LogLevel::info,
                        "VICE Editor built project " + project.name + " · " +
                            result.cooked_startup_world.generic_string()
                    );
                } else {
                    workflow_diagnostics = result.diagnostics;
                    log(LogLevel::error, "VICE Editor could not build the project: " + result.diagnostics);
                }
            }

            if (!show_start_surface && project_workflow_ui.consume_run_project_request()) {
                const auto player_executable = editor_player_executable();
                if (player_executable.empty()) {
                    workflow_diagnostics = "VICE Player is not available in this editor build.";
                    log(LogLevel::error, workflow_diagnostics);
                } else {
                    const auto result = vice::editor::run_project(*project_session, player_executable);
                    if (result) {
                        workflow_diagnostics.clear();
                        if (!assets.refresh(asset_source_root, cooked_asset_root)) {
                            log(LogLevel::warning, "VICE Editor asset index: " + assets.diagnostics());
                        }
                        log(LogLevel::info, "VICE Editor launched project " + project.name + ".");
                    } else {
                        workflow_diagnostics = result.diagnostics;
                        log(LogLevel::error, "VICE Editor could not run the project: " + result.diagnostics);
                    }
                }
            }

            if (project_workflow_ui.consume_open_development_project_request()) {
                show_start_surface = false;
                workflow_diagnostics.clear();
            }

            if (const auto create_request = project_workflow_ui.consume_new_project_request();
                create_request.has_value()) {
                const auto result = vice::editor::create_project(*create_request);
                if (result) {
                    next_project_path = result.manifest_path;
                    next_project_is_new = true;
                    running = false;
                } else {
                    workflow_diagnostics = result.diagnostics;
                }
            }

            if (const auto open_request = project_workflow_ui.consume_open_project_request();
                open_request.has_value()) {
                sync_document_dirty();
                if (document.dirty()) {
                    workflow_diagnostics = "Resolve unsaved World changes before switching projects.";
                } else {
                    (void)switch_project(*open_request);
                }
            }

            if (!show_start_surface) {
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
                        workflow_diagnostics.clear();
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
                sync_document_dirty();

                if (ui.consume_save_request() || project_workflow_ui.consume_save_world_request()) {
                    (void)save_current_world(std::nullopt);
                }
                if (const auto save_as = project_workflow_ui.consume_save_world_as_request(); save_as.has_value()) {
                    (void)save_current_world(save_as);
                }

                if (project_workflow_ui.consume_new_world_request()) {
                    (void)replace_with_new_world(vice::editor::DirtyReplacePolicy::reject);
                }

                if (const auto open_world = project_workflow_ui.consume_open_world_request(); open_world.has_value()) {
                    (void)replace_with_open_world(*open_world, vice::editor::DirtyReplacePolicy::reject);
                }

                if (const auto confirmed = project_workflow_ui.consume_confirmed_replacement(); confirmed.has_value()) {
                    const bool save_ok = confirmed->resolution == vice::editor::DirtyResolution::discard ||
                        save_current_world(std::nullopt);
                    if (save_ok) {
                        switch (confirmed->kind) {
                        case vice::editor::ReplacementKind::new_world:
                            (void)replace_with_new_world(vice::editor::DirtyReplacePolicy::discard);
                            break;
                        case vice::editor::ReplacementKind::open_world:
                            (void)replace_with_open_world(
                                confirmed->path,
                                vice::editor::DirtyReplacePolicy::discard
                            );
                            break;
                        case vice::editor::ReplacementKind::switch_project:
                            (void)switch_project(confirmed->path);
                            break;
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
            }

            auto commands = render_device->create_command_list();
            if (!commands) {
                log(LogLevel::error, "VICE Editor could not create a render command list.");
                break;
            }

            vice::render::RenderPassDesc pass;
            pass.clear_color = show_start_surface
                ? vice::render::Color{0.018F, 0.022F, 0.030F, 1.0F}
                : vice::render::Color{0.020F, 0.024F, 0.032F, 1.0F};
            pass.depth_attachment = depth_target.get();
            pass.clear_depth = 1.0F;
            pass.clear_color_attachment = true;
            pass.clear_depth_attachment = true;

            if (!commands->begin_render_pass(*swapchain, pass) ||
                (!show_start_surface && active_viewport.drawable() &&
                    !resource_mesh_renderer.render(
                        world,
                        selection,
                        assets,
                        *commands,
                        active_viewport,
                        metrics
                    )) ||
                (!show_start_surface && active_viewport.drawable() &&
                    !scene_renderer.render(
                        world,
                        selection,
                        ui.transform_tool(),
                        *commands,
                        active_viewport,
                        metrics
                    )) ||
                (!show_start_surface && active_viewport.drawable() &&
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
        log(LogLevel::info, "VICE Editor closed project session cleanly.");

        if (!next_project_path.has_value()) return 0;
        requested_project_path = *next_project_path;
        using_development_project = false;
        newly_created_project = next_project_is_new;
        show_start_surface = false;
    }
}
