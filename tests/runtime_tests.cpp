#include <vice/platform/dynamic_library.hpp>
#include <vice/runtime/module_api.hpp>
#include <vice/runtime/module_loader.hpp>
#include <vice/runtime/runtime.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/snapshot_file.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "runtime test failed: " << message << '\n';
    std::exit(1);
}

class RecordingModule final : public vice::runtime::GameModule {
public:
    std::vector<std::string> calls;
    double last_delta = 0.0;

    bool on_start(vice::runtime::GameContext& context) override {
        require(context.world.entity_count() == 1U, "module sees authoritative World on start");
        calls.emplace_back("start");
        return true;
    }

    void on_event(
        vice::runtime::GameContext&,
        const vice::platform::Event&
    ) override {
        calls.emplace_back("event");
    }

    void on_update(vice::runtime::GameContext&, const double delta_seconds) override {
        last_delta = delta_seconds;
        calls.emplace_back("update");
    }

    void on_frame(
        vice::runtime::GameContext&,
        vice::render::Color& clear_color
    ) override {
        clear_color.red = 0.25F;
        calls.emplace_back("frame");
    }

    void on_stop(vice::runtime::GameContext&) override {
        calls.emplace_back("stop");
    }
};

} // namespace

int main() {
    using namespace vice;

    std::error_code error;
    const auto temp_root = std::filesystem::temp_directory_path(error) / "vice_runtime_test_44000000";
    require(!error, "temporary directory path");
    std::filesystem::remove_all(temp_root, error);
    error.clear();
    std::filesystem::create_directories(temp_root, error);
    require(!error, "temporary directory creation");

    const auto project_id = project::ProjectId::parse(
        "44000000-0000-4000-8000-000000000141"
    );
    require(project_id.has_value(), "project ID parse");

    project::Manifest manifest;
    manifest.id = *project_id;
    manifest.name = "Runtime Test Project";
    manifest.game_name = "Runtime Test Game";
    manifest.game_target = "runtime-test";
    manifest.asset_root = "assets";
    manifest.startup_world = "worlds/main.vworld";

    const auto manifest_path = temp_root / "vice.runtime.viceproj";
    std::string diagnostics;
    require(project::save_manifest(manifest_path, manifest, &diagnostics), "runtime manifest save");
    require(diagnostics.empty(), "runtime manifest diagnostics");

    const auto content_root = temp_root / "content";
    const auto cooked_world = project::resolve_cooked_startup_world(content_root, manifest);
    std::filesystem::create_directories(cooked_world.parent_path(), error);
    require(!error, "cooked World directory creation");

    world::World authored_world;
    const auto entity_id = world::PersistentId::parse(
        "45000000-0000-4000-8000-000000000001"
    );
    require(entity_id.has_value(), "entity persistent ID parse");
    require(authored_world.create_entity(*entity_id).has_value(), "runtime test entity creation");
    require(
        world::save_world_snapshot(cooked_world, authored_world.snapshot(), &diagnostics),
        "cooked World save"
    );
    require(diagnostics.empty(), "cooked World save diagnostics");

    const auto fake_executable = temp_root / "runtime-test.exe";
    const auto staged = runtime::staged_launch_paths(fake_executable, &diagnostics);
    require(diagnostics.empty(), "staged launch diagnostics");
    require(staged.manifest == manifest_path, "staged manifest path");
    require(staged.cooked_cache_root == content_root, "staged content root");

    const auto staged_module = runtime::staged_game_module_path(fake_executable, "runtime-test");
    require(
        staged_module == temp_root / "modules" /
            ("runtime-test" + std::string{platform::dynamic_library_extension()}),
        "staged game module path"
    );

    const auto explicit_paths = runtime::explicit_launch_paths(
        manifest_path,
        content_root,
        &diagnostics
    );
    require(diagnostics.empty(), "explicit launch diagnostics");
    require(explicit_paths.manifest == std::filesystem::absolute(manifest_path).lexically_normal(), "explicit manifest path");
    require(
        explicit_paths.cooked_cache_root == std::filesystem::absolute(content_root).lexically_normal(),
        "explicit content root"
    );

    auto loaded = runtime::load_project(explicit_paths);
    require(loaded.project.has_value(), "runtime project load");
    require(loaded.diagnostics.empty(), "runtime project diagnostics");
    require(loaded.project->manifest.game_target == "runtime-test", "runtime target identity");
    require(loaded.project->startup_world_path == cooked_world, "runtime startup World path");
    require(loaded.project->world != nullptr, "runtime World allocation");
    require(loaded.project->world->entity_count() == 1U, "runtime World restore");

    RecordingModule module;
    {
        runtime::GameContext context{
            .project = loaded.project->manifest,
            .world = *loaded.project->world,
        };
        runtime::GameModuleLifecycle lifecycle{module, context};
        require(lifecycle.start(), "module lifecycle start");
        require(lifecycle.started(), "module lifecycle started state");
        require(!lifecycle.stopped(), "module lifecycle not stopped after start");

        const platform::Event key_event{platform::KeyEvent{
            .window = 7U,
            .key = platform::Key::w,
            .pressed = true,
        }};
        lifecycle.event(key_event);
        lifecycle.update(0.016);
        render::Color clear_color{};
        lifecycle.frame(clear_color);
        require(clear_color.red == 0.25F, "module can configure first frame presentation");
        lifecycle.stop();
        require(lifecycle.stopped(), "module lifecycle stopped state");
    }
    require(
        module.calls == std::vector<std::string>{"start", "event", "update", "frame", "stop"},
        "module callback order"
    );
    require(module.last_delta == 0.016, "module update delta");

    auto dynamic_module = runtime::load_game_module(std::filesystem::path{VICE_RUNTIME_TEST_MODULE});
    require(static_cast<bool>(dynamic_module), "dynamic project game module loads");
    require(dynamic_module.diagnostics.empty(), "dynamic project game module diagnostics");
    {
        runtime::GameContext context{
            .project = loaded.project->manifest,
            .world = *loaded.project->world,
        };
        runtime::GameModuleLifecycle lifecycle{*dynamic_module.module, context};
        require(lifecycle.start(), "dynamic module starts with host World API");
        lifecycle.event(platform::Event{platform::KeyEvent{
            .window = 7U,
            .key = platform::Key::space,
            .pressed = true,
        }});
        lifecycle.update(0.02);
        render::Color clear_color{};
        lifecycle.frame(clear_color);
        require(clear_color.red == 0.75F, "dynamic module frame callback");
        require(clear_color.green == 0.25F, "dynamic module frame color");
        lifecycle.stop();
    }

    auto legacy_module = runtime::load_game_module(VICE_RUNTIME_LEGACY_MODULE);
    require(static_cast<bool>(legacy_module), "frozen ABI 1 module loads against extended host");
    {
        runtime::GameContext context{loaded.project->manifest, *loaded.project->world};
        runtime::GameModuleLifecycle lifecycle{*legacy_module.module, context};
        require(lifecycle.start(), "frozen ABI 1 module can call original host prefix");
    }
    runtime::module::HostApi old_host;
    old_host.size = static_cast<std::uint32_t>(runtime::module::host_api_v1_size);
    runtime::module::ModuleApi compatible_api;
    require(runtime::module::detail::load<runtime::module::Game>(1, &old_host, &compatible_api),
        "new SDK module accepts old host prefix");
    compatible_api.destroy(compatible_api.state);
    runtime::module::Context old_context{&old_host};
    runtime::module::EntitySnapshot unavailable;
    require(old_context.read_entity({}, unavailable) == runtime::module::Result::unavailable &&
        old_context.set_body_velocity({}, {}) == runtime::module::Result::unavailable,
        "new methods detect absent extension without accessing beyond old host");
    runtime::module::SupportSnapshot unavailable_support;
    require(old_context.read_body_support({}, unavailable_support) == runtime::module::Result::unavailable,
        "support query detects an absent extension on old hosts");
    runtime::module::DistanceLinkSnapshot unavailable_link;
    require(old_context.read_distance_link({}, unavailable_link) == runtime::module::Result::unavailable,
        "link query detects an absent extension on old hosts");
    runtime::module::DependencySnapshot unavailable_dependency;
    require(old_context.read_dependency({}, unavailable_dependency) == runtime::module::Result::unavailable, "old host has no dependency callback");
    old_host.size = 0;
    require(!runtime::module::detail::load<runtime::module::Game>(1, &old_host, &compatible_api), "truncated original host is rejected");

    const auto missing_entrypoint = runtime::load_game_module(
        std::filesystem::path{VICE_RUNTIME_BAD_MODULE}
    );
    require(!missing_entrypoint, "module missing VICE entrypoint rejected");
    require(
        missing_entrypoint.diagnostics.find(runtime::module::entrypoint_name) != std::string::npos,
        "missing entrypoint diagnostic names symbol"
    );

    const auto incompatible_module = runtime::load_game_module(
        std::filesystem::path{VICE_RUNTIME_INCOMPATIBLE_MODULE}
    );
    require(!incompatible_module, "incompatible module ABI rejected");
    require(
        incompatible_module.diagnostics.find("ABI version") != std::string::npos,
        "incompatible module ABI diagnostic"
    );

    constexpr platform::WindowId game_window = 7U;
    require(
        runtime::classify_event(platform::Event{platform::QuitRequested{}}, game_window).quit,
        "global quit event"
    );
    require(
        runtime::classify_event(
            platform::Event{platform::WindowCloseRequested{.window = game_window}},
            game_window
        ).quit,
        "game window close event"
    );
    require(
        !runtime::classify_event(
            platform::Event{platform::WindowCloseRequested{.window = 8U}},
            game_window
        ).quit,
        "other window close ignored"
    );
    require(
        runtime::classify_event(
            platform::Event{platform::KeyEvent{
                .window = game_window,
                .key = platform::Key::escape,
                .pressed = true,
            }},
            game_window
        ).quit,
        "Escape quits game window"
    );
    require(
        !runtime::classify_event(
            platform::Event{platform::KeyEvent{
                .window = game_window,
                .key = platform::Key::escape,
                .pressed = false,
            }},
            game_window
        ).quit,
        "Escape release does not quit"
    );

    const platform::WindowMetrics resized{
        .width = 1600,
        .height = 900,
        .drawable_width = 3200,
        .drawable_height = 1800,
        .scale_factor = 2.0F,
    };
    const auto resize_decision = runtime::classify_event(
        platform::Event{platform::WindowMetricsChanged{
            .window = game_window,
            .metrics = resized,
        }},
        game_window
    );
    require(resize_decision.resize.has_value(), "game window resize classified");
    require(*resize_decision.resize == resized, "game window resize metrics preserved");
    require(
        !runtime::classify_event(
            platform::Event{platform::WindowMetricsChanged{
                .window = 8U,
                .metrics = resized,
            }},
            game_window
        ).resize.has_value(),
        "other window resize ignored"
    );

    std::filesystem::remove(cooked_world, error);
    require(!error, "cooked World removal");
    const auto missing_world = runtime::load_project(explicit_paths);
    require(!missing_world.project.has_value(), "missing startup World rejected");
    require(
        missing_world.diagnostics.find(cooked_world.generic_string()) != std::string::npos,
        "missing startup World diagnostic names file"
    );

    std::filesystem::remove_all(temp_root, error);
    require(!error, "temporary cleanup");
    return 0;
}
