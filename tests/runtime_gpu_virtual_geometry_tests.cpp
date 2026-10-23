#include <vice/render/rhi.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/gpu_virtual_geometry.hpp>
#include <vice/runtime/scene.hpp>
#include <vice/runtime/virtual_geometry.hpp>
#include <vice/runtime/visibility.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <tuple>
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

vice::render::MeshVertex vertex(const float x, const float z) {
    vice::render::MeshVertex result;
    result.position[0] = x;
    result.position[1] = 0.0F;
    result.position[2] = z;
    result.normal[1] = 1.0F;
    return result;
}

vice::render::MeshAsset virtual_asset() {
    using namespace vice;
    render::MeshAsset asset;
    asset.version = render::mesh_asset_format_version_v6;
    asset.vertices = {
        vertex(-2.0F, 0.0F),
        vertex(-1.0F, 0.0F),
        vertex(-1.5F, 1.0F),
        vertex(1.0F, 0.0F),
        vertex(2.0F, 0.0F),
        vertex(1.5F, 1.0F),
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
            .vertices = {
                vertex(-2.0F, 0.0F),
                vertex(2.0F, 0.0F),
                vertex(0.0F, 1.0F),
            },
            .indices = {0U, 1U, 2U},
        },
    };
    return asset;
}

vice::render::MeshAsset legacy_asset() {
    vice::render::MeshAsset asset;
    asset.version = vice::render::mesh_asset_format_version_v3;
    asset.vertices = {vertex(-1.0F, 0.0F), vertex(1.0F, 0.0F), vertex(0.0F, 1.0F)};
    asset.indices = {0U, 1U, 2U};
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

[[nodiscard]] bool test_plan_contract() {
    using namespace vice;
    using namespace vice::runtime;

    const world::ResourceId virtual_mesh{
        required_id("81000000-0000-4000-8000-000000000001")
    };
    const world::ResourceId legacy_mesh{
        required_id("81000000-0000-4000-8000-000000000002")
    };
    const std::vector<CookedMeshRecord> meshes{
        CookedMeshRecord{.resource_id = virtual_mesh, .asset = virtual_asset()},
        CookedMeshRecord{.resource_id = legacy_mesh, .asset = legacy_asset()},
    };
    const std::vector<RenderMeshInstance> instances{
        instance(virtual_mesh, "82000000-0000-4000-8000-000000000001", 0.0, -10.0),
        instance(virtual_mesh, "82000000-0000-4000-8000-000000000002", 0.0, -500.0),
        instance(virtual_mesh, "82000000-0000-4000-8000-000000000003", 1000.0, -100.0),
        instance(legacy_mesh, "82000000-0000-4000-8000-000000000004", 0.0, -20.0),
    };

    const auto first = build_runtime_gpu_virtual_geometry_plan(instances, meshes);
    const auto second = build_runtime_gpu_virtual_geometry_plan(instances, meshes);
    if (!first || !second || first.plan != second.plan || first.plan.scene_instance_count != 4U ||
        first.plan.mesh_ids != std::vector<world::ResourceId>{virtual_mesh} ||
        first.plan.nodes.size() != 3U || first.plan.candidates.size() != 9U ||
        first.plan.mesh_index(virtual_mesh) != std::optional<std::uint32_t>{0U} ||
        first.plan.mesh_index(legacy_mesh).has_value()) {
        std::cerr << "GPU virtual geometry plan is not deterministic/canonical: "
                  << first.diagnostics << ' ' << second.diagnostics << '\n';
        return false;
    }
    for (std::uint32_t scene_instance = 0U; scene_instance < 3U; ++scene_instance) {
        for (std::uint32_t node = 0U; node < 3U; ++node) {
            const auto& candidate = first.plan.candidates[scene_instance * 3U + node];
            if (candidate.scene_instance_index != scene_instance || candidate.node_index != node ||
                candidate.mesh_index != 0U || candidate.local_node_index != node) {
                std::cerr << "GPU virtual geometry candidate identity is not canonical.\n";
                return false;
            }
        }
    }

    auto malformed_meshes = meshes;
    malformed_meshes[0].asset.virtual_geometry_proxies.hierarchy.nodes[1].parent_index =
        render::virtual_geometry_invalid_node;
    if (build_runtime_gpu_virtual_geometry_plan(instances, malformed_meshes)) {
        std::cerr << "GPU virtual geometry accepted malformed hierarchy connectivity.\n";
        return false;
    }

    const std::array legacy_only{instances.back()};
    const auto legacy_plan = build_runtime_gpu_virtual_geometry_plan(legacy_only, meshes);
    if (!legacy_plan || !legacy_plan.plan.nodes.empty() || !legacy_plan.plan.candidates.empty() ||
        !legacy_plan.plan.mesh_ids.empty() || legacy_plan.plan.scene_instance_count != 1U) {
        std::cerr << "Legacy mesh unexpectedly generated GPU virtual geometry work.\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool test_gpu_selection_matches_cpu_oracle() {
    using namespace vice;
    using namespace vice::runtime;

    const world::ResourceId virtual_mesh{
        required_id("83000000-0000-4000-8000-000000000001")
    };
    const world::ResourceId legacy_mesh{
        required_id("83000000-0000-4000-8000-000000000002")
    };
    const std::vector<CookedMeshRecord> meshes{
        CookedMeshRecord{.resource_id = virtual_mesh, .asset = virtual_asset()},
        CookedMeshRecord{.resource_id = legacy_mesh, .asset = legacy_asset()},
    };
    const std::vector<RenderMeshInstance> instances{
        instance(virtual_mesh, "84000000-0000-4000-8000-000000000001", 0.0, -10.0),
        instance(virtual_mesh, "84000000-0000-4000-8000-000000000002", 0.0, -500.0),
        instance(virtual_mesh, "84000000-0000-4000-8000-000000000003", 1000.0, -100.0),
        instance(legacy_mesh, "84000000-0000-4000-8000-000000000004", 0.0, -20.0),
    };
    const auto plan = build_runtime_gpu_virtual_geometry_plan(instances, meshes);
    if (!plan) {
        std::cerr << "Could not build GPU virtual geometry plan: " << plan.diagnostics << '\n';
        return false;
    }

    const auto frustum = runtime_visibility_frustum(
        {0.0F, 0.0F, -1.0F},
        {0.0F, 1.0F, 0.0F},
        1.0471975512F,
        1.0F,
        0.1F,
        1000.0F
    );
    if (!valid(frustum)) {
        std::cerr << "Could not build virtual geometry test frustum.\n";
        return false;
    }

    const auto oracle = select_runtime_virtual_geometry_instances(
        instances,
        meshes,
        frustum,
        world::GlobalPosition{},
        1.0471975512F,
        1080.0F,
        5.0F
    );
    if (!oracle || oracle.instances.size() != 4U ||
        oracle.instances[0].geometry_node_index != 1U ||
        oracle.instances[1].geometry_node_index != 2U ||
        oracle.instances[2].geometry_node_index != 0U ||
        oracle.instances[3].geometry_node_index != render::virtual_geometry_invalid_node) {
        std::cerr << "CPU virtual geometry oracle did not produce the expected near/far frontier: "
                  << oracle.diagnostics << '\n';
        return false;
    }

    auto device = render::Device::create();
    if (!device) {
        std::cerr << "Could not create Tier 1 device for GPU virtual geometry smoke.\n";
        return false;
    }
    RuntimeGpuInstanceTable model_table;
    RuntimeGpuVirtualGeometrySelector selector;
    std::string diagnostics;
    if (!model_table.initialize(*device, &diagnostics) ||
        !selector.initialize(*device, &diagnostics)) {
        std::cerr << "Could not initialize GPU virtual geometry resources: " << diagnostics << '\n';
        return false;
    }

    std::vector<math::Mat4> models;
    models.reserve(instances.size());
    for (const auto& render_instance : instances) {
        models.push_back(runtime_model_matrix_relative_to(render_instance, world::GlobalPosition{}));
    }

    auto commands = device->create_command_list();
    if (!commands || !model_table.upload(*commands, models, &diagnostics) ||
        !selector.select(
            *commands,
            plan.plan,
            model_table.result(),
            frustum,
            1.0471975512F,
            1080.0F,
            5.0F,
            &diagnostics
        )) {
        std::cerr << "Could not encode GPU virtual geometry selection: " << diagnostics << '\n';
        return false;
    }

    const auto gpu = selector.result();
    if (gpu.node_request_count == nullptr || gpu.node_requests == nullptr ||
        gpu.page_request_count == nullptr || gpu.page_requests == nullptr ||
        gpu.request_capacity < plan.plan.candidates.size() ||
        gpu.candidate_count != plan.plan.candidates.size()) {
        std::cerr << "GPU virtual geometry exposed an invalid result contract.\n";
        return false;
    }

    auto node_count_readback = make_readback(
        *device, sizeof(std::uint32_t), "Virtual geometry node request count readback"
    );
    auto node_requests_readback = make_readback(
        *device,
        static_cast<std::uint64_t>(gpu.request_capacity) * sizeof(RuntimeGpuVirtualGeometryNodeRequest),
        "Virtual geometry node requests readback"
    );
    auto page_count_readback = make_readback(
        *device, sizeof(std::uint32_t), "Virtual geometry page request count readback"
    );
    auto page_requests_readback = make_readback(
        *device,
        static_cast<std::uint64_t>(gpu.request_capacity) * sizeof(RuntimeGpuVirtualGeometryPageRequest),
        "Virtual geometry page requests readback"
    );
    if (!node_count_readback || !node_requests_readback || !page_count_readback ||
        !page_requests_readback ||
        !commands->copy_buffer(
            *gpu.node_request_count, 0U, *node_count_readback, 0U, sizeof(std::uint32_t)
        ) || !commands->copy_buffer(
            *gpu.node_requests,
            0U,
            *node_requests_readback,
            0U,
            static_cast<std::uint64_t>(gpu.request_capacity) *
                sizeof(RuntimeGpuVirtualGeometryNodeRequest)
        ) || !commands->copy_buffer(
            *gpu.page_request_count, 0U, *page_count_readback, 0U, sizeof(std::uint32_t)
        ) || !commands->copy_buffer(
            *gpu.page_requests,
            0U,
            *page_requests_readback,
            0U,
            static_cast<std::uint64_t>(gpu.request_capacity) *
                sizeof(RuntimeGpuVirtualGeometryPageRequest)
        )) {
        std::cerr << "Could not encode GPU virtual geometry readback.\n";
        return false;
    }

    const auto submission = device->submit(std::move(commands));
    if (submission == render::invalid_submission || !device->wait_for_submission(submission)) {
        std::cerr << "GPU virtual geometry submission failed.\n";
        return false;
    }

    std::uint32_t node_count = 0U;
    std::uint32_t page_count = 0U;
    std::vector<RuntimeGpuVirtualGeometryNodeRequest> node_requests(gpu.request_capacity);
    std::vector<RuntimeGpuVirtualGeometryPageRequest> page_requests(gpu.request_capacity);
    if (!node_count_readback->read(
            0U, std::as_writable_bytes(std::span{&node_count, 1})
        ) || !page_count_readback->read(
            0U, std::as_writable_bytes(std::span{&page_count, 1})
        ) || !node_requests_readback->read(
            0U, std::as_writable_bytes(std::span{node_requests})
        ) || !page_requests_readback->read(
            0U, std::as_writable_bytes(std::span{page_requests})
        )) {
        std::cerr << "Could not read GPU virtual geometry output.\n";
        return false;
    }
    if (node_count != 3U || page_count != 2U) {
        std::cerr << "GPU virtual geometry selected an unexpected frontier/request count: "
                  << node_count << " nodes, " << page_count << " pages.\n";
        return false;
    }
    node_requests.resize(node_count);
    page_requests.resize(page_count);
    std::sort(node_requests.begin(), node_requests.end(), [](const auto& left, const auto& right) {
        return std::tie(left.scene_instance_index, left.mesh_index, left.node_index) <
            std::tie(right.scene_instance_index, right.mesh_index, right.node_index);
    });
    std::sort(page_requests.begin(), page_requests.end(), [](const auto& left, const auto& right) {
        return std::tie(left.scene_instance_index, left.mesh_index, left.node_index, left.page_index) <
            std::tie(right.scene_instance_index, right.mesh_index, right.node_index, right.page_index);
    });

    const std::vector<RuntimeGpuVirtualGeometryNodeRequest> expected_nodes{
        RuntimeGpuVirtualGeometryNodeRequest{.scene_instance_index = 0U, .mesh_index = 0U, .node_index = 1U},
        RuntimeGpuVirtualGeometryNodeRequest{.scene_instance_index = 0U, .mesh_index = 0U, .node_index = 2U},
        RuntimeGpuVirtualGeometryNodeRequest{.scene_instance_index = 1U, .mesh_index = 0U, .node_index = 0U},
    };
    const std::vector<RuntimeGpuVirtualGeometryPageRequest> expected_pages{
        RuntimeGpuVirtualGeometryPageRequest{
            .scene_instance_index = 0U,
            .mesh_index = 0U,
            .node_index = 1U,
            .page_index = 0U,
        },
        RuntimeGpuVirtualGeometryPageRequest{
            .scene_instance_index = 0U,
            .mesh_index = 0U,
            .node_index = 2U,
            .page_index = 1U,
        },
    };
    if (node_requests != expected_nodes || page_requests != expected_pages) {
        std::cerr << "GPU virtual geometry frontier diverged from the CPU oracle.\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    return test_plan_contract() && test_gpu_selection_matches_cpu_oracle() ? 0 : 1;
}
