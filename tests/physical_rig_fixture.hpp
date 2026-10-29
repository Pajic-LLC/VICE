#pragma once
#include <cstdlib>
#include <iostream>
#include <vice/presentation/assets.hpp>
#include <vice/runtime/skinning.hpp>
#include <vice/tools/world_authoring.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/tools/world_edit.hpp>
#include <vice/world/snapshot_file.hpp>
namespace physical_test {
using namespace vice;
inline void require(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "Physical rig: " << message << '\n';
        std::exit(1);
    }
}
inline world::PersistentId id(unsigned n) {
    world::PersistentId::Bytes b{};
    b[0] = 0xea;
    b[6] = 0x40;
    b[8] = 0x80;
    b[15] = static_cast<std::uint8_t>(n);
    return world::PersistentId{b};
}
inline presentation::Rig rig() {
    presentation::Rig r;
    r.id = {id(10)};
    r.nodes = {{"mesh", -1, {}, {id(11)}},
               {"base", -1, {}, {}},
               {"driven", 1, {{0, 1, 0}, {}, 1}, {}},
               {"child", 2, {{.5F, 0, 0}, {}, 1}, {}}};
    r.clips = {{"rest", 1, {}}};
    presentation::Skin skin;
    skin.node = 0;
    auto inverse = math::identity();
    inverse(1, 3) = -1;
    skin.joints = {{2, inverse}};
    presentation::SkinWeights weights;
    weights.weights[0] = 1;
    skin.vertices.assign(3, weights);
    r.skins = {skin};
    return r;
}
inline world::WorldSnapshot fixture() {
    world::World w;
    world::RegionDesc r;
    r.id = {1};
    r.simulation_island = {1};
    r.origin = {1e12, 0, 0};
    require(w.register_region(r), "region");
    require(w.create_resource({id(10)}).has_value() && w.create_resource({id(11)}).has_value(),
            "resources");
    for (unsigned n = 1; n <= 4; ++n) {
        auto e = w.create_entity(id(n));
        require(e && w.set_spatial_state(*e, {{1e12, n == 3 ? 2.0 : 0.0, 0}, {1}}), "entities");
    }
    world::AnimationPlayer p;
    p.rig = {id(10)};
    p.playing = false;
    require(w.set_animation(*w.find(id(1)), p), "root");
    require(w.set_rig_part(*w.find(id(2)), {id(1), 0}) &&
                w.set_resource_reference(*w.find(id(2)),
                                         {world::ResourceRole::render_mesh, {id(11)}}) &&
                w.set_rig_part(*w.find(id(4)), {id(1), 3}),
            "visual parts");
    require(w.set_rigid_box(*w.find(id(3)), {}) &&
                w.set_authored_transform(*w.find(id(3)), {{}, {.1F, .4F, .1F}}),
            "physical source ignores collider scale");
    require(w.set_physical_rig_binding(*w.find(id(3)), {id(1), 2}), "physical binding");
    return w.snapshot();
}
inline render::MeshAsset mesh() {
    render::MeshAsset m;
    m.version = 2;
    m.stable_id = id(11).bytes();
    m.source_fingerprint = 123;
    m.vertices.resize(3);
    m.vertices[1].position[1] = 1;
    m.vertices[2].position[0] = 1;
    m.indices = {0, 1, 2};
    return m;
}
inline bool near(float a, float b) { return std::abs(a - b) < 1e-4F; }
} // namespace physical_test
