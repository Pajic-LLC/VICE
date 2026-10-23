#include "gpu_pbr_batches.hpp"

#include <vice/render/material_asset.hpp>
#include <vice/runtime/virtual_geometry.hpp>

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <utility>

namespace vice::runtime::detail {
namespace {

[[nodiscard]] const CookedMeshRecord* find_mesh(
    const std::span<const CookedMeshRecord> meshes,
    const world::ResourceId resource_id
) noexcept {
    const auto found = std::find_if(meshes.begin(), meshes.end(), [=](const auto& record) {
        return record.resource_id == resource_id;
    });
    return found == meshes.end() ? nullptr : &*found;
}

[[nodiscard]] const CookedRenderMaterialRecord* find_material(
    const std::span<const CookedRenderMaterialRecord> materials,
    const world::ResourceId resource_id
) noexcept {
    const auto found = std::find_if(materials.begin(), materials.end(), [=](const auto& record) {
        return record.resource_id == resource_id;
    });
    return found == materials.end() ? nullptr : &*found;
}

} // namespace

RuntimeGpuPbrBatchBuildResult build_runtime_gpu_pbr_batch_plan(
    const std::span<const RenderMeshInstance> instances,
    const std::span<const CookedMeshRecord> meshes,
    const std::span<const CookedRenderMaterialRecord> materials
) {
    RuntimeGpuPbrBatchBuildResult result;
    if (instances.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime GPU PBR scene instance count exceeds the GPU batch contract.";
        return result;
    }

    result.plan.instance_batches.assign(
        instances.size(), runtime_gpu_draw_batch_unassigned
    );
    std::unordered_map<
        RuntimeGeometryVariantKey,
        std::uint32_t,
        RuntimeGeometryVariantKeyHash
    > batch_lookup;

    for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
        const auto& instance = instances[instance_index];
        if (!instance.material.has_value()) continue;

        const auto* const material = find_material(materials, *instance.material);
        if (material == nullptr) {
            result.plan = {};
            result.diagnostics = "Runtime GPU PBR instance references unknown material `" +
                instance.material->value.to_string() + "`.";
            return result;
        }
        if (!render::render_material_uses_gpu_scene_pbr(material->asset.version)) continue;

        const auto* const mesh = find_mesh(meshes, instance.mesh);
        if (mesh == nullptr) {
            result.plan = {};
            result.diagnostics = "Runtime GPU PBR instance references unknown mesh `" +
                instance.mesh.value.to_string() + "`.";
            return result;
        }

        const auto key = runtime_geometry_variant_key(instance);
        auto found = batch_lookup.find(key);
        if (found == batch_lookup.end()) {
            std::string draw_diagnostics;
            const auto draw = runtime_virtual_geometry_draw_batch(
                mesh->asset,
                instance.geometry_node_index,
                &draw_diagnostics
            );
            if (!draw.has_value()) {
                result.plan = {};
                result.diagnostics = "Runtime GPU PBR virtual geometry batch is invalid: " +
                    draw_diagnostics;
                return result;
            }
            const std::uint32_t batch_index = static_cast<std::uint32_t>(result.plan.batches.size());
            result.plan.batches.push_back(*draw);
            result.plan.batch_meshes.push_back(instance.mesh);
            found = batch_lookup.emplace(key, batch_index).first;
        }
        result.plan.instance_batches[instance_index] = found->second;
    }

    return result;
}

} // namespace vice::runtime::detail
