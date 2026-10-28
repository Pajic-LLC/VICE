#include "presentation_fixture.hpp"
#include <vice/runtime/debug.hpp>
#include <vice/runtime/module_loader.hpp>
#if defined(VICE_COMPONENT_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
int main() {
    using namespace fixture;
    Data f;
    auto &w = f.world;
    const auto type = world::ComponentTypeId{id("a2000000-0000-4000-8000-000000000064")};
    auto module = runtime::load_game_module(VICE_COMPONENT_TEST_MODULE);
    require(static_cast<bool>(module), "load SDK-only component module");
    project::Manifest manifest;
    runtime::GameModuleLifecycle lifecycle{*module.module, {manifest, w}};
    require(lifecycle.start(),
            "native registration, query, invalid mutation, old host and thread guards");
    lifecycle.event(platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}});
    render::Color color;
    lifecycle.frame(color);
    require(color.red == 1 && color.green == 1, "SDK event mutation and frame write guards");
    lifecycle.stop();
    const auto data = w.component(*w.find(car), type);
    require(data && std::get<std::int64_t>(data->fields[0].value) == 8, "World owns module state");
    const auto debug = runtime::sample_runtime_debug_entity(w, car);
    require(debug.components.size() == w.components(*w.find(car)).size() &&
                !debug.component_schemas.empty(),
            "Runtime debug includes component payloads and schemas");
    const auto saved = world::encode_world_snapshot(w.snapshot());
    world::World resumed;
    require(resumed.restore(*world::decode_world_snapshot(saved.bytes).snapshot),
            "save/checkpoint component state");
    auto next_module = runtime::load_game_module(VICE_COMPONENT_TEST_MODULE);
    require(static_cast<bool>(next_module), "fresh module");
    runtime::GameModuleLifecycle next{*next_module.module, {manifest, resumed}};
    require(next.start(), "schema registration idempotent after restore");
    next.frame(color);
    require(color.red == 1 && color.green == 1 &&
                resumed.component(*resumed.find(car), type) == data,
            "fresh Runtime module resumes values without overwriting persisted state");
    next.stop();
#if defined(VICE_COMPONENT_EDITOR)
    Data source;
    const auto original = tools::world_cooker::serialize_source_manifest(source.world.snapshot());
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    editor::PreviewRuntime preview;
    const editor::PreviewGameConfig config{manifest, VICE_COMPONENT_TEST_MODULE,
                                           source.workspace.cooked_root, false};
    std::string d;
    require(preview.reset(source.world, jobs, editor::AssetIndex{}, &d, &config),
            "Editor clones same authoritative state and game module");
    preview.play();
    preview.set_input_focus(true);
    require(preview.handle_event(
                platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}}),
            "Editor component event");
    preview.pause();
    require(preview.step_once() &&
                preview.world()->component(*preview.world()->find(car), type) == data,
            "Editor and standalone produce identical payload");
    const auto inspector = editor::project_inspector(*preview.world(), car);
    require(inspector && !inspector->components.empty() && !inspector->component_schemas.empty(),
            "Inspector sees cloned state");
    require(tools::world_cooker::serialize_source_manifest(source.world.snapshot()) == original,
            "Editor does not mutate authored state");
    preview.stop();
    require(
        preview.reset(source.world, jobs, editor::AssetIndex{}, &d, &config) &&
            std::get<std::int64_t>(
                preview.world()->component(*preview.world()->find(car), type)->fields[0].value) ==
                7,
        "Play reset discards run-specific payload mutations");
#endif
    std::cout << "SDK, restart, Runtime inspection and Editor component parity passed\n";
}
