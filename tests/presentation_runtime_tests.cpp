#include "presentation_fixture.hpp"
#include <vice/runtime/debug.hpp>
#include <vice/runtime/module_loader.hpp>
#if defined(VICE_PRESENTATION_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
int main() {
    using namespace fixture;
    Data f;
    auto &w = f.world;
    presentation::System system;
    std::string d;
    require(system.reset(w, f.workspace.cooked_root, &d), "shared runtime service");
    auto module = runtime::load_game_module(VICE_PRESENTATION_TEST_MODULE);
    require(static_cast<bool>(module), "load SDK-only presentation module");
    project::Manifest manifest;
    runtime::GameModuleLifecycle lifecycle{*module.module, {manifest, w, nullptr, &system}};
    require(lifecycle.start(),
            "SDK queries, invalid commands, sizes, old hosts and callback thread guard");
    lifecycle.event(platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}});
    require(w.animation(*w.find(actor))->clip == 2 && w.animation(*w.find(actor))->time == 0 &&
                w.audio_emitter(*w.find(car))->playing,
            "game commands publish saveable intent before evaluation");
    require(system.advance(.25, &d), "shared service evaluates SDK-selected clip/audio");
    render::Color color;
    lifecycle.frame(color);
    require(color.red == 1 && color.green == 1 && color.blue == 1,
            "SDK reads evaluated state and frame callbacks reject writes");
    lifecycle.stop();
    const auto debug = runtime::sample_runtime_debug_entity(w, actor, nullptr, &system);
    const auto engine = runtime::sample_runtime_debug_entity(w, car, nullptr, &system);
    require(debug.animation && debug.animation_state.find("ride") != std::string::npos &&
                engine.audio_state.find("left_gain") != std::string::npos,
            "normal Runtime debugging exposes evaluated animation and audio");
    const auto saved = world::encode_world_snapshot(w.snapshot());
    world::World resumed;
    require(resumed.restore(*world::decode_world_snapshot(saved.bytes).snapshot),
            "save SDK-selected presentation state");
    presentation::System next;
    require(next.reset(resumed, f.workspace.cooked_root) &&
                resumed.animation(*resumed.find(actor)) == w.animation(*w.find(actor)) &&
                resumed.audio_emitter(*resumed.find(car)) == w.audio_emitter(*w.find(car)),
            "restart preserves animation and audio clocks/profile");
#if defined(VICE_PRESENTATION_EDITOR)
    Data source;
    editor::AssetIndex assets;
    require(assets.refresh(source.workspace.asset_root, source.workspace.cooked_root),
            "index imported presentation assets");
    require(assets.find_resource({rig}) &&
                assets.find_resource({rig})->kind == editor::AssetKind::rig &&
                assets.find_resource({rig})->cooked_valid,
            "normal asset browser recognizes validated imported rig");
    const auto original = tools::world_cooker::serialize_source_manifest(source.world.snapshot());
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    editor::PreviewRuntime preview;
    const editor::PreviewGameConfig config{manifest, VICE_PRESENTATION_TEST_MODULE,
                                           source.workspace.cooked_root, false};
    require(preview.reset(source.world, jobs, editor::AssetIndex{}, &d, &config),
            "Editor uses same imported assets and SDK module");
    preview.play();
    preview.set_input_focus(true);
    require(preview.handle_event(
                platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}}),
            "preview presentation control event");
    preview.pause();
    for (unsigned i = 0; i < 30; ++i)
        require(preview.step_once(), "preview fixed-step presentation");
    require(preview.clear_color().red == 1 && preview.clear_color().green == 1 &&
                preview.clear_color().blue == 1 &&
                preview.world()->animation(*preview.world()->find(actor))->clip == 2,
            "preview matches Runtime animation/audio semantics");
    require(tools::world_cooker::serialize_source_manifest(source.world.snapshot()) == original,
            "preview presentation never mutates authored source");
#endif
    std::cout << "SDK, saved presentation and Editor parity passed\n";
}
