#include "presentation_fixture.hpp"
#include <vice/runtime/debug.hpp>
#include <vice/runtime/module_loader.hpp>
#if defined(VICE_EVENT_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
int main() {
    using namespace fixture;
    Data f;
    auto module = runtime::load_game_module(VICE_EVENT_TEST_MODULE);
    require(static_cast<bool>(module), "event module");
    project::Manifest manifest;
    runtime::GameModuleLifecycle lifecycle{*module.module, {manifest, f.world}};
    require(lifecycle.start(), "event SDK bounds, old host, wrong thread, payload and parents");
    render::Color color;
    lifecycle.frame(color);
    require(color.red == 1 && f.world.signal_info().retained_count == 1,
            "read-only frame and live signal");
    const auto debug = runtime::sample_runtime_debug_entity(f.world, car);
    require(debug.causal_history.events.size() == 2 && debug.signals.events.size() == 1,
            "target-side debug includes retained facts and signals");
    const auto json = runtime::serialize_runtime_debug_entity_snapshot(debug, "test");
    require(json.find("ExampleFact") != std::string::npos &&
                json.find("world_signals") != std::string::npos,
            "structured event inspection");
    const auto saved = world::encode_world_snapshot(f.world.snapshot());
    require(static_cast<bool>(saved), "event checkpoint");
    world::World next;
    require(next.restore(*world::decode_world_snapshot(saved.bytes).snapshot) &&
                next.signal_info().retained_count == 0,
            "checkpoint excludes transient delivery");
    auto fresh = runtime::load_game_module(VICE_EVENT_TEST_MODULE);
    require(static_cast<bool>(fresh), "fresh module");
    runtime::GameModuleLifecycle resumed{*fresh.module, {manifest, next}};
    require(resumed.start(), "fresh Runtime consumes past facts and continues cause");
    resumed.frame(color);
    require(color.red == 1 && next.causal_info().retained_count == 6,
            "continued same journal with new capture");
    resumed.stop();
    lifecycle.event(platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}});
    lifecycle.frame(color);
    require(color.red == 1 && f.world.signal_info().retained_count == 0,
            "SDK expiry and pruned read");
    lifecycle.stop();
#if defined(VICE_EVENT_EDITOR)
    Data source;
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    editor::PreviewRuntime preview;
    const editor::PreviewGameConfig config{manifest, VICE_EVENT_TEST_MODULE,
                                           source.workspace.cooked_root, false};
    require(preview.reset(source.world, jobs, editor::AssetIndex{}, nullptr, &config),
            "Editor event clone");
    preview.play();
    preview.pause();
    require(preview.world()->causal_info().retained_count == 3 &&
                preview.world()->signal_info().retained_count == 1 &&
                source.world.causal_info().retained_count == 0,
            "Editor module parity and isolated authoring World");
    require(preview.reset(source.world, jobs, editor::AssetIndex{}, nullptr, &config) &&
                preview.world()->causal_info().retained_count == 3,
            "Play reset does not duplicate old facts");
#endif
    std::cout << "World event SDK, checkpoint and Editor parity passed\n";
}
