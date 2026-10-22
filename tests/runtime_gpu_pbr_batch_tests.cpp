#include "gpu_pbr_batches.hpp"

#include <vice/runtime/gpu_instance_materials.hpp>

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] vice::runtime::CookedRenderMaterialRecord material_record(
    const char* const id,
    const std::uint32_t version
) {
    vice::runtime::CookedRenderMaterialRecord record;
    record.resource_id = vice::world::ResourceId{required_id(id)};
    record.asset.version = version;
    record.asset.stable_id = record.resource_id.value.bytes();
    return record;
}

} // namespace

int main() {
    using namespace vice;
    using namespace vice::runtime;

    const world::ResourceId mesh_a{required_id("60000000-0000-4000-8000-000000000001")};
    const world::ResourceId mesh_b{required_id("60000000-0000-4000-8000-000000000002")};
    const world::ResourceId material_a{required_id("70000000-0000-4000-8000-000000000001")};
    const world::ResourceId material_b{required_id("70000000-0000-4000-8000-000000000002")};
    const world::ResourceId legacy_material{required_id("70000000-0000-4000-8000-000000000003")};
    const auto texture_a = required_id("71000000-0000-4000-8000-000000000001");
    const auto texture_b = required_id("71000000-0000-4000-8000-000000000002");

    std::vector<CookedMeshRecord> meshes(2U);
    meshes[0].resource_id = mesh_a;
    meshes[0].asset.indices = {0U, 1U, 2U};
    meshes[1].resource_id = mesh_b;
    meshes[1].asset.indices = {0U, 1U, 2U, 2U, 3U, 0U};

    std::vector<CookedRenderMaterialRecord> materials;
    auto gpu_material_a = material_record(
        "70000000-0000-4000-8000-000000000001",
        render::render_material_asset_format_version_v4
    );
    gpu_material_a.asset.base_color_texture = texture_a.bytes();
    gpu_material_a.asset.normal_texture = texture_a.bytes();
    gpu_material_a.asset.orm_texture = texture_a.bytes();
    gpu_material_a.asset.emissive_texture = texture_a.bytes();
    materials.push_back(std::move(gpu_material_a));

    auto gpu_material_b = material_record(
        "70000000-0000-4000-8000-000000000002",
        render::render_material_asset_format_version_v4
    );
    gpu_material_b.asset.base_color_texture = texture_b.bytes();
    gpu_material_b.asset.normal_texture = texture_b.bytes();
    gpu_material_b.asset.orm_texture = texture_b.bytes();
    gpu_material_b.asset.emissive_texture = texture_b.bytes();
    materials.push_back(std::move(gpu_material_b));

    materials.push_back(material_record(
        "70000000-0000-4000-8000-000000000003",
        render::render_material_asset_format_version_v3
    ));

    std::vector<RenderMeshInstance> instances(5U);
    instances[0].mesh = mesh_a;
    instances[0].material = material_a;
    instances[1].mesh = mesh_a;
    instances[1].material = material_b;
    instances[2].mesh = mesh_a;
    instances[2].material = legacy_material;
    instances[3].mesh = mesh_a;
    instances[3].material.reset();
    instances[4].mesh = mesh_b;
    instances[4].material = material_b;

    const auto batches = detail::build_runtime_gpu_pbr_batch_plan(instances, meshes, materials);
    assert(batches);
    assert(batches.plan.instance_batches.size() == instances.size());
    assert(batches.plan.batches.size() == 2U);
    assert(batches.plan.batch_meshes.size() == 2U);
    assert(batches.plan.batch_meshes[0] == mesh_a);
    assert(batches.plan.batch_meshes[1] == mesh_b);
    assert(batches.plan.batches[0].index_count == 3U);
    assert(batches.plan.batches[1].index_count == 6U);
    assert(batches.plan.instance_batches[0] == 0U);
    assert(batches.plan.instance_batches[1] == 0U);
    assert(batches.plan.instance_batches[2] == runtime_gpu_draw_batch_unassigned);
    assert(batches.plan.instance_batches[3] == runtime_gpu_draw_batch_unassigned);
    assert(batches.plan.instance_batches[4] == 1U);

    const auto material_table = build_runtime_gpu_material_table_plan(
        RuntimeGpuMaterialTableBuildInput{
            .materials = materials,
            .validate_texture_references = false,
        }
    );
    assert(material_table);
    const auto material_a_index = material_table.plan.material_index(material_a);
    const auto material_b_index = material_table.plan.material_index(material_b);
    const auto legacy_material_index = material_table.plan.material_index(legacy_material);
    assert(material_a_index.has_value());
    assert(material_b_index.has_value());
    assert(legacy_material_index.has_value());
    assert(material_table.plan.records[*material_a_index].base_color_texture !=
        material_table.plan.records[*material_b_index].base_color_texture);
    assert(material_table.plan.texture_ids.size() == 2U);

    const auto instance_materials = build_runtime_gpu_instance_material_records(
        instances,
        material_table.plan
    );
    assert(instance_materials);
    assert(instance_materials.records.size() == instances.size());
    assert(instance_materials.records[0].material_index == *material_a_index);
    assert(instance_materials.records[1].material_index == *material_b_index);
    assert(instance_materials.records[2].material_index == *legacy_material_index);
    assert(instance_materials.records[3].material_index == runtime_gpu_resource_unassigned);
    assert(instance_materials.records[4].material_index == *material_b_index);
    assert(batches.plan.instance_batches[0] == batches.plan.instance_batches[1]);
    assert(instance_materials.records[0].material_index != instance_materials.records[1].material_index);
    assert(batches.plan.instance_batches[2] == runtime_gpu_draw_batch_unassigned);

    auto missing_material_instances = instances;
    missing_material_instances[0].material = world::ResourceId{
        required_id("70000000-0000-4000-8000-000000000099")
    };
    const auto missing_material = detail::build_runtime_gpu_pbr_batch_plan(
        missing_material_instances, meshes, materials
    );
    assert(!missing_material);

    auto missing_mesh_instances = instances;
    missing_mesh_instances[0].mesh = world::ResourceId{
        required_id("60000000-0000-4000-8000-000000000099")
    };
    const auto missing_mesh = detail::build_runtime_gpu_pbr_batch_plan(
        missing_mesh_instances, meshes, materials
    );
    assert(!missing_mesh);

    return 0;
}
