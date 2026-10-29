#include "physical_rig_fixture.hpp"
#include <fstream>
#include <thread>
#include <vice/presentation/system.hpp>
#include <vice/runtime/debug.hpp>
#include <vice/runtime/module_loader.hpp>
#if defined(VICE_PHYSICAL_RIG_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
#if defined(VICE_PHYSICAL_RIG_PHYSICS)
#include <vice/rigid_body/authored_simulation.hpp>
#endif
using namespace physical_test;
int main() {
    const auto folder = std::filesystem::temp_directory_path() /
                        ("vice-physical-rig-" + world::PersistentId::random().to_string());
    std::filesystem::create_directories(folder);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{folder};
    {
        std::ofstream f(folder / "rig.vrigc");
        f << presentation::serialize_rig(rig());
    }
    world::World w;
    require(w.restore(fixture()), "restore");
    const auto root = *w.find(id(1)), body = *w.find(id(3));
    const auto bytes = world::encode_world_snapshot(w.snapshot()).bytes;
    const auto decoded = world::decode_world_snapshot(bytes);
    require(decoded && decoded.snapshot->physical_rig_bindings.size() == 1 &&
                world::encode_world_snapshot(*decoded.snapshot).bytes == bytes,
            "deterministic persistence");
    world::World empty;
    auto old = world::encode_world_snapshot(empty.snapshot()).bytes;
    auto rehash = [](auto &bytes) {
        std::uint64_t hash = 14695981039346656037ULL;
        for (std::size_t i = 40; i < bytes.size(); ++i) {
            hash ^= std::to_integer<unsigned char>(bytes[i]);
            hash *= 1099511628211ULL;
        }
        for (unsigned i = 0; i < 8; ++i)
            bytes[32 + i] = static_cast<std::byte>(hash >> (i * 8));
    };
    auto unsafe = old;
    for (unsigned i = 0; i < 4; ++i)
        unsafe[unsafe.size() - 30 + i] = std::byte{255};
    rehash(unsafe);
    const auto rejected = world::decode_world_snapshot(unsafe);
    require(!rejected && rejected.diagnostics.find("physical rig count") != std::string::npos,
            "unsafe physical table rejected before allocation");
    old.erase(old.end() - 30, old.end() - 26);
    old[8] = std::byte{24};
    rehash(old);
    require(static_cast<bool>(world::decode_world_snapshot(old)), "v24 remains readable");
    const auto source = tools::world_cooker::serialize_source_manifest(w.snapshot());
    auto parsed = tools::world_cooker::parse_source_manifest(source);
    require(parsed.snapshot && parsed.snapshot->physical_rig_bindings.size() == 1 &&
                tools::world_cooker::serialize_source_manifest(*parsed.snapshot) == source,
            "source round trip");
    require(!w.clear_rigid_box(body) && !w.clear_spatial_state(body) && !w.clear_animation(root) &&
                !w.destroy_entity(root),
            "reference lifetime");
    auto scale = world::AuthoredTransform{};
    scale.scale = {2, 2, 2};
    require(!w.set_authored_transform(root, scale), "unit root scale contract");
    auto bad = w.snapshot();
    bad.physical_rig_bindings.push_back(bad.physical_rig_bindings[0]);
    require(!w.restore(bad) && world::encode_world_snapshot(w.snapshot()).bytes == bytes,
            "duplicate restore atomic");
    bad = w.snapshot();
    bad.physical_rig_bindings[0].binding.root = id(99);
    require(!w.restore(bad) && world::encode_world_snapshot(w.snapshot()).bytes == bytes,
            "missing root restore atomic");
    auto invalid = *w.physical_rig_binding(body);
    invalid.rotation = {0, 0, 0, 0};
    require(!w.set_physical_rig_binding(body, invalid), "invalid quaternion");
    tools::world_authoring::Transaction tx;
    tx.operations = {tools::world_authoring::SetRigidBox{id(3), {}}};
    require(!tools::world_authoring::apply(w.snapshot(), tx), "invalid source removal atomic");
    tx.operations.insert(tx.operations.begin(),
                         tools::world_authoring::SetPhysicalRigBinding{id(3), {}});
    require(tools::world_authoring::apply(w.snapshot(), tx).success,
            "explicit remove binding and body");
    std::vector<presentation::Pose> pose;
    require(presentation::sample_world_pose(w, root, rig(), 0, 0, pose) &&
                near(pose[2].translation.y, 2) && near(pose[3].translation.x, .5F) &&
                near(pose[3].translation.y, 2),
            "physical override and unbound child");
    auto animated = rig();
    presentation::Track t;
    t.node = 2;
    t.path = presentation::Path::translation;
    t.keys = {{0, {0, 1, 0, 0}}, {1, {0, 9, 0, 0}}};
    presentation::Track child;
    child.node = 3;
    child.path = presentation::Path::translation;
    child.keys = {{0, {.5F, 0, 0, 0}}, {1, {1, 0, 0, 0}}};
    animated.clips[0].tracks = {t, child};
    require(presentation::sample_world_pose(w, root, animated, 0, 1, pose) &&
                near(pose[2].translation.y, 2) && near(pose[3].translation.x, 1) &&
                near(pose[3].translation.y, 2),
            "physical parent overrides animation while animated child preserves local motion");
    world::RegionDesc other;
    other.id = {2};
    other.simulation_island = {2};
    other.origin = {1e12, 0, 0};
    require(w.register_region(other), "second region");
    require(!w.handoff_spatial(body, {2}) && !w.handoff_spatial(root, {2}),
            "binding cannot silently cross regions");
    const auto extra = *w.create_entity(id(5));
    require(w.set_spatial_state(extra, {{1e12, 0, 0}, {1}}) && w.set_rigid_box(extra, {}),
            "extra body");
    require(!w.set_physical_rig_binding(extra, {id(1), 2}),
            "two bodies cannot drive the same node");
    runtime::RuntimeSkinningCache cache;
    require(cache.reset(folder), "skin cache");
    auto bind = mesh();
    runtime::SkinningMeshView view;
    cache.begin_frame();
    require(cache.evaluate(w, id(2), bind, view) && view.asset &&
                near(view.asset->vertices[0].position[1], 1),
            "weighted vertices use body pose");
    cache.end_frame();
    const auto body_before = w.spatial_state(body);
    const auto transform_before = w.authored_transform(body);
    presentation::System system;
    require(system.reset(w, folder) && system.advance(.1) && w.spatial_state(body) == body_before &&
                w.authored_transform(body) == transform_before,
            "presentation never writes physical source");
    world::World invalid_asset;
    require(invalid_asset.restore(w.snapshot()), "invalid asset clone");
    auto node = *invalid_asset.physical_rig_binding(*invalid_asset.find(id(3)));
    node.node = 127;
    require(invalid_asset.set_physical_rig_binding(*invalid_asset.find(id(3)), node) &&
                !system.reset(invalid_asset, folder) && system.advance(0),
            "failed asset reset preserves previous presentation service");
    const auto saved_pose = pose[2].translation;
    require(
        invalid_asset.set_physical_rig_binding(*invalid_asset.find(id(3)), {id(1), 2}) &&
            invalid_asset.set_spatial_state(*invalid_asset.find(id(3)), {{1e12 + 2e6, 0, 0}, {1}}),
        "distant invalid binding");
    require(!presentation::sample_world_pose(invalid_asset, *invalid_asset.find(id(1)), rig(), 0, 0,
                                             pose) &&
                near(pose[2].translation.x, saved_pose.x) &&
                near(pose[2].translation.y, saved_pose.y),
            "invalid pose does not partially replace output");
    require(w.set_authored_transform(body, {{0, 0, .70710678F, .70710678F}, {.1F, .4F, .1F}}),
            "rotate source");
    cache.begin_frame();
    require(cache.evaluate(w, id(2), bind, view) && view.changed &&
                near(view.asset->vertices[0].position[0], 1) &&
                near(view.asset->vertices[0].position[1], 2) &&
                near(view.previous[0].position[1], 1),
            "paused animation still deforms and preserves motion history");
    cache.end_frame();
    require(system.advance(0) &&
                std::abs(w.spatial_state(*w.find(id(4)))->global_position.y - 2.5) < 1e-4,
            "rigid visual child follows driven parent");
    cache.begin_frame();
    require(cache.evaluate(w, id(2), bind, view) && !view.changed &&
                cache.info().evaluated_vertices == 0,
            "unchanged physical palette reuses geometry");
    cache.end_frame();
    auto binding = *w.physical_rig_binding(body);
    binding.translation = {0, .25F, 0};
    require(w.set_physical_rig_binding(body, binding), "COM offset");
    require(presentation::sample_world_pose(w, root, rig(), 0, 0, pose) &&
                near(pose[2].translation.x, -.25F) && near(pose[2].translation.y, 2),
            "rotated COM offset uses metres not collider scale");
    require(w.set_authored_transform(root, {{0, 0, .70710678F, .70710678F}, {1, 1, 1}}) &&
                presentation::sample_world_pose(w, root, rig(), 0, 0, pose) &&
                near(pose[2].translation.x, 2) && near(pose[2].translation.y, .25F),
            "rotated root inverse at large coordinates");
    world::World reduced;
    require(reduced.restore(w.snapshot()) &&
                reduced.set_region_fidelity({1}, world::RegionFidelity::reduced),
            "reduced clone");
    runtime::RuntimeSkinningCache reduced_cache;
    require(reduced_cache.reset(folder), "reduced cache");
    reduced_cache.begin_frame();
    require(reduced_cache.evaluate(reduced, id(2), bind, view), "first reduced pose");
    reduced_cache.end_frame();
    auto position = *reduced.spatial_state(*reduced.find(id(3)));
    position.global_position.y += .25;
    require(reduced.set_spatial_state(*reduced.find(id(3)), position),
            "move reduced physical source");
    reduced_cache.begin_frame();
    require(reduced_cache.evaluate(reduced, id(2), bind, view) && view.changed,
            "paused reduced clock cannot freeze physical deformation");
    reduced_cache.end_frame();
    world::World loaded;
    require(loaded.restore(
                *world::decode_world_snapshot(world::encode_world_snapshot(w.snapshot()).bytes)
                     .snapshot),
            "cold load");
    std::vector<presentation::Pose> resumed;
    require(presentation::sample_world_pose(loaded, *loaded.find(id(1)), rig(), 0, 0, resumed) &&
                near(resumed[2].translation.x, pose[2].translation.x),
            "saved pose reconstructs");
    auto module = runtime::load_game_module(VICE_PHYSICAL_RIG_MODULE);
    require(static_cast<bool>(module), "module");
    project::Manifest manifest;
    runtime::GameModuleLifecycle life{*module.module, {manifest, w}};
    require(life.start(), "module binding query/guards");
    render::Color color;
    life.frame(color);
    require(color.red == 1, "module frame reads binding");
    life.stop();
    const auto debug = runtime::serialize_runtime_debug_entity_snapshot(
        runtime::sample_runtime_debug_entity(w, id(1)), "physical");
    require(debug.find("physical_rig_sources") != std::string::npos &&
                debug.find(id(3).to_string()) != std::string::npos,
            "root structured inspection explains body source");
#if defined(VICE_PHYSICAL_RIG_PHYSICS)
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    world::World native;
    require(native.restore(fixture()), "native fixture");
    rigid_body::AuthoredSimulation sim;
    require(sim.reset(native, jobs), "native reset");
    runtime::RuntimeSkinningCache native_cache;
    require(native_cache.reset(folder), "native skin");
    for (unsigned i = 0; i < 30; ++i)
        require(sim.step(), "native fall");
    native_cache.begin_frame();
    require(native_cache.evaluate(native, id(2), bind, view) &&
                view.asset->vertices[0].position[1] < .9F,
            "actual solver motion deforms visible weighted geometry");
#if defined(VICE_PHYSICAL_RIG_EDITOR)
    editor::AssetIndex assets;
    editor::PreviewRuntime preview;
    std::string diagnostics;
    editor::PreviewGameConfig config{project::Manifest{}, {}, folder};
    world::World edit;
    require(edit.restore(fixture()), "editor source");
    require(preview.reset(edit, jobs, assets, &diagnostics, &config), diagnostics.c_str());
    for (unsigned i = 0; i < 30; ++i)
        require(preview.step_once(), "editor physics");
    require(edit.spatial_state(*edit.find(id(3)))->global_position.y == 2 &&
                preview.world()->spatial_state(*preview.world()->find(id(3)))->global_position.y <
                    2,
            "Play isolation");
    require(preview.reset(edit, jobs, assets, &diagnostics, &config) &&
                preview.world()->physical_rig_binding(*preview.world()->find(id(3))) ==
                    edit.physical_rig_binding(*edit.find(id(3))),
            "Play reset binding");
#endif
#endif
    std::cout << "PhysicalRigBinding=" << sizeof(world::PhysicalRigBinding)
              << " OwnedPhysicalRigBinding=" << sizeof(world::OwnedPhysicalRigBinding)
              << " SnapshotEntity=" << sizeof(world::SnapshotEntity) << '\n';
    std::cout << "Physical rig persistence, geometry and parity passed\n";
}
