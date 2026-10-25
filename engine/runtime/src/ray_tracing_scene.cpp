#include <vice/runtime/ray_tracing_scene.hpp>

#include <vice/render/ray_tracing.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace vice::runtime {
namespace {

[[nodiscard]] bool persistent_less(const world::PersistentId& left, const world::PersistentId& right) noexcept {
    return std::lexicographical_compare(left.bytes().begin(), left.bytes().end(), right.bytes().begin(), right.bytes().end());
}

[[nodiscard]] bool resource_less(const world::ResourceId& left, const world::ResourceId& right) noexcept {
    return persistent_less(left.value, right.value);
}

[[nodiscard]] const CookedMeshRecord* find_mesh(const std::span<const CookedMeshRecord> meshes, const world::ResourceId resource) noexcept {
    const auto found = std::find_if(meshes.begin(), meshes.end(), [&](const auto& mesh) { return mesh.resource_id == resource; });
    return found == meshes.end() ? nullptr : &*found;
}

[[nodiscard]] bool valid_mesh_for_ray_tracing(const CookedMeshRecord& mesh) noexcept {
    if (!mesh.resource_id.valid() || mesh.asset.vertices.size() < 3U || mesh.asset.indices.size() < 3U ||
        mesh.asset.indices.size() % 3U != 0U || mesh.asset.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        mesh.asset.indices.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    return std::all_of(mesh.asset.indices.begin(), mesh.asset.indices.end(), [&](const auto index) {
        return static_cast<std::size_t>(index) < mesh.asset.vertices.size();
    });
}

[[nodiscard]] RuntimeRayTracingBlasKey blas_key(const CookedMeshRecord& mesh) noexcept {
    return RuntimeRayTracingBlasKey{.mesh = mesh.resource_id, .stable_id = mesh.asset.stable_id,
        .source_fingerprint = mesh.asset.source_fingerprint,
        .vertex_count = static_cast<std::uint32_t>(mesh.asset.vertices.size()),
        .index_count = static_cast<std::uint32_t>(mesh.asset.indices.size())};
}

[[nodiscard]] std::uint32_t persistent_id_hash24(const world::PersistentId& id) noexcept {
    std::uint32_t hash = 2166136261U;
    for (const auto byte : id.bytes()) { hash ^= static_cast<std::uint32_t>(byte); hash *= 16777619U; }
    hash ^= hash >> 16U;
    return hash & render::acceleration_structure_instance_id_max;
}

[[nodiscard]] std::array<float, 12> portable_transform(
    const RenderMeshInstance& instance,
    const std::optional<world::GlobalPosition> origin
) noexcept {
    const math::Mat4 model = origin.has_value()
        ? runtime_model_matrix_relative_to(instance, *origin)
        : runtime_model_matrix(instance);
    return {model(0U,0U), model(0U,1U), model(0U,2U), model(0U,3U), model(1U,0U), model(1U,1U),
        model(1U,2U), model(1U,3U), model(2U,0U), model(2U,1U), model(2U,2U), model(2U,3U)};
}

RuntimeRayTracingScenePlan build_plan(
    const std::span<const RenderMeshInstance> instances,
    const std::span<const CookedMeshRecord> meshes,
    const std::optional<world::GlobalPosition> origin
) {
    RuntimeRayTracingScenePlan result;
    if (instances.empty()) return result;
    if (instances.size() > static_cast<std::size_t>(render::acceleration_structure_instance_id_max) + 1U ||
        instances.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        result.diagnostics = "Runtime ray tracing scene exceeds the portable instance-index range.";
        return result;
    }
    if (origin.has_value() && !world::finite(*origin)) {
        result.diagnostics = "Runtime ray tracing scene received a non-finite camera-relative origin.";
        return result;
    }

    std::vector<const RenderMeshInstance*> ordered_instances;
    ordered_instances.reserve(instances.size());
    for (const auto& instance : instances) {
        if (instance.entity.is_nil() || !instance.mesh.valid()) {
            result.diagnostics = "Runtime ray tracing scene contains an invalid entity or mesh identity."; return result;
        }
        const auto* mesh = find_mesh(meshes, instance.mesh);
        if (mesh == nullptr) {
            result.diagnostics = "Runtime ray tracing scene could not resolve mesh `" + instance.mesh.value.to_string() + "`."; return result;
        }
        if (!valid_mesh_for_ray_tracing(*mesh)) {
            result.diagnostics = "Runtime ray tracing scene rejected malformed mesh `" + instance.mesh.value.to_string() + "`."; return result;
        }
        ordered_instances.push_back(&instance);
    }

    std::sort(ordered_instances.begin(), ordered_instances.end(), [](const auto* left, const auto* right) {
        return persistent_less(left->entity, right->entity);
    });
    for (std::size_t index = 1U; index < ordered_instances.size(); ++index) {
        if (ordered_instances[index - 1U]->entity == ordered_instances[index]->entity) {
            result.diagnostics = "Runtime ray tracing scene contains a duplicate persistent entity ID."; return result;
        }
    }

    std::vector<world::ResourceId> resources;
    resources.reserve(instances.size());
    for (const auto* instance : ordered_instances) {
        if (std::find(resources.begin(), resources.end(), instance->mesh) == resources.end()) resources.push_back(instance->mesh);
    }
    std::sort(resources.begin(), resources.end(), resource_less);

    result.blases.reserve(resources.size());
    for (const auto resource : resources) {
        const auto* mesh = find_mesh(meshes, resource);
        if (mesh == nullptr || !valid_mesh_for_ray_tracing(*mesh)) {
            result.diagnostics = "Runtime ray tracing BLAS planning lost an authoritative mesh record."; result.blases.clear(); return result;
        }
        result.blases.push_back(RuntimeRayTracingBlasPlan{.key = blas_key(*mesh)});
    }

    std::vector<std::uint32_t> used_ids;
    used_ids.reserve(ordered_instances.size());
    result.instances.reserve(ordered_instances.size());
    result.scene_instance_indices.reserve(ordered_instances.size());
    for (const auto* instance : ordered_instances) {
        const auto blas = std::find_if(result.blases.begin(), result.blases.end(), [&](const auto& candidate) {
            return candidate.key.mesh == instance->mesh;
        });
        if (blas == result.blases.end()) {
            result.diagnostics = "Runtime ray tracing instance could not resolve its planned BLAS.";
            result.blases.clear(); result.instances.clear(); result.scene_instance_indices.clear(); return result;
        }

        std::uint32_t instance_id = persistent_id_hash24(instance->entity);
        while (std::find(used_ids.begin(), used_ids.end(), instance_id) != used_ids.end()) {
            instance_id = instance_id == render::acceleration_structure_instance_id_max ? 0U : instance_id + 1U;
        }
        used_ids.push_back(instance_id);

        result.instances.push_back(RuntimeRayTracingInstancePlan{.entity = instance->entity, .instance_id = instance_id,
            .blas_index = static_cast<std::uint32_t>(blas - result.blases.begin()), .transform = portable_transform(*instance, origin)});
        result.scene_instance_indices.push_back(static_cast<std::uint32_t>(instance - instances.data()));
    }

    return result;
}

} // namespace

RuntimeRayTracingScenePlan build_runtime_ray_tracing_scene_plan(
    const std::span<const RenderMeshInstance> instances,
    const std::span<const CookedMeshRecord> meshes
) {
    return build_plan(instances, meshes, std::nullopt);
}

RuntimeRayTracingScenePlan build_runtime_ray_tracing_scene_plan(
    const std::span<const RenderMeshInstance> instances,
    const std::span<const CookedMeshRecord> meshes,
    const world::GlobalPosition origin
) {
    return build_plan(instances, meshes, origin);
}

} // namespace vice::runtime
