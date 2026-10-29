#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vice/presentation/system.hpp>
#include <vice/runtime/debug.hpp>
#include <vice/runtime/resource_residency.hpp>
#include <vice/runtime/skinning.hpp>
#include <vice/world/snapshot_file.hpp>
#if defined(VICE_SKINNING_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
namespace {
using namespace vice;
void require(bool b, const char *s) {
    if (!b) {
        std::cerr << "Skin runtime: " << s << '\n';
        std::exit(1);
    }
}
world::PersistentId id(unsigned n) {
    world::PersistentId::Bytes b{};
    b[0] = 0xd2;
    b[6] = 0x40;
    b[8] = 0x80;
    b[15] = static_cast<std::uint8_t>(n);
    return world::PersistentId{b};
}
bool near(float a, float b) { return std::abs(a - b) < 1e-4F; }
presentation::Rig rig() {
    presentation::Rig r;
    r.id = {id(10)};
    r.nodes = {{"mesh", -1, {{30, 0, 0}, {}, 1}, {id(11)}},
               {"joint0", -1, {}, {}},
               {"joint1", 1, {{0, 1, 0}, {}, 1}, {}}};
    presentation::Track t;
    t.node = 2;
    t.path = presentation::Path::translation;
    t.keys = {presentation::Key{0, {0, 1, 0, 0}}, presentation::Key{1, {0, 3, 0, 0}}};
    r.clips = {{"deform", 1, {t}}};
    presentation::Skin s;
    s.node = 0;
    auto inverse = math::identity();
    inverse(1, 3) = -1;
    s.joints = {{1, math::identity()}, {2, inverse}};
    presentation::SkinWeights w;
    w.joints[1] = 1;
    w.weights[0] = w.weights[1] = .5;
    s.vertices.assign(3, w);
    r.skins = {s};
    return r;
}
} // namespace
int main() {
    using namespace vice;
    const auto folder = std::filesystem::temp_directory_path() /
                        ("vice-runtime-skin-" + world::PersistentId::random().to_string());
    std::filesystem::create_directories(folder);
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
        }
    } cleanup{folder};
    {
        std::ofstream f(folder / "skin.vrigc");
        f << presentation::serialize_rig(rig());
    }
    world::World w;
    world::RegionDesc region;
    region.id = {1};
    region.simulation_island = {1};
    region.streaming_cell = {1};
    region.origin = {1e12, 0, 0};
    require(w.register_region(region), "large-world region");
    require(w.create_resource({id(10)}).has_value() && w.create_resource({id(11)}).has_value(),
            "asset references");
    for (unsigned n = 1; n <= 4; ++n) {
        const auto e = w.create_entity(id(n));
        require(e && w.set_spatial_state(*e, {{1e12 + double(n), 0, 0}, {1}}),
                "persistent entities");
    }
    for (unsigned n = 1; n <= 2; ++n) {
        world::AnimationPlayer p;
        p.rig = {id(10)};
        p.playing = false;
        require(w.set_animation(*w.find(id(n)), p) &&
                    w.set_resource_reference(*w.find(id(n + 2)),
                                             {world::ResourceRole::render_mesh, {id(11)}}) &&
                    w.set_rig_part(*w.find(id(n + 2)), {id(n), 0}),
                "root/part state");
    }
    render::MeshAsset mesh;
    mesh.version = 2;
    mesh.stable_id = id(11).bytes();
    mesh.source_fingerprint = 123;
    mesh.vertices.resize(3);
    mesh.vertices[1].position[1] = 1;
    mesh.vertices[2].position[0] = 1;
    mesh.indices = {0, 1, 2};
    runtime::CookedMeshCatalog catalog;
    require(catalog.upsert_runtime_record({{id(11)}, {}, mesh, true}), "immutable bind source");
    runtime::RuntimeSkinningCache cache;
    require(cache.reset(folder), "rig cache");
    const auto instances = [&] {
        return std::vector<runtime::RenderMeshInstance>{
            {id(3), {id(11)}, {}, *w.spatial_state(*w.find(id(3))), {}},
            {id(4), {id(11)}, {}, *w.spatial_state(*w.find(id(4))), {}}};
    };
    const auto before = world::encode_world_snapshot(w.snapshot()).bytes;
    auto first = instances();
    require(cache.apply(w, catalog, first) && first[0].mesh != first[1].mesh &&
                first[0].mesh != world::ResourceId{id(11)},
            "per-instance derived geometry");
    require(world::encode_world_snapshot(w.snapshot()).bytes == before && w.resource_count() == 2,
            "no generated IDs/vertices in persistent World truth");
    runtime::RuntimeRenderResidentScene scene;
    scene.meshes = first;
    scene.residency.regions.push_back({{1}, {1}, world::RegionFidelity::full});
    runtime::CookedRenderMaterialCatalog materials;
    runtime::CookedTextureCatalog textures;
    const auto residency =
        runtime::build_runtime_render_resource_residency(scene, catalog, materials, textures, {});
    require(residency && residency.residency.geometry.resident_bytes ==
                             2 * (2 * 3 * sizeof(render::MeshVertex) + 3 * sizeof(std::uint32_t)),
            "geometry residency charges previous-position streams");
    require(first[0].spatial.global_position.x == 1e12 + 1 &&
                near(catalog.find(first[0].mesh)->asset.vertices[0].position[0], 0),
            "double root coordinates; mesh node transform ignored for skin");
    auto player = *w.animation(*w.find(id(1)));
    player.time = 1;
    require(w.set_animation(*w.find(id(1)), player), "change one playback clock");
    auto next = instances();
    require(cache.apply(w, catalog, next), "new deformation");
    const auto a = cache.find(next[0].mesh), b = cache.find(next[1].mesh);
    require(a && b && near(a->asset->vertices[0].position[1], 1) &&
                near(b->asset->vertices[0].position[1], 0) && near(a->previous[0].position[1], 0),
            "independent clocks and previous deformation history");
    require(near(catalog.find({id(11)})->asset.vertices[0].position[1], 0) &&
                cache.info().instances == 2 && cache.info().evaluated_vertices == 3,
            "source immutable and only dirty pose evaluated");
    const auto alias = next[0].mesh;
    next = instances();
    require(cache.apply(w, catalog, next) && cache.info().evaluated_vertices == 0 &&
                near(cache.find(alias)->previous[0].position[1], 1),
            "unchanged pose settles previous vertices without resampling");
    const auto saved = world::encode_world_snapshot(w.snapshot());
    world::World restored;
    require(saved && restored.restore(*world::decode_world_snapshot(saved.bytes).snapshot),
            "World checkpoint");
    runtime::RuntimeSkinningCache fresh;
    require(fresh.reset(folder), "fresh asset cache");
    auto resumed = instances();
    require(fresh.apply(restored, catalog, resumed) &&
                near(fresh.find(resumed[0].mesh)->asset->vertices[0].position[1], 1) &&
                near(fresh.find(resumed[0].mesh)->previous[0].position[1], 1),
            "restore reconstructs pose and resets temporal deformation history");
    auto live_restore = instances();
    require(cache.apply(restored, catalog, live_restore) &&
                near(cache.find(live_restore[0].mesh)->previous[0].position[1], 1),
            "live World restore invalidates deformation history");
    require(w.set_region_fidelity({1}, world::RegionFidelity::reduced), "reduced fidelity");
    player.time = .501;
    require(w.set_animation(*w.find(id(1)), player), "coarse clock");
    next = instances();
    require(cache.apply(w, catalog, next), "coarse pose");
    player.time = .51;
    require(w.set_animation(*w.find(id(1)), player), "same coarse interval");
    next = instances();
    require(cache.apply(w, catalog, next) && cache.info().evaluated_vertices == 0,
            "reduced skin evaluation cadence");
    presentation::System service;
    require(service.reset(w, folder), "shared presentation reset");
    const auto info = service.animation(id(1));
    require(info && info->skin_count == 1 && info->joint_count == 2 && info->weighted_vertices == 3,
            "structured skeletal inspection");
    const auto debug = runtime::sample_runtime_debug_entity(w, id(1), nullptr, &service);
    require(runtime::serialize_runtime_debug_entity_snapshot(debug, "test")
                    .find("\"weighted_vertices\":3") != std::string::npos,
            "Runtime debug exposes weighted geometry");
    auto failed = instances();
    const auto state = world::encode_world_snapshot(w.snapshot()).bytes;
    failed[0].mesh = {id(99)};
    require(!cache.apply(w, catalog, failed) &&
                world::encode_world_snapshot(w.snapshot()).bytes == state,
            "bad skin source cannot mutate World");
    // Departed instances free their generated slot and catalog record before new ones are admitted.
    std::vector<runtime::RenderMeshInstance> empty;
    require(cache.apply(w, catalog, empty) && cache.info().instances == 0 && !catalog.find(alias),
            "cache eviction removes derived records");
#if defined(VICE_SKINNING_EDITOR)
    require(w.set_region_fidelity({1}, world::RegionFidelity::full), "preview full fidelity");
    player.time = .2;
    player.playing = true;
    require(w.set_animation(*w.find(id(1)), player), "preview playback intent");
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    editor::PreviewRuntime preview;
    project::Manifest manifest;
    const editor::PreviewGameConfig config{manifest, {}, folder, false};
    std::string diagnostics;
    require(preview.reset(w, jobs, editor::AssetIndex{}, &diagnostics, &config),
            "weighted Editor clone/reset");
    require(preview.step_once() &&
                preview.world()->animation(*preview.world()->find(id(1)))->time > .2 &&
                w.animation(*w.find(id(1)))->time == .2,
            "weighted playback uses clone and isolates authored intent");
    require(preview.reset(w, jobs, editor::AssetIndex{}, &diagnostics, &config) &&
                preview.world()->animation(*preview.world()->find(id(1)))->time == .2,
            "Editor Reset restores weighted playback clock");
#endif
    runtime::RuntimeSkinningCache invalid;
    require(invalid.reset(folder), "prior valid cache");
    {
        std::ofstream f(folder / "bad.vrigc");
        f << "vice-rig 99";
    }
    require(!invalid.reset(folder), "invalid asset reset rejected transactionally");
    std::cout << "Per-instance skinning, pose history, fidelity, World persistence and "
                 "reconstruction passed\n";
}
