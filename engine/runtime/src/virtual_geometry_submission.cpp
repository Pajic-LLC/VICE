#include <vice/runtime/virtual_geometry_submission.hpp>

#include <vice/runtime/virtual_geometry.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
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

[[nodiscard]] const CookedMeshRecord* find_mesh(
    const std::span<const CookedMeshRecord> meshes,
    const world::ResourceId resource
) noexcept {
    const auto found = std::find_if(meshes.begin(), meshes.end(), [&](const auto& mesh) {
        return mesh.resource_id == resource;
    });
    return found == meshes.end() ? nullptr : &*found;
}

[[nodiscard]] const RuntimeGpuVirtualGeometryNodeRecord* find_plan_node(
    const RuntimeGpuVirtualGeometryPlan& plan,
    const std::uint32_t mesh_index,
    const std::uint32_t local_node_index
) noexcept {
    const auto found = std::find_if(plan.nodes.begin(), plan.nodes.end(), [&](const auto& node) {
        return node.mesh_index == mesh_index && node.local_node_index == local_node_index;
    });
    return found == plan.nodes.end() ? nullptr : &*found;
}

[[nodiscard]] bool valid_local_geometry(
    const std::span<const render::MeshVertex> vertices,
    const std::span<const std::uint32_t> indices
) noexcept {
    if (vertices.empty() || indices.empty() || indices.size() % 3U != 0U ||
        vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) ||
        indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    for (std::size_t offset = 0U; offset < indices.size(); offset += 3U) {
        const auto a = indices[offset];
        const auto b = indices[offset + 1U];
        const auto c = indices[offset + 2U];
        if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size() ||
            a == b || b == c || a == c) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] render::MeshAsset make_runtime_asset(
    const CookedMeshRecord& source,
    const world::ResourceId resource,
    const std::span<const render::MeshVertex> vertices,
    const std::span<const std::uint32_t> indices
) {
    render::MeshAsset asset;
    asset.version = render::mesh_asset_format_version_v3;
    asset.source_fingerprint = source.asset.source_fingerprint;
    const auto stable = resource.value.bytes();
    std::copy(stable.begin(), stable.end(), asset.stable_id.begin());
    asset.vertices.assign(vertices.begin(), vertices.end());
    asset.indices.assign(indices.begin(), indices.end());
    return asset;
}

[[nodiscard]] const render::VirtualGeometryProxyMesh* find_proxy(
    const render::MeshAsset& asset,
    const std::uint32_t node_index
) noexcept {
    const auto found = std::find_if(
        asset.virtual_geometry_proxies.proxies.begin(),
        asset.virtual_geometry_proxies.proxies.end(),
        [=](const auto& proxy) { return proxy.node_index == node_index; }
    );
    return found == asset.virtual_geometry_proxies.proxies.end() ? nullptr : &*found;
}

[[nodiscard]] RuntimeVirtualGeometrySubmissionGeometry* find_geometry(
    std::vector<RuntimeVirtualGeometrySubmissionGeometry>& geometry,
    const world::ResourceId resource
) noexcept {
    const auto found = std::find_if(geometry.begin(), geometry.end(), [&](const auto& item) {
        return item.resource_id == resource;
    });
    return found == geometry.end() ? nullptr : &*found;
}

} // namespace

RuntimeVirtualGeometrySubmissionResult build_runtime_virtual_geometry_submission(
    const std::span<const RenderMeshInstance> source_instances,
    const std::span<const CookedMeshRecord> meshes,
    const RuntimeGpuVirtualGeometryPlan& plan,
    const RuntimeGpuVirtualGeometryFeedbackResult& feedback,
    RuntimeVirtualGeometryResidentPool& resident_pool
) {
    RuntimeVirtualGeometrySubmissionResult result;
    if (source_instances.size() != plan.scene_instance_count) {
        result.diagnostics = "Virtual geometry submission instance count does not match the GPU plan.";
        return result;
    }

    const auto validated = validate_runtime_gpu_virtual_geometry_feedback(
        feedback.node_requests,
        feedback.page_requests,
        plan
    );
    if (!validated) {
        result.diagnostics = "Virtual geometry submission rejected GPU feedback: " + validated.diagnostics;
        return result;
    }

    const auto page_requests = build_runtime_virtual_geometry_page_requests(
        validated.page_requests,
        plan
    );
    if (!page_requests) {
        result.diagnostics = "Virtual geometry submission rejected page residency requests: " +
            page_requests.diagnostics;
        return result;
    }
    result.residency = resident_pool.update(page_requests.requests, meshes);
    if (!result.residency) {
        result.diagnostics = result.residency.diagnostics;
        return result;
    }

    result.instances.reserve(source_instances.size() + validated.node_requests.size());
    result.geometry.reserve(validated.node_requests.size());
    std::size_t request_cursor = 0U;
    for (std::size_t instance_index = 0U; instance_index < source_instances.size(); ++instance_index) {
        const auto& source_instance = source_instances[instance_index];
        const auto mesh_index = plan.mesh_index(source_instance.mesh);
        if (!mesh_index.has_value()) {
            result.instances.push_back(source_instance);
            continue;
        }

        while (request_cursor < validated.node_requests.size() &&
               validated.node_requests[request_cursor].scene_instance_index < instance_index) {
            ++request_cursor;
        }
        std::size_t local_cursor = request_cursor;
        while (local_cursor < validated.node_requests.size() &&
               validated.node_requests[local_cursor].scene_instance_index == instance_index) {
            const auto& request = validated.node_requests[local_cursor];
            if (request.mesh_index != *mesh_index || request.mesh_index >= plan.mesh_ids.size() ||
                plan.mesh_ids[request.mesh_index] != source_instance.mesh) {
                result.instances.clear();
                result.geometry.clear();
                result.diagnostics = "Virtual geometry submission feedback does not match the source instance mesh.";
                return result;
            }
            const auto* const node = find_plan_node(plan, request.mesh_index, request.node_index);
            const auto* const source = find_mesh(meshes, source_instance.mesh);
            if (node == nullptr || source == nullptr || source->runtime_generated || source->path.empty() ||
                source->asset.version < render::mesh_asset_format_version_v7) {
                result.instances.clear();
                result.geometry.clear();
                result.diagnostics = "Virtual geometry submission requires an authored file-backed v7 source mesh.";
                return result;
            }

            const std::uint32_t local_node_index = node->local_node_index;
            const auto resource = runtime_virtual_geometry_variant_resource_id(
                source_instance.mesh,
                local_node_index
            );
            auto* geometry = find_geometry(result.geometry, resource);
            if (geometry != nullptr &&
                (geometry->source_mesh != source_instance.mesh || geometry->node_index != local_node_index)) {
                result.instances.clear();
                result.geometry.clear();
                result.diagnostics = "Virtual geometry submission resource ID collided across source nodes.";
                return result;
            }
            if (geometry == nullptr) {
                RuntimeVirtualGeometrySubmissionGeometry next;
                next.resource_id = resource;
                next.source_mesh = source_instance.mesh;
                next.node_index = local_node_index;
                if (node->child_count == 0U) {
                    if (node->page_count != 1U) {
                        result.instances.clear();
                        result.geometry.clear();
                        result.diagnostics = "Virtual geometry submission leaf has invalid page coverage.";
                        return result;
                    }
                    const RuntimeVirtualGeometryPageKey key{
                        .mesh = source_instance.mesh,
                        .page_index = node->page_offset,
                    };
                    const auto* const resident = resident_pool.find(key);
                    if (resident == nullptr ||
                        !valid_local_geometry(resident->geometry.vertices, resident->geometry.indices)) {
                        result.instances.clear();
                        result.geometry.clear();
                        result.diagnostics = "Virtual geometry submission leaf page is not resident or renderable.";
                        return result;
                    }
                    next.page_index = node->page_offset;
                    next.asset = make_runtime_asset(
                        *source,
                        resource,
                        resident->geometry.vertices,
                        resident->geometry.indices
                    );
                } else {
                    const auto* const proxy = find_proxy(source->asset, local_node_index);
                    if (proxy == nullptr || !valid_local_geometry(proxy->vertices, proxy->indices)) {
                        result.instances.clear();
                        result.geometry.clear();
                        result.diagnostics = "Virtual geometry submission internal node has no renderable persisted proxy.";
                        return result;
                    }
                    next.asset = make_runtime_asset(
                        *source,
                        resource,
                        proxy->vertices,
                        proxy->indices
                    );
                }
                result.geometry.push_back(std::move(next));
            }

            auto draw_instance = source_instance;
            draw_instance.mesh = resource;
            draw_instance.geometry_node_index = render::virtual_geometry_invalid_node;
            result.instances.push_back(std::move(draw_instance));
            ++local_cursor;
        }
        request_cursor = local_cursor;
    }

    if (request_cursor != validated.node_requests.size()) {
        result.instances.clear();
        result.geometry.clear();
        result.diagnostics = "Virtual geometry submission contains feedback for a missing scene instance.";
        return result;
    }

    std::sort(result.geometry.begin(), result.geometry.end(), [](const auto& left, const auto& right) {
        if (left.source_mesh != right.source_mesh) return resource_less(left.source_mesh, right.source_mesh);
        if (left.node_index != right.node_index) return left.node_index < right.node_index;
        return resource_less(left.resource_id, right.resource_id);
    });
    return result;
}

} // namespace vice::runtime
