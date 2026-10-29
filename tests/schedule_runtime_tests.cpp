#include "presentation_fixture.hpp"
#include <vice/runtime/debug.hpp>
#include <vice/runtime/module_loader.hpp>
#if defined(VICE_SCHEDULE_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
int main() {
    using namespace fixture;
    Data f;
    auto module = runtime::load_game_module(VICE_SCHEDULE_TEST_MODULE);
    require(static_cast<bool>(module), "scheduler module");
    project::Manifest manifest;
    runtime::GameModuleLifecycle lifecycle{*module.module, {manifest, f.world}};
    require(lifecycle.start(), "scheduler module guards and query");
    const auto saved = world::encode_world_snapshot(f.world.snapshot());
    require(static_cast<bool>(saved), "encode pending schedule");
    const auto decoded = world::decode_world_snapshot(saved.bytes);
    if (!decoded.snapshot)
        std::cerr << decoded.diagnostics;
    require(decoded.snapshot.has_value(), "decode pending schedule");
    world::World next;
    require(next.restore(*decoded.snapshot), "restore pending schedule");
    require(next.causal_info().retained_count == f.world.causal_info().retained_count &&
                next.scheduler_info().pending == 1,
            "checkpoint stores future work without firing");
    lifecycle.event(platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}});
    render::Color color;
    lifecycle.frame(color);
    require(color.red == 1 && f.world.scheduler_info().pending == 1,
            "SDK dispatch and read-only frame");
    const auto debug = runtime::sample_runtime_debug_entity(f.world, car);
    require(debug.schedules.entries.size() == 1 && debug.schedules.entries[0].occurrence == 2 &&
                debug.causal_history.events.size() == 2,
            "target-side scheduler inspection");
    require(runtime::serialize_runtime_debug_entity_snapshot(debug, "test").find("TimedFact") !=
                std::string::npos,
            "schedule-only schema explained");
    auto fresh = runtime::load_game_module(VICE_SCHEDULE_TEST_MODULE);
    require(static_cast<bool>(fresh), "fresh module");
    runtime::GameModuleLifecycle resumed{*fresh.module, {manifest, next}};
    require(resumed.start(), "fresh Runtime reuses persistent schedule ID");
    resumed.event(platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}});
    resumed.frame(color);
    require(color.red == 1 && next.schedule(debug.schedules.entries[0].id)->occurrence == 2,
            "Runtime resume parity");
    resumed.stop();
    lifecycle.event(platform::Event{platform::KeyEvent{.key = platform::Key::r, .pressed = true}});
    lifecycle.frame(color);
    require(color.red == 1 && f.world.scheduler_info().count == 0, "module cancellation");
    lifecycle.stop();
#if defined(VICE_SCHEDULE_EDITOR)
    Data source;
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    editor::PreviewRuntime preview;
    const editor::PreviewGameConfig config{manifest, VICE_SCHEDULE_TEST_MODULE,
                                           source.workspace.cooked_root, false};
    require(preview.reset(source.world, jobs, editor::AssetIndex{}, nullptr, &config),
            "scheduler preview clone");
    preview.play();
    preview.set_input_focus(true);
    require(preview.handle_event(
                platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}}),
            "preview schedule input");
    preview.pause();
    require(preview.step_once() &&
                preview.world()->schedule(debug.schedules.entries[0].id)->occurrence == 2 &&
                source.world.scheduler_info().count == 0,
            "Editor Runtime parity and authoring isolation");
    require(preview.reset(source.world, jobs, editor::AssetIndex{}, nullptr, &config) &&
                preview.world()->schedule(debug.schedules.entries[0].id)->occurrence == 0,
            "Reset restores future work");
#endif
    std::cout << "Scheduler SDK, checkpoint, cancellation and Editor parity passed\n";
}
