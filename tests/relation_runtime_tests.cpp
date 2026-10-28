#include "presentation_fixture.hpp"
#include <algorithm>
#include <vice/runtime/debug.hpp>
#include <vice/runtime/module_loader.hpp>
#if defined(VICE_RELATION_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
namespace {
using namespace fixture;
world::PersistentId partner(unsigned n) {
    auto b = id("b9000000-0000-4000-8000-000000000001").bytes();
    b[14] = static_cast<std::uint8_t>(n >> 8);
    b[15] = static_cast<std::uint8_t>(n);
    return world::PersistentId{b};
}
const auto kind = id("a2000000-0000-4000-8000-00000000004f"),
           type = id("a2000000-0000-4000-8000-000000000050");
void populate(world::World &w) {
    for (unsigned n = 1; n <= 70; ++n)
        require(w.create_entity(partner(n)).has_value() &&
                    w.add_relation(world::RelationKey{actor, kind, partner(n)}),
                "populate paged relation graph");
}
} // namespace
int main() {
    using namespace fixture;
    Data f;
    auto &w = f.world;
    populate(w);
    const world::RelationKey key{actor, kind, car};
    auto module = runtime::load_game_module(VICE_RELATION_TEST_MODULE);
    require(static_cast<bool>(module), "load SDK-only relation module");
    project::Manifest manifest;
    runtime::GameModuleLifecycle lifecycle{*module.module, {manifest, w}};
    require(lifecycle.start(), "relation SDK create/read/state/page/size/thread/old-host checks");
    lifecycle.event(platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}});
    render::Color color;
    lifecycle.frame(color);
    require(color.red == 1 && color.green == 1,
            "relation mutation, page invalidation and frame guards");
    const auto state = w.relation_component(key, {type});
    require(state && std::get<std::int64_t>(state->fields[0].value) == 6,
            "World owns stateful edge");
    const auto debug = runtime::sample_runtime_debug_entity(w, actor);
    require(std::any_of(debug.relation_states.begin(), debug.relation_states.end(),
                        [&](const auto &r) { return r.key == key && !r.components.empty(); }),
            "Runtime debug contains typed edge state");
    const auto json = runtime::serialize_runtime_debug_entity_snapshot(debug, "test");
    require(json.find("relation_states") != std::string::npos &&
                json.find("RelationState") != std::string::npos,
            "debug explains state schema");
    const auto saved = world::encode_world_snapshot(w.snapshot());
    require(w.restore(*world::decode_world_snapshot(saved.bytes).snapshot),
            "restore equal-count graph into same World");
    lifecycle.event(platform::Event{platform::KeyEvent{.key = platform::Key::r, .pressed = true}});
    lifecycle.frame(color);
    require(color.red == 1 && color.green == 1, "restoring graph invalidates saved page revision");
    lifecycle.stop();
    world::World resumed;
    require(resumed.restore(*world::decode_world_snapshot(saved.bytes).snapshot),
            "fresh World restore");
    auto fresh = runtime::load_game_module(VICE_RELATION_TEST_MODULE);
    require(static_cast<bool>(fresh), "fresh module");
    runtime::GameModuleLifecycle next{*fresh.module, {manifest, resumed}};
    require(next.start(), "fresh module preserves schema and relation payload");
    next.frame(color);
    require(color.red == 1 && color.green == 1 && resumed.relation_component(key, {type}) == state,
            "fresh Runtime continues relation state");
    next.stop();
#if defined(VICE_RELATION_EDITOR)
    Data source;
    populate(source.world);
    const auto original = tools::world_cooker::serialize_source_manifest(source.world.snapshot());
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    editor::PreviewRuntime preview;
    const editor::PreviewGameConfig config{manifest, VICE_RELATION_TEST_MODULE,
                                           source.workspace.cooked_root, false};
    std::string d;
    require(preview.reset(source.world, jobs, editor::AssetIndex{}, &d, &config),
            "Editor same relation World/SDK");
    preview.play();
    preview.set_input_focus(true);
    require(preview.handle_event(
                platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}}),
            "preview relation edit");
    preview.pause();
    require(preview.step_once() && preview.world()->relation_component(key, {type}) == state,
            "preview and Runtime relation state agree");
    const auto inspector = editor::project_inspector(*preview.world(), actor);
    require(inspector &&
                std::any_of(inspector->relations.begin(), inspector->relations.end(),
                            [&](const auto &r) { return r.kind == kind && !r.components.empty(); }),
            "Inspector sees stateful edge");
    require(tools::world_cooker::serialize_source_manifest(source.world.snapshot()) == original,
            "Play cannot mutate authored graph");
    preview.stop();
    require(preview.reset(source.world, jobs, editor::AssetIndex{}, &d, &config) &&
                std::get<std::int64_t>(
                    preview.world()->relation_component(key, {type})->fields[0].value) == 5,
            "Play reset rebuilds source graph");
#endif
    std::cout << "SDK stateful relations, paging, restore and Editor parity passed\n";
}
