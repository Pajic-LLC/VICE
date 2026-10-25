#include <vice/render/ray_tracing.hpp>
#include <vice/runtime/ray_tracing_scene.hpp>
#include <vice/runtime/resource_residency.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace {
[[nodiscard]] vice::world::PersistentId persistent_id(const std::uint8_t value) {
    vice::world::PersistentId::Bytes bytes{}; bytes.back() = value; return vice::world::PersistentId{bytes};
}
[[nodiscard]] vice::world::ResourceId resource_id(const std::uint8_t value) { return vice::world::ResourceId{.value = persistent_id(value)}; }
[[nodiscard]] vice::runtime::CookedMeshRecord mesh_record(const std::uint8_t resource, const std::uint8_t stable, const std::uint64_t fingerprint) {
    vice::render::MeshAsset asset; asset.stable_id.back() = stable; asset.source_fingerprint = fingerprint; asset.vertices.resize(3U);
    asset.vertices[1].position[0] = 1.0F; asset.vertices[2].position[1] = 1.0F; asset.indices = {0U, 1U, 2U};
    return vice::runtime::CookedMeshRecord{.resource_id = resource_id(resource), .path = {}, .asset = std::move(asset), .runtime_generated = false};
}
[[nodiscard]] vice::runtime::RenderMeshInstance instance(const std::uint8_t entity, const std::uint8_t mesh,
    const double x = 0.0, const double y = 0.0, const double z = 0.0) {
    return vice::runtime::RenderMeshInstance{.entity = persistent_id(entity), .mesh = resource_id(mesh), .material = std::nullopt,
        .spatial = vice::world::SpatialState{.global_position = {x, y, z}, .region = {}}, .authored = {}};
}
} // namespace

int main() {
    using namespace vice;
    const std::vector<runtime::CookedMeshRecord> meshes{mesh_record(2U,22U,202U), mesh_record(1U,11U,101U)};
    const std::vector<runtime::RenderMeshInstance> instances{instance(3U,1U), instance(1U,1U,4.0,5.0,6.0), instance(2U,2U)};

    const auto plan = runtime::build_runtime_ray_tracing_scene_plan(instances, meshes);
    assert(plan); assert(plan.blases.size() == 2U); assert(plan.instances.size() == 3U); assert(plan.scene_instance_indices.size() == 3U);
    assert(plan.blases[0].key.mesh == resource_id(1U)); assert(plan.blases[1].key.mesh == resource_id(2U));
    assert(plan.instances[0].entity == persistent_id(1U)); assert(plan.instances[1].entity == persistent_id(2U)); assert(plan.instances[2].entity == persistent_id(3U));
    assert(plan.instances[0].blas_index == 0U); assert(plan.instances[1].blas_index == 1U); assert(plan.instances[2].blas_index == 0U);
    assert(plan.instances[0].transform[3] == 4.0F); assert(plan.instances[0].transform[7] == 5.0F); assert(plan.instances[0].transform[11] == 6.0F);
    // TLAS order is persistent-identity order, while this map resolves back to the
    // authoritative RenderMeshInstance order supplied by Runtime.
    assert(plan.scene_instance_index(0U) == 1U); assert(plan.scene_instance_index(1U) == 2U); assert(plan.scene_instance_index(2U) == 0U);
    assert(plan.scene_instance_index(3U) == runtime::runtime_ray_tracing_invalid_scene_instance);
    for (std::size_t index = 0U; index < plan.instances.size(); ++index) {
        assert(plan.instances[index].instance_id <= render::acceleration_structure_instance_id_max);
        assert(instances[plan.scene_instance_indices[index]].entity == plan.instances[index].entity);
        for (std::size_t other = index + 1U; other < plan.instances.size(); ++other) assert(plan.instances[index].instance_id != plan.instances[other].instance_id);
    }

    auto reordered_instances = instances; std::reverse(reordered_instances.begin(), reordered_instances.end());
    const auto reordered = runtime::build_runtime_ray_tracing_scene_plan(reordered_instances, meshes);
    assert(reordered); assert(reordered.blases == plan.blases); assert(reordered.instances == plan.instances);
    assert(reordered.scene_instance_indices != plan.scene_instance_indices);
    for (std::size_t index = 0U; index < reordered.instances.size(); ++index)
        assert(reordered_instances[reordered.scene_instance_indices[index]].entity == reordered.instances[index].entity);

    auto changed_meshes = meshes; changed_meshes[1].asset.source_fingerprint = 999U;
    const auto changed = runtime::build_runtime_ray_tracing_scene_plan(instances, changed_meshes); assert(changed); assert(changed.blases[0].key != plan.blases[0].key);
    const std::array missing_instance{instance(4U,9U)}; const auto missing = runtime::build_runtime_ray_tracing_scene_plan(missing_instance, meshes); assert(!missing); assert(!missing.diagnostics.empty());
    auto malformed_meshes = meshes; malformed_meshes[1].asset.indices.clear(); const std::array malformed_instance{instance(5U,1U)};
    const auto malformed = runtime::build_runtime_ray_tracing_scene_plan(malformed_instance, malformed_meshes); assert(!malformed); assert(!malformed.diagnostics.empty());
    const std::array duplicate_entities{instance(6U,1U), instance(6U,2U)};
    const auto duplicate = runtime::build_runtime_ray_tracing_scene_plan(duplicate_entities, meshes); assert(!duplicate); assert(!duplicate.diagnostics.empty());

    runtime::RuntimeResourceResidencyPlan only_mesh_one; only_mesh_one.resources = {resource_id(1U)};
    const auto resident_mesh_one = runtime::filter_runtime_resident_geometry(instances, only_mesh_one); assert(resident_mesh_one.size() == 2U);
    const auto resident_plan = runtime::build_runtime_ray_tracing_scene_plan(resident_mesh_one, meshes);
    assert(resident_plan); assert(resident_plan.blases.size() == 1U); assert(resident_plan.instances.size() == 2U); assert(resident_plan.scene_instance_indices.size() == 2U);
    assert(resident_plan.blases[0].key.mesh == resource_id(1U));
    assert(std::none_of(resident_plan.instances.begin(), resident_plan.instances.end(), [&](const auto& planned) { return planned.entity == persistent_id(2U); }));
    for (std::size_t index = 0U; index < resident_plan.instances.size(); ++index)
        assert(resident_mesh_one[resident_plan.scene_instance_indices[index]].entity == resident_plan.instances[index].entity);

    runtime::RuntimeResourceResidencyPlan all_meshes; all_meshes.resources = {resource_id(1U), resource_id(2U)};
    const auto streamed_back = runtime::filter_runtime_resident_geometry(instances, all_meshes);
    const auto streamed_back_plan = runtime::build_runtime_ray_tracing_scene_plan(streamed_back, meshes); assert(streamed_back_plan);
    const auto before_identity = std::find_if(plan.instances.begin(), plan.instances.end(), [&](const auto& planned) { return planned.entity == persistent_id(2U); });
    const auto after_identity = std::find_if(streamed_back_plan.instances.begin(), streamed_back_plan.instances.end(), [&](const auto& planned) { return planned.entity == persistent_id(2U); });
    assert(before_identity != plan.instances.end()); assert(after_identity != streamed_back_plan.instances.end()); assert(before_identity->instance_id == after_identity->instance_id);

    auto reordered_resident = streamed_back; std::reverse(reordered_resident.begin(), reordered_resident.end());
    const auto reordered_resident_plan = runtime::build_runtime_ray_tracing_scene_plan(reordered_resident, meshes);
    assert(reordered_resident_plan); assert(reordered_resident_plan.blases == streamed_back_plan.blases); assert(reordered_resident_plan.instances == streamed_back_plan.instances);
    for (std::size_t index = 0U; index < reordered_resident_plan.instances.size(); ++index)
        assert(reordered_resident[reordered_resident_plan.scene_instance_indices[index]].entity == reordered_resident_plan.instances[index].entity);
    return 0;
}
