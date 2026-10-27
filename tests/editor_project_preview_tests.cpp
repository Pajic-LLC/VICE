#include <vice/editor/project_preview.hpp>
#include <vice/platform/dynamic_library.hpp>
#include <vice/runtime/module_loader.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/tools/project_workflow.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "Project preview: " << message << '\n'; std::exit(1); }
}
std::string read(const std::filesystem::path& path) { std::ifstream in{path}; return {std::istreambuf_iterator<char>{in}, {}}; }
void write(const std::filesystem::path& path, const std::string& text) { std::ofstream{path} << text; }
std::size_t count(const std::string& text, const std::string& line) {
    std::size_t result = 0, position = 0;
    while ((position = text.find(line + "\n", position)) != std::string::npos) { ++result; position += line.size() + 1; }
    return result;
}
}
int main() {
    using namespace vice;
    const auto root = std::filesystem::temp_directory_path() / ("vice-project-preview-" + world::PersistentId::random().to_string());
    std::filesystem::create_directories(root / "assets/worlds");
    std::filesystem::create_directories(root / "game");
    const auto trace = root / "trace.txt";
#if defined(_WIN32)
    _putenv_s("VICE_TEST_PREVIEW_TRACE", trace.string().c_str());
#else
    setenv("VICE_TEST_PREVIEW_TRACE", trace.c_str(), 1);
#endif
    const auto manifest_path = root / "preview.viceproj";
    write(manifest_path, "vice-project 1\nid 94000000-0000-4000-8000-000000000001\nname \"Preview Test\"\ngame_name \"Preview Test\"\ngame_target \"preview-test\"\nassets \"assets\"\nstartup_world \"worlds/main.vworld\"\n");
    const std::string source = "vice-world-source 1\nregion 1 10 1000000000000 0 0 0 0 0 full none\n"
        "entity 94000000-0000-4000-8000-000000000202 spatial 1000000000000 5 0 1 rigid_box dynamic 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 discrete solid\n";
    const auto world_path = root / "assets/worlds/main.vworld";
    write(world_path, source);
    const auto game_source = read(VICE_PREVIEW_TEST_GAME_SOURCE);
    write(root / "game/game.cpp", game_source);
    std::string diagnostics;
    const auto session = editor::ProjectSession::open(manifest_path, root / "cache", root / "state", &diagnostics);
    require(session.has_value(), "open authored project");
    const auto parsed = tools::world_cooker::parse_source_manifest(source);
    world::World authored;
    require(parsed.snapshot && authored.restore(*parsed.snapshot), "load authored source");
    const auto actor_id = *world::PersistentId::parse("94000000-0000-4000-8000-000000000202");
    const auto actor = *authored.find(actor_id);
    require(authored.set_spatial_state(actor, {{1000000000000.25, 5, 0}, world::RegionId{1}}), "unsaved edit fixture");
    const auto authored_before = tools::world_cooker::serialize_source_manifest(authored.snapshot());
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    editor::AssetIndex assets;
    editor::PreviewRuntime preview;
    require(editor::reset_project_preview(preview, *session, authored, jobs, assets, &diagnostics), "compile current module and reset preview");
    require(preview.has_game_module() && preview.playback() == editor::PreviewPlayback::paused, "game starts paused in isolated World");
    require(preview.world()->spatial_state(*preview.world()->find(actor_id))->global_position.x == 1000000000000.25,
        "preview clones current unsaved World rather than stale cooked source");
    require(preview.clear_color().red == 0.75F, "preview executes module frame callback with phase restrictions");
    const auto paused_trace = read(trace);
    require(preview.update(1) && read(trace) == paused_trace, "pause suppresses update and frame callbacks");
    const platform::Event key{platform::KeyEvent{.key = platform::Key::right, .pressed = true}};
    require(!preview.handle_event(key), "paused preview does not receive game input");
    preview.play();
    require(!preview.handle_event(key), "unfocused preview does not receive game input");
    preview.set_input_focus(true);
    require(preview.handle_event(key), "focused preview receives actual key event");
    require(preview.handle_event(platform::Event{platform::MouseMoveEvent{.x = 110, .y = 220}}, 100, 200), "pointer events translate to viewport-local coordinates");
    require(preview.update(1.0 / 60.0) && preview.clear_color().red == 0.75F, "module update, physics and frame run in order");
    const auto preview_position = preview.world()->spatial_state(*preview.world()->find(actor_id))->global_position;
    require(preview_position.x > 1000000000000.25 && preview.coordinator()->step_index() == 2, "input drives preview physics");
    {
        world::World standalone;
        require(standalone.restore(authored.snapshot()), "standalone clone");
        rigid_body::AuthoredSimulation physics;
        require(physics.reset(standalone, jobs), "standalone physics");
        const auto module_path = root / ".vice/build/game-module/preview-test/output" / ("preview-test" + std::string{platform::dynamic_library_extension()});
        auto module = runtime::load_game_module(module_path);
        require(static_cast<bool>(module), "standalone loads same compiled module");
        runtime::GameModuleLifecycle lifecycle{*module.module, {session->manifest(), standalone, &physics}};
        require(lifecycle.start(), "standalone module start");
        lifecycle.event(platform::Event{platform::WindowFocusChanged{.focused = true}});
        lifecycle.event(key);
        lifecycle.update(1.0 / 60.0);
        require(physics.advance(1.0 / 60.0), "standalone frame physics");
        render::Color color; lifecycle.frame(color);
        const auto position = standalone.spatial_state(*standalone.find(actor_id))->global_position;
        require(position.x == preview_position.x && std::abs(position.y - preview_position.y) < 1e-6, "same module and input produce matching preview/standalone positions");
    }
    preview.pause();
    const auto paused_step = preview.coordinator()->step_index();
    const auto before_pause = read(trace);
    require(preview.update(1) && read(trace) == before_pause, "pause freezes gameplay callbacks");
    require(preview.step_once() && preview.coordinator()->step_index() == paused_step + 1, "single step runs one gameplay and physics tick");
    require(preview.world()->rigid_box(*preview.world()->find(actor_id))->linear_velocity.x > 0.9F &&
        preview.world()->rigid_box(*preview.world()->find(actor_id))->linear_velocity.x < 1.1F, "pause clears held gameplay input");
    write(root / "game/game.cpp", "not valid C++\n");
    const auto before_failure = preview.coordinator()->step_index();
    require(!editor::reset_project_preview(preview, *session, authored, jobs, assets, &diagnostics) && !diagnostics.empty() &&
        preview.coordinator()->step_index() == before_failure && preview.has_game_module(), "failed module compilation preserves active preview");
    auto rejected_source = game_source;
    const auto return_position = rejected_source.find("return ++starts == 1 &&");
    require(return_position != std::string::npos, "rejected startup fixture");
    rejected_source.replace(return_position, std::string{"return ++starts == 1 &&"}.size(), "return false && ++starts == 1 &&");
    write(root / "game/game.cpp", rejected_source);
    require(!editor::reset_project_preview(preview, *session, authored, jobs, assets, &diagnostics) &&
        preview.coordinator()->step_index() == before_failure && preview.has_game_module(),
        "failed game startup preserves previous session and cloned World");
    write(root / "game/game.cpp", game_source);
    const bool reset_ok = editor::reset_project_preview(preview, *session, authored, jobs, assets, &diagnostics);
    if (!reset_ok) std::cerr << diagnostics << '\n';
    require(reset_ok, "reset rebuilds while previous private image is loaded and starts fresh module globals");
    require(preview.coordinator()->step_index() == 0 && preview.playback() == editor::PreviewPlayback::paused, "reset starts fresh paused simulation");
    preview.stop();
    const auto stopped_trace = read(trace);
    require(count(stopped_trace, "start") == 4 && count(stopped_trace, "stop") == 3 && count(stopped_trace, "destroy") == 4,
        "all successful module sessions stop and destroy exactly once");
    require(read(world_path) == source && tools::world_cooker::serialize_source_manifest(authored.snapshot()) == authored_before,
        "playback, resets and compilation do not save or mutate authored source or unsaved World");
    std::filesystem::rename(root / "game/game.cpp", root / "game/game.saved.cpp");
    world::World moduleless;
    require(moduleless.register_region({.id = world::RegionId{1}, .simulation_island = world::SimulationIslandId{10}}), "moduleless region");
    const auto lab_id = *world::PersistentId::parse("10000000-0000-4000-8000-000000000002");
    const auto lab_named_entity = *moduleless.create_entity(lab_id);
    require(moduleless.set_spatial_state(lab_named_entity, {{0, 5, 0}, world::RegionId{1}}), "moduleless authored entity");
    require(editor::reset_project_preview(preview, *session, moduleless, jobs, assets, &diagnostics) &&
        !preview.has_game_module() && preview.snapshot().bodies.empty() && preview.step_once(),
        "moduleless projects never infer Lab physics from entity identity");
    preview.stop();
    require(read(trace) == stopped_trace, "moduleless preview produces no game callbacks");
    std::filesystem::remove_all(root);
    return 0;
}
