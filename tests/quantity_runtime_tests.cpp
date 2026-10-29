#include "presentation_fixture.hpp"
#include <vice/runtime/debug.hpp>
#include <vice/runtime/module_loader.hpp>
#if defined(VICE_QUANTITY_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#include <vice/editor/state.hpp>
#endif
int main() {
    using namespace fixture;
    Data f;
    auto module = runtime::load_game_module(VICE_QUANTITY_TEST_MODULE);
    require(static_cast<bool>(module), "quantity module");
    project::Manifest manifest;
    runtime::GameModuleLifecycle lifecycle{*module.module, {manifest, f.world}};
    require(lifecycle.start(), "quantity SDK guards, failures, reserved receipt and registration");
    lifecycle.event(platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}});
    render::Color color;
    lifecycle.frame(color);
    require(color.red == 1, "SDK atomic transfer/read-only frame");
    const world::QuantityTypeId type{id("a2000000-0000-4000-8000-00000000005d")};
    require(f.world.quantity_account({car, type})->amount == 75 &&
                f.world.quantity_account({actor, type})->amount == 25,
            "authoritative World holds balances");
    const auto debug = runtime::sample_runtime_debug_entity(f.world, actor);
    require(
        debug.quantity_accounts.size() == 1 && debug.quantity_types.size() == 1 &&
            debug.quantity_accounts[0].amount == 25 &&
            runtime::serialize_runtime_debug_entity_snapshot(debug, "test").find("ExampleUnits") !=
                std::string::npos,
        "Runtime explains quantity types and holdings");
    const auto saved = world::encode_world_snapshot(f.world.snapshot());
    world::World next;
    require(saved && next.restore(*world::decode_world_snapshot(saved.bytes).snapshot),
            "quantity checkpoint");
    auto fresh = runtime::load_game_module(VICE_QUANTITY_TEST_MODULE);
    require(static_cast<bool>(fresh), "fresh module");
    runtime::GameModuleLifecycle resumed{*fresh.module, {manifest, next}};
    require(resumed.start(), "fresh Runtime reuses types and balances");
    resumed.event(platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}});
    resumed.frame(color);
    require(color.red == 1 && next.quantity_account({car, type})->amount == 50 &&
                next.quantity_account({actor, type})->amount == 50,
            "resume continues transfer without reseeding");
    resumed.stop();
    lifecycle.stop();
#if defined(VICE_QUANTITY_EDITOR)
    world::World undo_world;
    const auto created = id("c9000000-0000-4000-8000-000000000001");
    require(undo_world.create_entity(created).has_value(), "undo holder");
    editor::CommandHistory history;
    world::SnapshotEntity entry;
    entry.persistent_id = created;
    require(history.record(editor::CommandHistory::CreateEntityEntry{entry, {}, "Create holder"}) &&
                undo_world.register_quantity_type({type, "UndoUnits", 1, 1}) &&
                undo_world.create_quantity_account({created, type}, 10) ==
                    world::QuantityResult::success,
            "attach capacity after creation command");
    require(!history.undo(undo_world) && undo_world.find(created).has_value() &&
                undo_world.quantity_account({created, type})->capacity == 10,
            "stale creation undo cannot discard empty capacity state");
    require(undo_world.remove_quantity_account({created, type}) == world::QuantityResult::success &&
                history.undo(undo_world) && history.redo(undo_world),
            "explicit account removal permits undo/redo");
    Data source;
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    editor::PreviewRuntime preview;
    const editor::PreviewGameConfig config{manifest, VICE_QUANTITY_TEST_MODULE,
                                           source.workspace.cooked_root, false};
    require(preview.reset(source.world, jobs, editor::AssetIndex{}, nullptr, &config),
            "quantity preview");
    preview.play();
    preview.set_input_focus(true);
    require(preview.handle_event(
                platform::Event{platform::KeyEvent{.key = platform::Key::e, .pressed = true}}),
            "preview transfer");
    preview.pause();
    require(preview.step_once() && preview.world()->quantity_account({actor, type})->amount == 25 &&
                source.world.quantity_account_count() == 0,
            "Editor Runtime parity and source isolation");
    require(preview.reset(source.world, jobs, editor::AssetIndex{}, nullptr, &config) &&
                preview.world()->quantity_account({car, type})->amount == 100 &&
                preview.world()->quantity_account({actor, type})->amount == 0,
            "Reset restores initial holdings");
#endif
    std::cout << "Quantity SDK, receipts, checkpoint and Editor parity passed\n";
}
