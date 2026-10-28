#include "presentation_fixture.hpp"
#include <vice/runtime/debug.hpp>
#include <vice/runtime/module_loader.hpp>
#if defined(VICE_CLOCK_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
int main() {
    using namespace fixture;
    Data f;
    auto module = runtime::load_game_module(VICE_CLOCK_TEST_MODULE);
    require(static_cast<bool>(module), "clock module");
    project::Manifest manifest;
    runtime::GameModuleLifecycle lifecycle{*module.module, {manifest, f.world}};
    require(lifecycle.start(), "clock SDK size/thread/old-host guards");
    lifecycle.event(platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}});
    render::Color color;
    lifecycle.frame(color);
    require(color.red == 1 && color.green == 1 && f.world.world_clock().ticks == 16,
            "SDK exact stepping/frame guard");
    lifecycle.stop();
    const auto debug = runtime::sample_runtime_debug_entity(f.world, car);
    require(debug.clock == f.world.world_clock(), "Runtime clock inspection");
    const auto save = world::encode_world_snapshot(f.world.snapshot());
    world::World next;
    require(next.restore(*world::decode_world_snapshot(save.bytes).snapshot) &&
                next.world_clock() == f.world.world_clock(),
            "clock checkpoint");
#if defined(VICE_CLOCK_EDITOR)
    Data source;
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    editor::PreviewRuntime preview;
    const editor::PreviewGameConfig config{manifest, VICE_CLOCK_TEST_MODULE,
                                           source.workspace.cooked_root, false};
    require(preview.reset(source.world, jobs, editor::AssetIndex{}, nullptr, &config),
            "clock preview");
    preview.play();
    preview.set_input_focus(true);
    require(preview.handle_event(
                platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}}),
            "preview step event");
    preview.pause();
    require(preview.step_once() && preview.world()->world_clock() == f.world.world_clock() &&
                source.world.world_clock().ticks == 0,
            "preview clock parity/isolation");
#endif
    std::cout << "World time SDK and preview parity passed\n";
}
