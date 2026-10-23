#include <vice/render/rhi.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/gpu_virtual_geometry.hpp>
#include <vice/runtime/gpu_virtual_geometry_feedback_ring.hpp>
#include <vice/runtime/virtual_geometry_gpu_arena.hpp>
#include <vice/runtime/visibility.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <string>
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
    result.normal[1] = 1.0F;
    return result;
}

vice::render::MeshAsset virtual_asset() {
    using namespace vice;
    render::MeshAsset asset;
    asset.version = render::mesh_asset_format_version_v7;
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

class RecordingCommandList final : public vice::render::CommandList {
public:
    vice::render::Buffer* vertex_buffer = nullptr;
    vice::render::Buffer* index_buffer = nullptr;
    std::uint64_t vertex_offset = 0U;
    std::uint64_t index_offset = 0U;

    bool copy_buffer(
        vice::render::Buffer&,
        std::uint64_t,
        vice::render::Buffer&,
        std::uint64_t,
        std::uint64_t
    ) override { return false; }
    bool begin_render_pass(vice::render::Swapchain&, const vice::render::RenderPassDesc&) override {
        return false;
    }
    bool begin_render_pass(vice::render::Texture&, const vice::render::RenderPassDesc&) override {
        return false;
    }
    bool begin_depth_render_pass(vice::render::Texture&, float, bool) override { return false; }
    bool set_viewport(const vice::render::Viewport&) override { return false; }
    bool set_scissor_rect(const vice::render::ScissorRect&) override { return false; }
    bool set_graphics_pipeline(vice::render::GraphicsPipeline&) override { return false; }
    bool set_vertex_buffer(
        std::uint32_t,
        vice::render::Buffer& buffer,
        std::uint64_t offset
    ) override {
        vertex_buffer = &buffer;
        vertex_offset = offset;
        return true;
    }
    bool set_index_buffer(
        vice::render::Buffer& buffer,
        vice::render::IndexFormat,
        std::uint64_t offset
    ) override {
        index_buffer = &buffer;
        index_offset = offset;
        return true;
    }
    bool set_graphics_constant_buffer(
        const vice::render::ShaderBindingKey&,
        vice::render::Buffer&,
        std::uint64_t,
        std::uint64_t
    ) override { return false; }
    bool set_graphics_buffer(
        const vice::render::ShaderBindingKey&,
        vice::render::Buffer&,
        const vice::render::BufferViewDesc&
    ) override { return false; }
    bool set_graphics_storage_buffer(
        const vice::render::ShaderBindingKey&,
        vice::render::Buffer&,
        const vice::render::BufferViewDesc&
    ) override { return false; }
    bool set_graphics_texture(
        const vice::render::ShaderBindingKey&,
        vice::render::Texture&
    ) override { return false; }
    bool set_graphics_storage_texture(
        const vice::render::ShaderBindingKey&,
        vice::render::Texture&
    ) override { return false; }
    bool set_graphics_sampler(
        const vice::render::ShaderBindingKey&,
        vice::render::Sampler&
    ) override { return false; }
    bool draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {
        return false;
    }
    bool end_render_pass() override { return false; }
    bool present(vice::render::Swapchain&) override { return false; }
};

[[nodiscard]] bool test_packed_arena_and_resolved_bindings() {
    using namespace vice;
    using namespace vice::runtime;

    auto device = render::Device::create();
    if (!device) {
        std::cerr << "Could not create Tier 1 device for virtual geometry arena test.\n";
        return false;
    }
    RuntimeVirtualGeometryGpuArena arena;
    std::string diagnostics;
    if (!arena.initialize(*device, &diagnostics)) {
        std::cerr << "Could not initialize virtual geometry arena: " << diagnostics << '\n';
        return false;
    }

    const world::ResourceId first{required_id("8a000000-0000-4000-8000-000000000001")};
    const world::ResourceId second{required_id("8a000000-0000-4000-8000-000000000002")};
    const std::array first_vertices{vertex(-1.0F, 0.0F), vertex(0.0F, 1.0F), vertex(1.0F, 0.0F)};
    const std::array second_vertices{vertex(2.0F, 0.0F), vertex(3.0F, 1.0F), vertex(4.0F, 0.0F)};
    const std::array<std::uint32_t, 3U> indices{0U, 1U, 2U};
    const std::array sources{
        RuntimeVirtualGeometryGpuArenaSource{first, first_vertices, indices},
        RuntimeVirtualGeometryGpuArenaSource{second, second_vertices, indices},
    };
    const auto packed = arena.update(sources);
    if (!packed || !packed.repacked || !packed.grew || packed.generation == 0U) {
        std::cerr << "Virtual geometry arena did not perform its initial packed upload: "
                  << packed.diagnostics << '\n';
        return false;
    }
    const auto first_binding = arena.find(first);
    const auto second_binding = arena.find(second);
    if (!first_binding.has_value() || !second_binding.has_value() ||
        first_binding->vertex_buffer != second_binding->vertex_buffer ||
        first_binding->index_buffer != second_binding->index_buffer ||
        first_binding->vertex_offset % runtime_virtual_geometry_vertex_alignment != 0U ||
        second_binding->vertex_offset % runtime_virtual_geometry_vertex_alignment != 0U ||
        first_binding->index_offset % runtime_virtual_geometry_index_alignment != 0U ||
        second_binding->index_offset % runtime_virtual_geometry_index_alignment != 0U ||
        first_binding->vertex_offset + first_binding->vertex_size > second_binding->vertex_offset ||
        first_binding->index_offset + first_binding->index_size > second_binding->index_offset) {
        std::cerr << "Virtual geometry arena bindings are not shared/aligned/non-overlapping.\n";
        return false;
    }

    const auto unchanged = arena.update(sources);
    if (!unchanged || unchanged.repacked || unchanged.grew ||
        unchanged.generation != packed.generation) {
        std::cerr << "Unchanged virtual geometry arena set unexpectedly repacked.\n";
        return false;
    }

    RuntimeVirtualGeometryBufferSlice vertex_slice{
        *second_binding->vertex_buffer,
        second_binding->vertex_offset,
        second_binding->vertex_size,
        render::BufferUsage::vertex
    };
    RuntimeVirtualGeometryBufferSlice index_slice{
        *second_binding->index_buffer,
        second_binding->index_offset,
        second_binding->index_size,
        render::BufferUsage::index
    };
    RecordingCommandList commands;
    if (!commands.set_vertex_buffer_resolved(0U, vertex_slice, 8U) ||
        !commands.set_index_buffer_resolved(index_slice, render::IndexFormat::uint32, 4U) ||
        commands.vertex_buffer != second_binding->vertex_buffer ||
        commands.index_buffer != second_binding->index_buffer ||
        commands.vertex_offset != second_binding->vertex_offset + 8U ||
        commands.index_offset != second_binding->index_offset + 4U) {
        std::cerr << "RHI buffer-slice binding did not resolve to the shared arena backing offsets.\n";
        return false;
    }

    const std::array reduced_sources{
        RuntimeVirtualGeometryGpuArenaSource{second, second_vertices, indices},
    };
    const auto reduced = arena.update(reduced_sources);
    const auto rebound = arena.find(second);
    if (!reduced || !reduced.repacked || reduced.generation <= packed.generation ||
        !rebound.has_value() || rebound->vertex_offset != 0U || rebound->index_offset != 0U) {
        std::cerr << "Virtual geometry arena did not deterministically repack after eviction.\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool test_async_feedback_lanes() {
    using namespace vice;
    using namespace vice::runtime;

    const world::ResourceId mesh{required_id("8b000000-0000-4000-8000-000000000001")};
    const std::vector<CookedMeshRecord> meshes{
        CookedMeshRecord{.resource_id = mesh, .asset = virtual_asset()},
    };
    const std::vector<RenderMeshInstance> instances{
        instance(mesh, "8b100000-0000-4000-8000-000000000001", 0.0, -10.0),
        instance(mesh, "8b100000-0000-4000-8000-000000000002", 0.0, -500.0),
    };
    const auto plan_build = build_runtime_gpu_virtual_geometry_plan(instances, meshes);
    if (!plan_build) {
        std::cerr << "Could not build async feedback plan: " << plan_build.diagnostics << '\n';
        return false;
    }
    const auto& plan = plan_build.plan;
    const auto bootstrap = build_runtime_gpu_virtual_geometry_root_feedback(plan);
    if (!bootstrap || bootstrap.node_requests.size() != 2U || !bootstrap.page_requests.empty() ||
        bootstrap.node_requests[0].node_index != 0U || bootstrap.node_requests[1].node_index != 0U) {
        std::cerr << "Virtual geometry root bootstrap frontier is not deterministic.\n";
        return false;
    }

    auto device = render::Device::create();
    if (!device) {
        std::cerr << "Could not create Tier 1 device for async feedback test.\n";
        return false;
    }
    RuntimeGpuVirtualGeometryFeedbackRing ring;
    std::array<RuntimeGpuInstanceTable, runtime_gpu_virtual_geometry_feedback_lane_count> models;
    std::array<RuntimeGpuVirtualGeometrySelector, runtime_gpu_virtual_geometry_feedback_lane_count> selectors;
    std::string diagnostics;
    if (!ring.initialize(*device, &diagnostics)) {
        std::cerr << "Could not initialize async feedback ring: " << diagnostics << '\n';
        return false;
    }
    for (std::size_t lane = 0U; lane < models.size(); ++lane) {
        if (!models[lane].initialize(*device, &diagnostics) ||
            !selectors[lane].initialize(*device, &diagnostics)) {
            std::cerr << "Could not initialize async selection lane: " << diagnostics << '\n';
            return false;
        }
    }

    const auto first_reserved = ring.acquire(plan, instances);
    const auto second_reserved = ring.acquire(plan, instances);
    const auto saturated = ring.acquire(plan, instances);
    if (!first_reserved || !second_reserved || !saturated ||
        !first_reserved.ticket.has_value() || !second_reserved.ticket.has_value() ||
        saturated.ticket.has_value() || first_reserved.ticket->slot == second_reserved.ticket->slot ||
        ring.in_flight_count() != runtime_gpu_virtual_geometry_feedback_lane_count) {
        std::cerr << "Async feedback ring did not reserve exactly two independent lanes.\n";
        return false;
    }
    ring.cancel(*first_reserved.ticket);
    ring.cancel(*second_reserved.ticket);

    const auto frustum = runtime_visibility_frustum(
        {0.0F, 0.0F, -1.0F},
        {0.0F, 1.0F, 0.0F},
        1.0471975512F,
        1.0F,
        0.1F,
        1000.0F
    );
    if (!valid(frustum)) {
        std::cerr << "Could not build async feedback frustum.\n";
        return false;
    }
    const auto acquired = ring.acquire(plan, instances);
    if (!acquired || !acquired.ticket.has_value()) {
        std::cerr << "Could not acquire async feedback lane.\n";
        return false;
    }
    const auto ticket = *acquired.ticket;
    std::vector<math::Mat4> matrices;
    matrices.reserve(instances.size());
    for (const auto& render_instance : instances) {
        matrices.push_back(runtime_model_matrix_relative_to(render_instance, world::GlobalPosition{}));
    }
    auto commands = device->create_command_list();
    if (!commands || !models[ticket.slot].upload(*commands, matrices, &diagnostics) ||
        !selectors[ticket.slot].select(
            *commands,
            plan,
            models[ticket.slot].result(),
            frustum,
            1.0471975512F,
            1080.0F,
            5.0F,
            &diagnostics
        ) || !ring.capture(*commands, selectors[ticket.slot].result(), ticket, &diagnostics)) {
        ring.cancel(ticket);
        std::cerr << "Could not encode async selection lane: " << diagnostics << '\n';
        return false;
    }
    const auto submission = device->submit(std::move(commands));
    if (submission == render::invalid_submission || !ring.commit(ticket, submission, &diagnostics)) {
        ring.cancel(ticket);
        std::cerr << "Could not commit async selection lane: " << diagnostics << '\n';
        return false;
    }

    // Production polling never waits. The test waits only to make the completion point deterministic.
    if (!device->wait_for_submission(submission)) {
        std::cerr << "Async feedback test submission did not complete.\n";
        return false;
    }
    const auto completed = ring.poll(plan, instances);
    if (!completed || !completed.feedback.has_value() || completed.serial != ticket.serial ||
        completed.feedback->node_requests.size() != 3U ||
        completed.feedback->page_requests.size() != 2U || ring.in_flight_count() != 0U) {
        std::cerr << "Async feedback ring did not consume the completed compatible frontier: "
                  << completed.diagnostics << '\n';
        return false;
    }

    const auto stale_ticket_result = ring.acquire(plan, instances);
    if (!stale_ticket_result || !stale_ticket_result.ticket.has_value()) return false;
    const auto stale_ticket = *stale_ticket_result.ticket;
    auto stale_commands = device->create_command_list();
    if (!stale_commands || !models[stale_ticket.slot].upload(*stale_commands, matrices, &diagnostics) ||
        !selectors[stale_ticket.slot].select(
            *stale_commands,
            plan,
            models[stale_ticket.slot].result(),
            frustum,
            1.0471975512F,
            1080.0F,
            5.0F,
            &diagnostics
        ) || !ring.capture(
            *stale_commands,
            selectors[stale_ticket.slot].result(),
            stale_ticket,
            &diagnostics
        )) {
        ring.cancel(stale_ticket);
        return false;
    }
    const auto stale_submission = device->submit(std::move(stale_commands));
    if (stale_submission == render::invalid_submission ||
        !ring.commit(stale_ticket, stale_submission, &diagnostics) ||
        !device->wait_for_submission(stale_submission)) {
        return false;
    }
    auto changed_instances = instances;
    changed_instances[0].entity = required_id("8b100000-0000-4000-8000-000000000099");
    const auto stale = ring.poll(plan, changed_instances);
    if (!stale || stale.feedback.has_value() || ring.in_flight_count() != 0U) {
        std::cerr << "Async feedback ring reused a completed frontier for a changed scene identity.\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    return test_packed_arena_and_resolved_bindings() && test_async_feedback_lanes() ? 0 : 1;
}
