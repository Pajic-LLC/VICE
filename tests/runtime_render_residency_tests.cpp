#include <vice/runtime/render_streaming.hpp>
#include <vice/world/world.hpp>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "runtime render residency test failed: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    require(parsed.has_value(), "fixed UUID parses");
    return *parsed;
}

[[nodiscard]] const vice::runtime::RenderMeshInstance* find_mesh(
    const vice::runtime::RuntimeRenderResidentScene& scene,
    const vice::world::PersistentId entity
) {
    for (const auto& mesh : scene.meshes) {
        if (mesh.entity == entity) return &mesh;
    }
    return nullptr;
}

} // namespace

int main() {
    using namespace vice;

    world::World world_state;
    const world::RegionDesc full_region{
        .id = world::RegionId{10U},
        .simulation_island = world::SimulationIslandId{10U},
        .origin = {1'000'000'000.0, 0.0, 0.0},
        .streaming_cell = {10, 0, 0},
        .fidelity = world::RegionFidelity::full,
    };
    const world::RegionDesc reduced_region{
        .id = world::RegionId{20U},
        .simulation_island = world::SimulationIslandId{20U},
        .origin = {1'000'001'000.0, 0.0, 0.0},
        .streaming_cell = {20, 0, 0},
        .fidelity = world::RegionFidelity::reduced,
    };
    const world::RegionDesc inactive_region{
        .id = world::RegionId{30U},
        .simulation_island = world::SimulationIslandId{30U},
        .origin = {1'000'002'000.0, 0.0, 0.0},
        .streaming_cell = {30, 0, 0},
        .fidelity = world::RegionFidelity::inactive,
    };
    require(world_state.register_region(inactive_region), "inactive region registers");
    require(world_state.register_region(full_region), "full region registers");
    require(world_state.register_region(reduced_region), "reduced region registers");

    const world::ResourceId mesh_resource{required_id("81000000-0000-4000-8000-000000000001")};
    const world::ResourceId environment_resource{required_id("81000000-0000-4000-8000-000000000002")};
    require(world_state.create_resource(mesh_resource).has_value(), "mesh resource registers");
    require(world_state.create_resource(environment_resource).has_value(), "environment resource registers");

    const auto full_mesh_id = required_id("81000000-0000-4000-8000-000000000010");
    const auto reduced_mesh_id = required_id("81000000-0000-4000-8000-000000000020");
    const auto inactive_mesh_id = required_id("81000000-0000-4000-8000-000000000030");
    const auto full_light_id = required_id("81000000-0000-4000-8000-000000000040");
    const auto inactive_light_id = required_id("81000000-0000-4000-8000-000000000050");
    const auto reduced_probe_id = required_id("81000000-0000-4000-8000-000000000060");
    const auto inactive_probe_id = required_id("81000000-0000-4000-8000-000000000070");
    const auto camera_id = required_id("81000000-0000-4000-8000-000000000080");

    const auto add_mesh = [&](const world::PersistentId id, const world::RegionDesc& region, const double x) {
        const auto entity = world_state.create_entity(id);
        require(entity.has_value(), "mesh entity creates");
        require(world_state.set_spatial_state(*entity, world::SpatialState{
            .global_position = {x, 0.0, 0.0},
            .region = region.id,
        }), "mesh spatial state");
        require(world_state.set_resource_reference(*entity, world::ResourceReference{
            .role = world::ResourceRole::render_mesh,
            .resource = mesh_resource,
        }), "mesh reference");
        return *entity;
    };

    const auto full_mesh = add_mesh(full_mesh_id, full_region, full_region.origin.x + 5.0);
    const auto reduced_mesh = add_mesh(reduced_mesh_id, reduced_region, reduced_region.origin.x + 7.0);
    add_mesh(inactive_mesh_id, inactive_region, inactive_region.origin.x + 9.0);

    const auto add_light = [&](const world::PersistentId id, const world::RegionDesc& region) {
        const auto entity = world_state.create_entity(id);
        require(entity.has_value(), "light entity creates");
        require(world_state.set_spatial_state(*entity, world::SpatialState{
            .global_position = region.origin,
            .region = region.id,
        }), "light spatial state");
        require(world_state.set_light(*entity, world::Light{
            .type = world::LightType::point,
            .color = {1.0F, 1.0F, 1.0F},
            .intensity = 2.0F,
            .range = 20.0F,
            .enabled = true,
        }), "light state");
    };
    add_light(full_light_id, full_region);
    add_light(inactive_light_id, inactive_region);

    const auto add_probe = [&](const world::PersistentId id, const world::RegionDesc& region) {
        const auto entity = world_state.create_entity(id);
        require(entity.has_value(), "probe entity creates");
        require(world_state.set_spatial_state(*entity, world::SpatialState{
            .global_position = region.origin,
            .region = region.id,
        }), "probe spatial state");
        require(world_state.set_reflection_probe(*entity, world::ReflectionProbe{
            .half_extents = {10.0F, 10.0F, 10.0F},
            .blend_distance = 1.0F,
            .priority = 0,
            .enabled = true,
        }), "probe state");
        require(world_state.set_resource_reference(*entity, world::ResourceReference{
            .role = world::ResourceRole::reflection_environment,
            .resource = environment_resource,
        }), "probe environment reference");
    };
    add_probe(reduced_probe_id, reduced_region);
    add_probe(inactive_probe_id, inactive_region);

    const auto camera = world_state.create_entity(camera_id);
    require(camera.has_value(), "camera entity creates");
    require(world_state.set_spatial_state(*camera, world::SpatialState{
        .global_position = inactive_region.origin,
        .region = inactive_region.id,
    }), "camera spatial state");
    require(world_state.set_camera(*camera, world::Camera{
        .projection = world::CameraProjection::perspective,
        .vertical_fov_radians = 1.0F,
        .near_plane = 0.1F,
        .far_plane = 1000.0F,
        .primary = true,
    }), "primary camera state");

    auto build = runtime::build_runtime_render_resident_scene(world_state);
    require(static_cast<bool>(build), "initial resident scene builds");
    require(build.scene.residency.regions.size() == 2U, "full and reduced regions are resident");
    require(build.scene.residency.regions[0].region == full_region.id, "resident regions sort by RegionId");
    require(build.scene.residency.regions[0].cell == full_region.streaming_cell, "full region keeps cell identity");
    require(build.scene.residency.regions[1].region == reduced_region.id, "reduced region stays resident");
    require(build.scene.residency.regions[1].cell == reduced_region.streaming_cell, "reduced region keeps cell identity");
    require(!build.scene.residency.resident(inactive_region.id), "inactive region is not resident");
    require(build.scene.meshes.size() == 2U, "inactive mesh is filtered");
    require(find_mesh(build.scene, full_mesh_id) != nullptr, "full mesh is resident");
    require(find_mesh(build.scene, reduced_mesh_id) != nullptr, "reduced mesh is resident");
    require(find_mesh(build.scene, inactive_mesh_id) == nullptr, "inactive mesh is absent");
    require(build.scene.lights.size() == 1U && build.scene.lights[0].entity == full_light_id,
        "inactive authored light is filtered");
    require(build.scene.reflection_probes.size() == 1U &&
        build.scene.reflection_probes[0].entity == reduced_probe_id,
        "inactive local reflection probe is filtered");
    require(runtime::primary_camera_view(world_state, 16.0F / 9.0F).has_value(),
        "primary camera remains authoritative in an inactive region");

    require(world_state.set_region_fidelity(full_region.id, world::RegionFidelity::inactive),
        "full region can become inactive");
    require(world_state.set_region_fidelity(inactive_region.id, world::RegionFidelity::full),
        "inactive region can become full");
    build = runtime::build_runtime_render_resident_scene(world_state);
    require(static_cast<bool>(build), "fidelity-transition scene builds");
    require(find_mesh(build.scene, full_mesh_id) == nullptr, "newly inactive mesh leaves resident scene");
    require(find_mesh(build.scene, inactive_mesh_id) != nullptr, "newly full mesh enters resident scene");
    require(build.scene.lights.size() == 1U && build.scene.lights[0].entity == inactive_light_id,
        "light residency follows fidelity transition");
    require(build.scene.reflection_probes.size() == 2U,
        "probe residency follows fidelity transition while reduced remains resident");

    const auto identity_before = world_state.persistent_id(reduced_mesh);
    const auto position_before = world_state.spatial_state(reduced_mesh)->global_position;
    require(world_state.handoff_spatial(reduced_mesh, inactive_region.id), "mesh hands off across cells");
    build = runtime::build_runtime_render_resident_scene(world_state);
    require(static_cast<bool>(build), "handoff scene builds");
    const auto* handed_off = find_mesh(build.scene, reduced_mesh_id);
    require(handed_off != nullptr, "handed-off mesh remains resident in target full region");
    require(handed_off->spatial.region == inactive_region.id, "handoff updates render region identity");
    require(build.scene.residency.cell(handed_off->spatial.region) ==
        std::optional{inactive_region.streaming_cell}, "handoff resolves target streaming cell");
    require(world_state.persistent_id(reduced_mesh) == identity_before, "handoff preserves persistent identity");
    require(world_state.spatial_state(reduced_mesh)->global_position == position_before,
        "handoff preserves authoritative global position");

    world::World small_world;
    const world::RegionDesc small_region{
        .id = world::RegionId{1U},
        .simulation_island = world::SimulationIslandId{1U},
        .streaming_cell = {},
        .fidelity = world::RegionFidelity::full,
    };
    require(small_world.register_region(small_region), "small-world region registers");
    require(small_world.create_resource(mesh_resource).has_value(), "small-world mesh resource registers");
    const auto small_entity = small_world.create_entity(required_id("81000000-0000-4000-8000-000000000090"));
    require(small_entity.has_value(), "small-world entity creates");
    require(small_world.set_spatial_state(*small_entity, world::SpatialState{
        .global_position = {2.0, 3.0, 4.0},
        .region = small_region.id,
    }), "small-world spatial state");
    require(small_world.set_resource_reference(*small_entity, world::ResourceReference{
        .role = world::ResourceRole::render_mesh,
        .resource = mesh_resource,
    }), "small-world mesh reference");
    const auto small_build = runtime::build_runtime_render_resident_scene(small_world);
    require(static_cast<bool>(small_build) && small_build.scene.meshes.size() == 1U,
        "single full region keeps existing small-world behavior");

    (void)full_mesh;
    return 0;
}
