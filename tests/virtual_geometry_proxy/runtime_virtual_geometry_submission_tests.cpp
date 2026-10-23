#include <vice/render/mesh_asset.hpp>
#include <vice/render/rhi.hpp>
#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/gpu_virtual_geometry.hpp>
#include <vice/runtime/gpu_virtual_geometry_feedback.hpp>
#include <vice/runtime/virtual_geometry_submission.hpp>
#include <vice/runtime/visibility.hpp>
#include <vice/tools/mesh_cooker.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    if (!parsed.has_value()) {
        std::cerr << "Invalid test UUID: " << text << '\n';
        std::terminate();
    }
    return *parsed;
}

vice::render::MeshVertex vertex(const float x, const float z) {
    vice::render::MeshVertex result;
    result.position[0] = x;
    result.position[1] = 0.0F;
    result.position[2] = z;
    result.color[0] = 0.2F;
    result.color[1] = 0.5F;
    result.color[2] = 0.8F;
    result.normal[1] = 1.0F;
    return result;
}

vice::render::MeshAsset feedback_asset() {
    using namespace vice;
    render::MeshAsset asset;
    asset.version = render::mesh_asset_format_version_v6;
    asset.vertices = {
        vertex(-2.0F, 0.0F), vertex(-1.0F, 0.0F), vertex(-1.5F, 1.0F),
        vertex(1.0F, 0.0F), vertex(2.0F, 0.0F), vertex(1.5F, 1.0F),
    };
    asset.indices = {0U, 1U, 2U, 3U, 4U, 5U};
    asset.virtual_geometry_pages.pages = {
        render::VirtualGeometryPage{
            .meshlet_offset = 0U,
            .meshlet_count = 1U,
            .triangle_index_offset = 0U,
            .triangle_index_count = 3U,
            .bounds = render::MeshletBounds{.center = {-1.5F, 0.0F, 0.5F}, .radius = 1.0F},
        },
        render::VirtualGeometryPage{
            .meshlet_offset = 1U,
            .meshlet_count = 1U,
            .triangle_index_offset = 3U,
            .triangle_index_count = 3U,
            .bounds = render::MeshletBounds{.center = {1.5F, 0.0F, 0.5F}, .radius = 1.0F},
        },
    };
    asset.virtual_geometry_proxies.hierarchy.root_index = 0U;
    asset.virtual_geometry_proxies.hierarchy.nodes = {
        render::VirtualGeometryHierarchyNode{
            .parent_index = render::virtual_geometry_invalid_node,
            .first_child = 1U,
            .child_count = 2U,
            .page_offset = 0U,
            .page_count = 2U,
            .depth = 0U,
            .bounds = render::MeshletBounds{.center = {0.0F, 0.0F, 0.5F}, .radius = 3.0F},
            .geometric_error = 1.0F,
        },
        render::VirtualGeometryHierarchyNode{
            .parent_index = 0U,
            .first_child = render::virtual_geometry_invalid_node,
            .child_count = 0U,
            .page_offset = 0U,
            .page_count = 1U,
            .depth = 1U,
            .bounds = render::MeshletBounds{.center = {-1.5F, 0.0F, 0.5F}, .radius = 1.0F},
            .geometric_error = 0.0F,
        },
        render::VirtualGeometryHierarchyNode{
            .parent_index = 0U,
            .first_child = render::virtual_geometry_invalid_node,
            .child_count = 0U,
            .page_offset = 1U,
            .page_count = 1U,
            .depth = 1U,
            .bounds = render::MeshletBounds{.center = {1.5F, 0.0F, 0.5F}, .radius = 1.0F},
            .geometric_error = 0.0F,
        },
    };
    asset.virtual_geometry_proxies.proxies = {
        render::VirtualGeometryProxyMesh{
            .node_index = 0U,
            .source_triangle_count = 2U,
            .direct_geometric_error = 1.0F,
            .vertices = {vertex(-2.0F, 0.0F), vertex(2.0F, 0.0F), vertex(0.0F, 1.0F)},
            .indices = {0U, 1U, 2U},
        },
    };
    return asset;
}

vice::runtime::RenderMeshInstance instance(
    const vice::world::ResourceId mesh,
    const char* const entity,
    const double x,
    const double z
) {
    vice::runtime::RenderMeshInstance result;
    result.entity = required_id(entity);
    result.mesh = mesh;
    result.spatial.global_position.x = x;
    result.spatial.global_position.z = z;
    return result;
}

[[nodiscard]] vice::tools::mesh_cooker::MeshSource stream_source(
    const vice::world::PersistentId stable_id
) {
    using namespace vice;
    tools::mesh_cooker::MeshSource source;
    source.version = tools::mesh_cooker::mesh_source_format_version_v2;
    source.name = "runtime_virtual_geometry_submission_v7";
    const auto stable = stable_id.bytes();
    std::copy(stable.begin(), stable.end(), source.stable_id.begin());

    constexpr std::uint32_t triangle_count = 32U * 21U + 1U;
    source.vertices.reserve(static_cast<std::size_t>(triangle_count) * 3U);
    source.indices.reserve(static_cast<std::size_t>(triangle_count) * 3U);
    for (std::uint32_t triangle = 0U; triangle < triangle_count; ++triangle) {
        const auto base_index = static_cast<std::uint32_t>(source.vertices.size());
        const float base_x = static_cast<float>(triangle) * 2.0F;
        source.vertices.push_back(vertex(base_x, 0.0F));
        source.vertices.push_back(vertex(base_x + 1.0F, 0.0F));
        source.vertices.push_back(vertex(base_x, 1.0F));
        source.indices.push_back(base_index);
        source.indices.push_back(base_index + 2U);
        source.indices.push_back(base_index + 1U);
    }
    return source;
}

[[nodiscard]] bool write_bytes(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes
) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) return false;
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    return output.good();
}

[[nodiscard]] std::unique_ptr<vice::render::Buffer> make_readback(
    vice::render::Device& device,
    const std::uint64_t size,
    const char* const name
) {
    vice::render::BufferDesc desc;
    desc.size = size;
    desc.usage = vice::render::BufferUsage::copy_destination;
    desc.memory = vice::render::MemoryLocation::gpu_to_cpu;
    desc.debug_name = name;
    return device.create_buffer(desc);
}

[[nodiscard]] bool test_gpu_feedback_capture() {
    using namespace vice;
    using namespace vice::runtime;

    const world::ResourceId mesh{required_id("88000000-0000-4000-8000-000000000001")};
    const world::ResourceId legacy_mesh{required_id("88000000-0000-4000-8000-000000000002")};
    render::MeshAsset legacy;
    legacy.version = render::mesh_asset_format_version_v3;
    legacy.vertices = {vertex(-1.0F, 0.0F), vertex(1.0F, 0.0F), vertex(0.0F, 1.0F)};
    legacy.indices = {0U, 1U, 2U};
    const std::vector<CookedMeshRecord> meshes{
        CookedMeshRecord{.resource_id = mesh, .asset = feedback_asset()},
        CookedMeshRecord{.resource_id = legacy_mesh, .asset = legacy},
    };
    const std::vector<RenderMeshInstance> instances{
        instance(mesh, "88100000-0000-4000-8000-000000000001", 0.0, -10.0),
        instance(mesh, "88100000-0000-4000-8000-000000000002", 0.0, -500.0),
        instance(mesh, "88100000-0000-4000-8000-000000000003", 1000.0, -100.0),
        instance(legacy_mesh, "88100000-0000-4000-8000-000000000004", 0.0, -20.0),
    };
    const auto plan = build_runtime_gpu_virtual_geometry_plan(instances, meshes);
    const auto frustum = runtime_visibility_frustum(
        {0.0F, 0.0F, -1.0F},
        {0.0F, 1.0F, 0.0F},
        1.0471975512F,
        1.0F,
        0.1F,
        1000.0F
    );
    if (!plan || !valid(frustum)) {
        std::cerr << "Could not build GPU feedback test plan/frustum: " << plan.diagnostics << '\n';
        return false;
    }

    auto device = render::Device::create();
    RuntimeGpuInstanceTable models;
    RuntimeGpuVirtualGeometrySelector selector;
    RuntimeGpuVirtualGeometryFeedback feedback;
    std::string diagnostics;
    if (!device || !models.initialize(*device, &diagnostics) ||
        !selector.initialize(*device, &diagnostics) || !feedback.initialize(*device, &diagnostics)) {
        std::cerr << "Could not initialize GPU frontier feedback: " << diagnostics << '\n';
        return false;
    }

    std::vector<math::Mat4> matrices;
    matrices.reserve(instances.size());
    for (const auto& render_instance : instances) {
        matrices.push_back(runtime_model_matrix_relative_to(render_instance, world::GlobalPosition{}));
    }
    auto commands = device->create_command_list();
    if (!commands || !models.upload(*commands, matrices, &diagnostics) ||
        !selector.select(
            *commands,
            plan.plan,
            models.result(),
            frustum,
            1.0471975512F,
            1080.0F,
            5.0F,
            &diagnostics
        ) || !feedback.capture(*commands, selector.result(), &diagnostics)) {
        std::cerr << "Could not encode GPU frontier feedback: " << diagnostics << '\n';
        return false;
    }
    const auto submission = device->submit(std::move(commands));
    if (submission == render::invalid_submission || !device->wait_for_submission(submission)) {
        std::cerr << "GPU frontier feedback submission failed.\n";
        return false;
    }

    const auto captured = feedback.read(plan.plan);
    const std::vector<RuntimeGpuVirtualGeometryNodeRequest> expected_nodes{
        RuntimeGpuVirtualGeometryNodeRequest{.scene_instance_index = 0U, .mesh_index = 0U, .node_index = 1U},
        RuntimeGpuVirtualGeometryNodeRequest{.scene_instance_index = 0U, .mesh_index = 0U, .node_index = 2U},
        RuntimeGpuVirtualGeometryNodeRequest{.scene_instance_index = 1U, .mesh_index = 0U, .node_index = 0U},
    };
    const std::vector<RuntimeGpuVirtualGeometryPageRequest> expected_pages{
        RuntimeGpuVirtualGeometryPageRequest{.scene_instance_index = 0U, .mesh_index = 0U, .node_index = 1U, .page_index = 0U},
        RuntimeGpuVirtualGeometryPageRequest{.scene_instance_index = 0U, .mesh_index = 0U, .node_index = 2U, .page_index = 1U},
    };
    if (!captured || captured.node_requests != expected_nodes || captured.page_requests != expected_pages) {
        std::cerr << "Captured GPU frontier did not canonicalize to the expected requests: "
                  << captured.diagnostics << '\n';
        return false;
    }

    const std::array leaf_without_page{
        RuntimeGpuVirtualGeometryNodeRequest{.scene_instance_index = 0U, .mesh_index = 0U, .node_index = 1U},
    };
    if (validate_runtime_gpu_virtual_geometry_feedback(leaf_without_page, {}, plan.plan)) {
        std::cerr << "GPU feedback validation accepted a selected leaf without a page request.\n";
        return false;
    }
    const std::array ancestor_and_leaf{
        RuntimeGpuVirtualGeometryNodeRequest{.scene_instance_index = 0U, .mesh_index = 0U, .node_index = 0U},
        RuntimeGpuVirtualGeometryNodeRequest{.scene_instance_index = 0U, .mesh_index = 0U, .node_index = 1U},
    };
    const std::array leaf_page{
        RuntimeGpuVirtualGeometryPageRequest{.scene_instance_index = 0U, .mesh_index = 0U, .node_index = 1U, .page_index = 0U},
    };
    if (validate_runtime_gpu_virtual_geometry_feedback(ancestor_and_leaf, leaf_page, plan.plan)) {
        std::cerr << "GPU feedback validation accepted an ancestor/descendant frontier overlap.\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool test_streamed_submission_and_indirect_batches() {
    using namespace vice;
    using namespace vice::runtime;

    const auto mesh_pid = required_id("89000000-0000-4000-8000-000000000001");
    const world::ResourceId mesh{mesh_pid};
    const world::ResourceId legacy_mesh{required_id("89000000-0000-4000-8000-000000000002")};
    const auto cooked = tools::mesh_cooker::cook(stream_source(mesh_pid));
    if (!cooked.success) {
        std::cerr << "Could not cook v7 streamed submission fixture: " << cooked.diagnostics << '\n';
        return false;
    }
    const auto decoded = render::decode_mesh_asset(cooked.bytes);
    if (!decoded.asset.has_value() || decoded.asset->version != render::mesh_asset_format_version_v7 ||
        decoded.asset->virtual_geometry_pages.pages.size() != 2U ||
        decoded.asset->virtual_geometry_proxies.hierarchy.nodes.size() != 3U ||
        decoded.asset->virtual_geometry_proxies.proxies.size() != 1U) {
        std::cerr << "Cooked v7 fixture does not expose the expected page/proxy hierarchy.\n";
        return false;
    }

    const auto temp_root = std::filesystem::temp_directory_path();
    const auto path = temp_root / "vice_runtime_virtual_geometry_submission.vmeshc";
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
    if (!write_bytes(path, cooked.bytes)) {
        std::cerr << "Could not write v7 streamed submission fixture.\n";
        return false;
    }

    render::MeshAsset legacy;
    legacy.version = render::mesh_asset_format_version_v3;
    legacy.vertices = {vertex(-1.0F, 0.0F), vertex(1.0F, 0.0F), vertex(0.0F, 1.0F)};
    legacy.indices = {0U, 1U, 2U};
    const std::vector<CookedMeshRecord> meshes{
        CookedMeshRecord{
            .resource_id = mesh,
            .path = path,
            .asset = *decoded.asset,
            .runtime_generated = false,
        },
        CookedMeshRecord{.resource_id = legacy_mesh, .asset = legacy},
    };
    const std::vector<RenderMeshInstance> instances{
        instance(mesh, "89100000-0000-4000-8000-000000000001", 0.0, -5.0),
        instance(mesh, "89100000-0000-4000-8000-000000000002", 0.5, -5.0),
        instance(mesh, "89100000-0000-4000-8000-000000000003", -0.5, -5.0),
        instance(mesh, "89100000-0000-4000-8000-000000000004", 1000.0, -5.0),
        instance(legacy_mesh, "89100000-0000-4000-8000-000000000005", 0.0, -5.0),
    };
    const auto plan = build_runtime_gpu_virtual_geometry_plan(instances, meshes);
    if (!plan || plan.plan.mesh_ids.size() != 1U) {
        std::cerr << "Could not build v7 streamed submission plan: " << plan.diagnostics << '\n';
        std::filesystem::remove(path, cleanup_error);
        return false;
    }
    const auto mesh_index = plan.plan.mesh_index(mesh);
    if (!mesh_index.has_value()) {
        std::cerr << "V7 streamed submission plan lost the virtual mesh identity.\n";
        std::filesystem::remove(path, cleanup_error);
        return false;
    }

    const auto& hierarchy = decoded.asset->virtual_geometry_proxies.hierarchy;
    const std::uint32_t root_node = hierarchy.root_index;
    const auto leaf_found = std::find_if(
        hierarchy.nodes.begin(), hierarchy.nodes.end(),
        [](const auto& node) { return node.child_count == 0U && node.page_offset == 0U; }
    );
    if (leaf_found == hierarchy.nodes.end()) {
        std::cerr << "V7 streamed submission fixture has no page-zero leaf.\n";
        std::filesystem::remove(path, cleanup_error);
        return false;
    }
    const auto leaf_node = static_cast<std::uint32_t>(
        std::distance(hierarchy.nodes.begin(), leaf_found)
    );

    RuntimeGpuVirtualGeometryFeedbackResult feedback;
    feedback.node_requests = {
        RuntimeGpuVirtualGeometryNodeRequest{.scene_instance_index = 0U, .mesh_index = *mesh_index, .node_index = leaf_node},
        RuntimeGpuVirtualGeometryNodeRequest{.scene_instance_index = 1U, .mesh_index = *mesh_index, .node_index = leaf_node},
        RuntimeGpuVirtualGeometryNodeRequest{.scene_instance_index = 2U, .mesh_index = *mesh_index, .node_index = root_node},
    };
    feedback.page_requests = {
        RuntimeGpuVirtualGeometryPageRequest{.scene_instance_index = 0U, .mesh_index = *mesh_index, .node_index = leaf_node, .page_index = 0U},
        RuntimeGpuVirtualGeometryPageRequest{.scene_instance_index = 1U, .mesh_index = *mesh_index, .node_index = leaf_node, .page_index = 0U},
    };

    RuntimeVirtualGeometryResidentPool pool{64ULL * 1024ULL * 1024ULL};
    auto streamed = build_runtime_virtual_geometry_submission(
        instances,
        meshes,
        plan.plan,
        feedback,
        pool
    );
    if (!streamed || streamed.instances.size() != 4U || streamed.geometry.size() != 2U ||
        streamed.residency.admitted.size() != 1U || pool.pages().size() != 1U ||
        streamed.instances[0].mesh != streamed.instances[1].mesh ||
        streamed.instances[3].mesh != legacy_mesh) {
        std::cerr << "V7 streamed submission did not dedupe/resubmit the expected frontier: "
                  << streamed.diagnostics << '\n';
        std::filesystem::remove(path, cleanup_error);
        return false;
    }

    const auto leaf_resource = runtime_virtual_geometry_variant_resource_id(mesh, leaf_node);
    const auto root_resource = runtime_virtual_geometry_variant_resource_id(mesh, root_node);
    const auto leaf_geometry = std::find_if(streamed.geometry.begin(), streamed.geometry.end(), [&](const auto& item) {
        return item.resource_id == leaf_resource;
    });
    const auto root_geometry = std::find_if(streamed.geometry.begin(), streamed.geometry.end(), [&](const auto& item) {
        return item.resource_id == root_resource;
    });
    const auto* const resident_leaf = pool.find(RuntimeVirtualGeometryPageKey{.mesh = mesh, .page_index = 0U});
    if (leaf_geometry == streamed.geometry.end() || root_geometry == streamed.geometry.end() ||
        resident_leaf == nullptr || leaf_geometry->page_index != std::optional<std::uint32_t>{0U} ||
        root_geometry->page_index.has_value() ||
        leaf_geometry->asset.vertices != resident_leaf->geometry.vertices ||
        leaf_geometry->asset.indices != resident_leaf->geometry.indices ||
        root_geometry->asset.vertices != decoded.asset->virtual_geometry_proxies.proxies.front().vertices ||
        root_geometry->asset.indices != decoded.asset->virtual_geometry_proxies.proxies.front().indices) {
        std::cerr << "Streamed submission geometry did not originate from the resident leaf/persisted proxy.\n";
        std::filesystem::remove(path, cleanup_error);
        return false;
    }

    auto metadata_only_meshes = meshes;
    metadata_only_meshes[0].asset.vertices.clear();
    metadata_only_meshes[0].asset.indices.clear();
    RuntimeGpuVirtualGeometryFeedbackResult leaf_only;
    leaf_only.node_requests = {
        RuntimeGpuVirtualGeometryNodeRequest{.scene_instance_index = 0U, .mesh_index = *mesh_index, .node_index = leaf_node},
    };
    leaf_only.page_requests = {
        RuntimeGpuVirtualGeometryPageRequest{.scene_instance_index = 0U, .mesh_index = *mesh_index, .node_index = leaf_node, .page_index = 0U},
    };
    RuntimeVirtualGeometryResidentPool metadata_pool{64ULL * 1024ULL * 1024ULL};
    const auto metadata_submission = build_runtime_virtual_geometry_submission(
        instances,
        metadata_only_meshes,
        plan.plan,
        leaf_only,
        metadata_pool
    );
    if (!metadata_submission || metadata_submission.geometry.size() != 1U ||
        metadata_submission.geometry.front().asset.vertices != resident_leaf->geometry.vertices ||
        metadata_submission.geometry.front().asset.indices != resident_leaf->geometry.indices) {
        std::cerr << "Leaf submission still depends on monolithic authoritative source streams: "
                  << metadata_submission.diagnostics << '\n';
        std::filesystem::remove(path, cleanup_error);
        return false;
    }

    auto device = render::Device::create();
    RuntimeVisibilityCuller culler;
    RuntimeGpuDrawBatcher batcher;
    std::string diagnostics;
    if (!device || !culler.initialize(*device, &diagnostics) || !batcher.initialize(*device, &diagnostics)) {
        std::cerr << "Could not initialize indirect batch proof: " << diagnostics << '\n';
        std::filesystem::remove(path, cleanup_error);
        return false;
    }
    const auto frustum = runtime_visibility_frustum(
        {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F},
        1.57079632679F, 1.0F, 1.0F, 10.0F
    );
    const std::array spheres{
        RuntimeVisibilitySphere{{0.0F, 0.0F, -5.0F}, 0.25F, 0U},
        RuntimeVisibilitySphere{{0.5F, 0.0F, -5.0F}, 0.25F, 1U},
        RuntimeVisibilitySphere{{-0.5F, 0.0F, -5.0F}, 0.25F, 2U},
        RuntimeVisibilitySphere{{0.0F, 0.5F, -5.0F}, 0.25F, 3U},
    };
    const std::array<std::uint32_t, 4U> assignments{0U, 0U, 1U, 2U};
    const std::array batches{
        RuntimeGpuDrawBatch{
            .index_count = static_cast<std::uint32_t>(leaf_geometry->asset.indices.size()),
            .first_index = 0U,
            .vertex_offset = 0,
        },
        RuntimeGpuDrawBatch{
            .index_count = static_cast<std::uint32_t>(root_geometry->asset.indices.size()),
            .first_index = 0U,
            .vertex_offset = 0,
        },
        RuntimeGpuDrawBatch{
            .index_count = static_cast<std::uint32_t>(legacy.indices.size()),
            .first_index = 0U,
            .vertex_offset = 0,
        },
    };
    auto commands = device->create_command_list();
    if (!valid(frustum) || !commands || !culler.cull(*commands, spheres, frustum, &diagnostics) ||
        !batcher.build(*commands, culler.result(), assignments, batches, &diagnostics)) {
        std::cerr << "Could not build streamed frontier indirect batches: " << diagnostics << '\n';
        std::filesystem::remove(path, cleanup_error);
        return false;
    }
    const auto batch_result = batcher.result();
    const std::uint64_t argument_bytes = static_cast<std::uint64_t>(batch_result.batch_count) *
        sizeof(render::IndexedIndirectDrawArguments);
    auto readback = make_readback(*device, argument_bytes, "Streamed frontier indirect arguments readback");
    if (!readback || batch_result.indirect_arguments == nullptr ||
        !commands->copy_buffer(
            *batch_result.indirect_arguments, 0U, *readback, 0U, argument_bytes
        )) {
        std::cerr << "Could not encode streamed frontier indirect argument readback.\n";
        std::filesystem::remove(path, cleanup_error);
        return false;
    }
    const auto gpu_submission = device->submit(std::move(commands));
    if (gpu_submission == render::invalid_submission || !device->wait_for_submission(gpu_submission)) {
        std::cerr << "Streamed frontier indirect batch submission failed.\n";
        std::filesystem::remove(path, cleanup_error);
        return false;
    }
    std::vector<render::IndexedIndirectDrawArguments> arguments(batch_result.batch_count);
    if (!readback->read(0U, std::as_writable_bytes(std::span{arguments})) ||
        arguments.size() != 3U || arguments[0].instance_count != 2U ||
        arguments[1].instance_count != 1U || arguments[2].instance_count != 1U ||
        arguments[0].index_count != batches[0].index_count ||
        arguments[1].index_count != batches[1].index_count ||
        arguments[2].index_count != batches[2].index_count) {
        std::cerr << "Streamed frontier did not reach the expected indexed indirect draw batches.\n";
        std::filesystem::remove(path, cleanup_error);
        return false;
    }

    std::filesystem::remove(path, cleanup_error);
    return true;
}

} // namespace

int main() {
    return test_gpu_feedback_capture() && test_streamed_submission_and_indirect_batches() ? 0 : 1;
}
