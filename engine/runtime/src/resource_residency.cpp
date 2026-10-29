#include <vice/runtime/resource_residency.hpp>

#include <vice/render/material_asset.hpp>
#include <vice/render/texture_asset.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>

namespace vice::runtime {
namespace {

[[nodiscard]] bool resource_less(
    const world::ResourceId left,
    const world::ResourceId right
) noexcept {
    return std::lexicographical_compare(
        left.value.bytes().begin(), left.value.bytes().end(),
        right.value.bytes().begin(), right.value.bytes().end()
    );
}

[[nodiscard]] int fidelity_priority(const world::RegionFidelity fidelity) noexcept {
    switch (fidelity) {
    case world::RegionFidelity::full: return 2;
    case world::RegionFidelity::reduced: return 1;
    case world::RegionFidelity::inactive: return 0;
    }
    return 0;
}

[[nodiscard]] world::RegionFidelity higher_fidelity(
    const world::RegionFidelity left,
    const world::RegionFidelity right
) noexcept {
    return fidelity_priority(left) >= fidelity_priority(right) ? left : right;
}

[[nodiscard]] std::optional<world::ResourceId> texture_resource(
    const render::TextureStableId& stable_id
) noexcept {
    if (!render::valid_texture_id(stable_id)) return std::nullopt;
    return world::ResourceId{world::PersistentId{stable_id}};
}

[[nodiscard]] bool add_size(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& output
) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) return false;
    output = left + right;
    return true;
}

} // namespace

bool RuntimeResourceResidencyPlan::resident(const world::ResourceId resource) const noexcept {
    return std::binary_search(resources.begin(), resources.end(), resource, resource_less);
}

RuntimeResourceResidencyBuildResult build_runtime_resource_residency_plan(
    const std::span<const RuntimeResourceResidencyCandidate> candidates,
    const std::uint64_t budget_bytes
) {
    RuntimeResourceResidencyBuildResult result;
    result.plan.budget_bytes = budget_bytes;

    std::unordered_map<
        world::ResourceId,
        RuntimeResourceResidencyCandidate,
        world::ResourceIdHash
    > unique;
    unique.reserve(candidates.size());

    for (const auto& candidate : candidates) {
        if (!candidate.resource.valid() || candidate.bytes == 0U ||
            candidate.fidelity == world::RegionFidelity::inactive) {
            result.diagnostics = "Runtime resource residency received an invalid candidate.";
            return result;
        }
        const auto found = unique.find(candidate.resource);
        if (found == unique.end()) {
            unique.emplace(candidate.resource, candidate);
            continue;
        }
        if (found->second.bytes != candidate.bytes) {
            result.diagnostics = "Runtime resource residency received conflicting byte sizes for `" +
                candidate.resource.value.to_string() + "`.";
            return result;
        }
        found->second.fidelity = higher_fidelity(found->second.fidelity, candidate.fidelity);
    }

    std::vector<RuntimeResourceResidencyCandidate> ordered;
    ordered.reserve(unique.size());
    for (const auto& entry : unique) ordered.push_back(entry.second);
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        const int left_priority = fidelity_priority(left.fidelity);
        const int right_priority = fidelity_priority(right.fidelity);
        if (left_priority != right_priority) return left_priority > right_priority;
        return resource_less(left.resource, right.resource);
    });

    std::vector<world::ResourceId> admitted;
    admitted.reserve(ordered.size());
    std::uint64_t used = 0U;
    for (const auto& candidate : ordered) {
        if (candidate.bytes > budget_bytes - used) continue;
        used += candidate.bytes;
        admitted.push_back(candidate.resource);
    }
    std::sort(admitted.begin(), admitted.end(), resource_less);
    result.plan.resources = std::move(admitted);
    result.plan.resident_bytes = used;
    return result;
}

RuntimeRenderResourceResidencyBuildResult build_runtime_render_resource_residency(
    const RuntimeRenderResidentScene& scene,
    const CookedMeshCatalog& meshes,
    const CookedRenderMaterialCatalog& materials,
    const CookedTextureCatalog& textures,
    const RuntimeRenderResourceBudgets budgets
) {
    RuntimeRenderResourceResidencyBuildResult result;

    std::vector<RuntimeResourceResidencyCandidate> geometry_candidates;
    geometry_candidates.reserve(scene.meshes.size());
    for (const auto& instance : scene.meshes) {
        const auto fidelity = scene.residency.fidelity(instance.spatial.region);
        if (!fidelity.has_value() || *fidelity == world::RegionFidelity::inactive) {
            result.diagnostics = "Runtime resident mesh references a nonresident region.";
            return result;
        }
        const auto* const mesh = meshes.find(instance.mesh);
        if (mesh == nullptr) {
            result.diagnostics = "Runtime World references missing cooked mesh `" +
                instance.mesh.value.to_string() + "`.";
            return result;
        }
        std::uint64_t vertex_bytes = static_cast<std::uint64_t>(mesh->asset.vertices.size()) *
            static_cast<std::uint64_t>(sizeof(render::MeshVertex));
        std::uint64_t index_bytes = static_cast<std::uint64_t>(mesh->asset.indices.size()) *
            static_cast<std::uint64_t>(sizeof(std::uint32_t));
        std::uint64_t total_bytes = 0U;
        if (!add_size(vertex_bytes, index_bytes, total_bytes) ||
            !add_size(total_bytes, mesh->additional_resident_geometry_bytes, total_bytes) ||
            total_bytes == 0U) {
            result.diagnostics = "Runtime cooked mesh has invalid residency byte size `" +
                instance.mesh.value.to_string() + "`.";
            return result;
        }
        geometry_candidates.push_back(RuntimeResourceResidencyCandidate{
            .resource = instance.mesh,
            .bytes = total_bytes,
            .fidelity = *fidelity,
        });
    }

    auto geometry = build_runtime_resource_residency_plan(
        geometry_candidates,
        budgets.geometry_bytes
    );
    if (!geometry) {
        result.diagnostics = std::move(geometry.diagnostics);
        return result;
    }
    result.residency.geometry = std::move(geometry.plan);

    std::vector<RuntimeResourceResidencyCandidate> texture_candidates;
    for (const auto& instance : scene.meshes) {
        if (!result.residency.geometry.resident(instance.mesh) || !instance.material.has_value()) {
            continue;
        }
        const auto fidelity = scene.residency.fidelity(instance.spatial.region);
        if (!fidelity.has_value() || *fidelity == world::RegionFidelity::inactive) continue;

        const auto* const material = materials.find(*instance.material);
        if (material == nullptr) {
            result.diagnostics = "Runtime World references missing cooked material `" +
                instance.material->value.to_string() + "`.";
            return result;
        }
        const std::array texture_ids{
            material->asset.base_color_texture,
            material->asset.normal_texture,
            material->asset.orm_texture,
            material->asset.emissive_texture,
        };
        for (const auto& stable_id : texture_ids) {
            const auto resource = texture_resource(stable_id);
            if (!resource.has_value()) continue;
            const auto* const texture = textures.find(*resource);
            if (texture == nullptr) {
                result.diagnostics = "Runtime material references missing cooked texture `" +
                    resource->value.to_string() + "`.";
                return result;
            }
            const std::uint64_t bytes = static_cast<std::uint64_t>(texture->asset.rgba8.size());
            if (bytes == 0U) {
                result.diagnostics = "Runtime cooked texture has invalid residency byte size `" +
                    resource->value.to_string() + "`.";
                return result;
            }
            texture_candidates.push_back(RuntimeResourceResidencyCandidate{
                .resource = *resource,
                .bytes = bytes,
                .fidelity = *fidelity,
            });
        }
    }

    auto texture_plan = build_runtime_resource_residency_plan(
        texture_candidates,
        budgets.texture_bytes
    );
    if (!texture_plan) {
        result.diagnostics = std::move(texture_plan.diagnostics);
        return result;
    }
    result.residency.textures = std::move(texture_plan.plan);
    return result;
}

std::vector<RenderMeshInstance> filter_runtime_resident_geometry(
    const std::span<const RenderMeshInstance> instances,
    const RuntimeResourceResidencyPlan& geometry
) {
    std::vector<RenderMeshInstance> filtered;
    filtered.reserve(instances.size());
    for (const auto& instance : instances) {
        if (geometry.resident(instance.mesh)) filtered.push_back(instance);
    }
    return filtered;
}

} // namespace vice::runtime
