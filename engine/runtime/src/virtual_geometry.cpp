#include <vice/runtime/virtual_geometry.hpp>

#include <vice/render/virtual_geometry_selection.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace vice::runtime {
namespace {

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] const CookedMeshRecord* find_mesh(
    const std::span<const CookedMeshRecord> meshes,
    const world::ResourceId resource_id
) noexcept {
    const auto found = std::find_if(meshes.begin(), meshes.end(), [=](const auto& record) {
        return record.resource_id == resource_id;
    });
    return found == meshes.end() ? nullptr : &*found;
}

[[nodiscard]] bool valid_authoritative_geometry(const render::MeshAsset& asset) noexcept {
    return !asset.vertices.empty() && !asset.indices.empty() &&
        asset.vertices.size() <= std::numeric_limits<std::uint32_t>::max() &&
        asset.indices.size() <= std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result
) noexcept {
    if (left > std::numeric_limits<std::size_t>::max() - right) return false;
    result = left + right;
    return true;
}

[[nodiscard]] render::VirtualGeometrySelectionTransform selection_transform(
    const RenderMeshInstance& instance,
    const world::GlobalPosition camera_global_position
) noexcept {
    const auto model = runtime_model_matrix_relative_to(instance, camera_global_position);
    render::VirtualGeometrySelectionTransform transform;
    transform.linear = {
        model(0U, 0U), model(0U, 1U), model(0U, 2U),
        model(1U, 0U), model(1U, 1U), model(1U, 2U),
        model(2U, 0U), model(2U, 1U), model(2U, 2U),
    };
    transform.translation = {model(0U, 3U), model(1U, 3U), model(2U, 3U)};
    return transform;
}

[[nodiscard]] render::VirtualGeometrySelectionView selection_view(
    const RuntimeVisibilityFrustum& frustum,
    const float vertical_fov_radians,
    const float viewport_height_pixels,
    const float max_projected_error_pixels
) noexcept {
    render::VirtualGeometrySelectionView view;
    view.camera_position = {0.0F, 0.0F, 0.0F};
    for (std::size_t index = 0U; index < frustum.planes.size(); ++index) {
        view.frustum_planes[index].normal = {
            frustum.planes[index].normal.x,
            frustum.planes[index].normal.y,
            frustum.planes[index].normal.z,
        };
        view.frustum_planes[index].distance = frustum.planes[index].distance;
    }
    view.vertical_fov_radians = vertical_fov_radians;
    view.viewport_height_pixels = viewport_height_pixels;
    view.max_projected_error_pixels = max_projected_error_pixels;
    return view;
}

} // namespace

std::size_t RuntimeGeometryVariantKeyHash::operator()(
    const RuntimeGeometryVariantKey& key
) const noexcept {
    const std::size_t mesh_hash = world::ResourceIdHash{}(key.mesh);
    const std::size_t node_hash = std::hash<std::uint32_t>{}(key.node_index);
    return mesh_hash ^ (node_hash + 0x9e3779b9U + (mesh_hash << 6U) + (mesh_hash >> 2U));
}

RuntimeGeometryVariantKey runtime_geometry_variant_key(
    const RenderMeshInstance& instance
) noexcept {
    return RuntimeGeometryVariantKey{
        .mesh = instance.mesh,
        .node_index = instance.geometry_node_index,
    };
}

RuntimeVirtualGeometryBufferBuildResult build_runtime_virtual_geometry_buffer_plan(
    const render::MeshAsset& asset
) {
    RuntimeVirtualGeometryBufferBuildResult result;
    if (!valid_authoritative_geometry(asset)) {
        result.diagnostics = "Runtime virtual geometry requires non-empty 32-bit authoritative mesh streams.";
        return result;
    }
    if (asset.virtual_geometry_proxies.hierarchy.nodes.empty() &&
        !asset.virtual_geometry_proxies.proxies.empty()) {
        result.diagnostics = "Runtime virtual geometry proxy payload is present without a hierarchy.";
        return result;
    }

    std::size_t total_vertices = asset.vertices.size();
    std::size_t total_indices = asset.indices.size();
    for (const auto& proxy : asset.virtual_geometry_proxies.proxies) {
        if (proxy.vertices.empty() || proxy.indices.empty() || proxy.indices.size() % 3U != 0U ||
            !checked_add(total_vertices, proxy.vertices.size(), total_vertices) ||
            !checked_add(total_indices, proxy.indices.size(), total_indices) ||
            total_vertices > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) ||
            total_indices > std::numeric_limits<std::uint32_t>::max()) {
            result.diagnostics = "Runtime virtual geometry combined proxy streams exceed the draw contract.";
            return result;
        }
        for (const auto index : proxy.indices) {
            if (index >= proxy.vertices.size()) {
                result.diagnostics = "Runtime virtual geometry proxy contains an out-of-range local index.";
                return result;
            }
        }
    }

    RuntimeVirtualGeometryBufferPlan plan;
    plan.vertices.reserve(total_vertices);
    plan.indices.reserve(total_indices);
    plan.vertices.insert(plan.vertices.end(), asset.vertices.begin(), asset.vertices.end());
    plan.indices.insert(plan.indices.end(), asset.indices.begin(), asset.indices.end());
    for (const auto& proxy : asset.virtual_geometry_proxies.proxies) {
        plan.vertices.insert(plan.vertices.end(), proxy.vertices.begin(), proxy.vertices.end());
        plan.indices.insert(plan.indices.end(), proxy.indices.begin(), proxy.indices.end());
    }
    result.plan = std::move(plan);
    return result;
}

std::optional<RuntimeGpuDrawBatch> runtime_virtual_geometry_draw_batch(
    const render::MeshAsset& asset,
    const std::uint32_t node_index,
    std::string* const diagnostics
) {
    if (!valid_authoritative_geometry(asset)) {
        set_diagnostics(diagnostics, "Runtime virtual geometry draw references invalid authoritative streams.");
        return std::nullopt;
    }

    if (node_index == render::virtual_geometry_invalid_node) {
        if (diagnostics != nullptr) diagnostics->clear();
        return RuntimeGpuDrawBatch{
            .index_count = static_cast<std::uint32_t>(asset.indices.size()),
            .first_index = 0U,
            .vertex_offset = 0,
        };
    }

    const auto& hierarchy = asset.virtual_geometry_proxies.hierarchy;
    if (hierarchy.nodes.empty() || node_index >= hierarchy.nodes.size()) {
        set_diagnostics(diagnostics, "Runtime virtual geometry draw references an unknown hierarchy node.");
        return std::nullopt;
    }
    const auto& node = hierarchy.nodes[node_index];
    if (node.child_count == 0U) {
        if (node.page_count != 1U || node.page_offset >= asset.virtual_geometry_pages.pages.size()) {
            set_diagnostics(diagnostics, "Runtime virtual geometry leaf has an invalid authoritative page mapping.");
            return std::nullopt;
        }
        const auto& page = asset.virtual_geometry_pages.pages[node.page_offset];
        const std::uint64_t index_end = static_cast<std::uint64_t>(page.triangle_index_offset) +
            page.triangle_index_count;
        if (page.triangle_index_count == 0U || page.triangle_index_count % 3U != 0U ||
            index_end > asset.indices.size()) {
            set_diagnostics(diagnostics, "Runtime virtual geometry leaf page range exceeds the authoritative index stream.");
            return std::nullopt;
        }
        if (diagnostics != nullptr) diagnostics->clear();
        return RuntimeGpuDrawBatch{
            .index_count = page.triangle_index_count,
            .first_index = page.triangle_index_offset,
            .vertex_offset = 0,
        };
    }

    std::uint64_t vertex_offset = asset.vertices.size();
    std::uint64_t index_offset = asset.indices.size();
    for (const auto& proxy : asset.virtual_geometry_proxies.proxies) {
        if (proxy.node_index == node_index) {
            if (proxy.vertices.empty() || proxy.indices.empty() || proxy.indices.size() % 3U != 0U ||
                vertex_offset > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) ||
                index_offset > std::numeric_limits<std::uint32_t>::max() ||
                proxy.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
                set_diagnostics(diagnostics, "Runtime virtual geometry proxy draw exceeds the indexed draw contract.");
                return std::nullopt;
            }
            for (const auto index : proxy.indices) {
                if (index >= proxy.vertices.size()) {
                    set_diagnostics(diagnostics, "Runtime virtual geometry proxy draw contains an out-of-range index.");
                    return std::nullopt;
                }
            }
            if (diagnostics != nullptr) diagnostics->clear();
            return RuntimeGpuDrawBatch{
                .index_count = static_cast<std::uint32_t>(proxy.indices.size()),
                .first_index = static_cast<std::uint32_t>(index_offset),
                .vertex_offset = static_cast<std::int32_t>(vertex_offset),
            };
        }
        vertex_offset += proxy.vertices.size();
        index_offset += proxy.indices.size();
        if (vertex_offset > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) ||
            index_offset > std::numeric_limits<std::uint32_t>::max()) {
            set_diagnostics(diagnostics, "Runtime virtual geometry proxy stream offsets overflow the draw contract.");
            return std::nullopt;
        }
    }

    set_diagnostics(diagnostics, "Runtime virtual geometry internal node has no persisted proxy payload.");
    return std::nullopt;
}

RuntimeVirtualGeometryInstanceBuildResult select_runtime_virtual_geometry_instances(
    const std::span<const RenderMeshInstance> instances,
    const std::span<const CookedMeshRecord> meshes,
    const RuntimeVisibilityFrustum& frustum,
    const world::GlobalPosition camera_global_position,
    const float vertical_fov_radians,
    const float viewport_height_pixels,
    const float max_projected_error_pixels
) {
    RuntimeVirtualGeometryInstanceBuildResult result;
    if (!valid(frustum) || !std::isfinite(vertical_fov_radians) || vertical_fov_radians <= 0.0F ||
        !std::isfinite(viewport_height_pixels) || viewport_height_pixels <= 0.0F ||
        !std::isfinite(max_projected_error_pixels) || max_projected_error_pixels < 0.0F) {
        result.diagnostics = "Runtime virtual geometry selection received an invalid camera contract.";
        return result;
    }

    const auto view = selection_view(
        frustum,
        vertical_fov_radians,
        viewport_height_pixels,
        max_projected_error_pixels
    );
    result.instances.reserve(instances.size());
    for (const auto& instance : instances) {
        const auto* const mesh = find_mesh(meshes, instance.mesh);
        if (mesh == nullptr) {
            result.instances.clear();
            result.diagnostics = "Runtime virtual geometry selection references an unknown mesh `" +
                instance.mesh.value.to_string() + "`.";
            return result;
        }

        const auto& hierarchy = mesh->asset.virtual_geometry_proxies.hierarchy;
        if (hierarchy.nodes.empty()) {
            auto authoritative = instance;
            authoritative.geometry_node_index = render::virtual_geometry_invalid_node;
            result.instances.push_back(std::move(authoritative));
            continue;
        }

        const auto selected = render::select_virtual_geometry(
            hierarchy,
            view,
            selection_transform(instance, camera_global_position)
        );
        if (!selected) {
            result.instances.clear();
            result.diagnostics = "Runtime virtual geometry selection failed for mesh `" +
                instance.mesh.value.to_string() + "`: " + selected.diagnostics;
            return result;
        }
        for (const auto selected_node : selected.selection->selected_node_indices) {
            if (!runtime_virtual_geometry_draw_batch(mesh->asset, selected_node)) {
                result.instances.clear();
                result.diagnostics = "Runtime virtual geometry selection resolved a node without draw-ready persisted geometry.";
                return result;
            }
            auto draw_instance = instance;
            draw_instance.geometry_node_index = selected_node;
            result.instances.push_back(std::move(draw_instance));
        }
    }
    return result;
}

} // namespace vice::runtime
