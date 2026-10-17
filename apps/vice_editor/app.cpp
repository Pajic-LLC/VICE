#include "scene_renderer.hpp"

#include <vice/core/build.hpp>
#include <vice/core/log.hpp>
#include <vice/core/platform.hpp>
#include <vice/core/time.hpp>
#include <vice/core/version.hpp>
#include <vice/editor/state.hpp>
#include <vice/editor/ui.hpp>
#include <vice/editor/ui_renderer.hpp>
#include <vice/jobs/jobs.hpp>
#include <vice/platform/event.hpp>
#include <vice/platform/services.hpp>
#include <vice/render/rhi.hpp>
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

[[nodiscard]] std::filesystem::path editor_save_path() {
#if defined(VICE_EDITOR_SAVE_FILE)
    return std::filesystem::path{VICE_EDITOR_SAVE_FILE};
#else
    return {};
#endif
}

[[nodiscard]] bool load_editor_world(
    vice::world::World& world,
    const std::filesystem::path& save_path
) {
    if (!save_path.empty()) {
        std::error_code error;
        const bool save_exists = std::filesystem::exists(save_path, error);
        if (error) {
            vice::core::log(
                vice::core::LogLevel::error,
                "VICE Editor could not inspect its saved World: " + error.message()
            );
            return false;
        }
        if (save_exists) {
            const auto loaded = vice::world::load_world_snapshot(save_path);
            if (!loaded.snapshot.has_value() || !world.restore(*loaded.snapshot)) {
                vice::core::log(
                    vice::core::LogLevel::error,
                    "VICE Editor saved World is invalid: " + loaded.diagnostics
                );
                return false;
            }
            vice::core::log(vice::core::LogLevel::info, "VICE Editor restored the saved World.");
            return true;
        }
    }

#if defined(VICE_EDITOR_COOKED_WORLD)
    const auto loaded = vice::world::load_world_snapshot(VICE_EDITOR_COOKED_WORLD);
    if (!loaded.snapshot.has_value() || !world.restore(*loaded.snapshot)) {
        vice::core::log(
            vice::core::LogLevel::error,
            "VICE Editor could not load the authored World: " + loaded.diagnostics
        );
        return false;
    }
    vice::core::log(vice::core::LogLevel::info, "VICE Editor loaded the authored VICE Lab World.");
    return true;
#else
    if (!build_fallback_world(world)) {
        vice::core::log(vice::core::LogLevel::error, "VICE Editor could not create its fallback World.");
        return false;
    }
    vice::core::log(
        vice::core::LogLevel::warning,
        "VICE Editor is using the development World fallback because cooked tools are disabled."
    );
    return true;
#endif
}

[[nodiscard]] std::filesystem::path editor_layout_path() {
#if defined(VICE_EDITOR_LAYOUT_FILE)
    return std::filesystem::path{VICE_EDITOR_LAYOUT_FILE};
#else
    return {};
#endif
}

[[nodiscard]] std::filesystem::path editor_mesh_path() {
#if defined(VICE_EDITOR_COOKED_SCENE_MESH)
    return std::filesystem::path{VICE_EDITOR_COOKED_SCENE_MESH};
#else
    return {};
#endif
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

int main() {
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

    vice::jobs::Scheduler jobs;
    auto platform_services = vice::platform::PlatformServices::create();
    if (!platform_services) {
        log(LogLevel::error, "VICE Editor could not initialize platform services.");
        return 1;
    }

    vice::platform::WindowDesc window_desc;
    window_desc.title = "VICE Editor";
    window_desc.width = 1600;
    window_desc.height = 980;
    auto window = platform_services->create_window(window_desc);
    if (!window) {
        log(LogLevel::error, "VICE Editor could not create its native window.");
        return 1;
    }

    auto render_device = vice::render::Device::create();
    if (!render_device) {
        log(LogLevel::error, "VICE Editor could not create a render device.");
        return 1;
    }
    auto swapchain = render_device->create_swapchain(*window);
    auto depth_target = create_depth_target(*render_device, window->metrics());
    if (!swapchain || !depth_target) {
        log(LogLevel::error, "VICE Editor could not create render targets.");
        return 1;
    }

    const std::filesystem::path save_path = editor_save_path();
    vice::world::World world;
    if (!load_editor_world(world, save_path)) return 1;

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
    vice::editor::UiRhiRenderer ui_renderer;
    vice::editor::detail::SceneRenderer scene_renderer;
    if (!ui.valid() ||
        !ui_renderer.initialize(*render_device, swapchain->color_format()) ||
        !scene_renderer.initialize(*render_device, swapchain->color_format(), editor_mesh_path())) {
        log(LogLevel::error, "VICE Editor could not initialize its visual workspace.");
        return 1;
    }

    vice::editor::UiStatus status;
    status.world_name = "VICE Lab World";
    status.render_backend = std::string{vice::render::backend_name(render_device->backend())};
    status.world_loaded = true;

    log(
        LogLevel::info,
        "VICE Editor ready: " + std::to_string(world.entity_count()) + " World entities, " + status.render_backend
    );

    vice::core::FrameClock clock;
    vice::editor::SceneViewport active_viewport{};
    bool running = true;

    while (running) {
        vice::platform::Event event{vice::platform::QuitRequested{}};
        while (platform_services->poll_event(event)) {
            ui.handle_event(event);
            if (route_event_to_scene(event, active_viewport)) {
                scene_renderer.handle_event(
                    event,
                    active_viewport,
                    world,
                    selection,
                    history,
                    ui.transform_tool()
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

        scene_renderer.update(delta_seconds, active_viewport);
        if (!ui.begin_frame(metrics, delta_seconds)) {
            log(LogLevel::error, "VICE Editor could not begin a UI frame.");
            break;
        }
        ui.draw(world, selection, history, console, status);
        ui.end_frame();
        active_viewport = ui.scene_viewport();

        if (ui.consume_undo_request() && history.can_undo() && !history.undo(world)) {
            log(LogLevel::error, "VICE Editor could not undo the last transform edit.");
        }
        if (ui.consume_redo_request() && history.can_redo() && !history.redo(world)) {
            log(LogLevel::error, "VICE Editor could not redo the next transform edit.");
        }
        if (ui.consume_save_request()) {
            if (save_path.empty()) {
                log(LogLevel::error, "VICE Editor has no configured World save path.");
            } else {
                std::string diagnostics;
                if (vice::world::save_world_snapshot(save_path, world.snapshot(), &diagnostics)) {
                    history.mark_saved();
                    log(LogLevel::info, "VICE Editor saved the World.");
                } else {
                    log(LogLevel::error, "VICE Editor could not save the World: " + diagnostics);
                }
            }
        }

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
                !scene_renderer.render(
                    world,
                    selection,
                    ui.transform_tool(),
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
