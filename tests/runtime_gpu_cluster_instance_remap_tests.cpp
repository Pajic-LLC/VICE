#include <vice/math/math.hpp>
#include <vice/runtime/gpu_cluster_instance_remap.hpp>
#include <vice/runtime/gpu_cluster_visibility.hpp>
#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/gpu_meshlet_table.hpp>
#include <vice/runtime/gpu_visibility_set.hpp>
#include <vice/world/persistent_id.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

[[nodiscard]] vice::world::ResourceId required_resource(const char* const text) {
    const auto id = vice::world::PersistentId::parse(text);
    if (!id.has_value()) std::terminate();
    return vice::world::ResourceId{*id};
}

[[nodiscard]] std::unique_ptr<vice::render::Buffer> readback_buffer(
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

} // namespace

int main() {
    using namespace vice;
    using namespace vice::runtime;

    auto device = render::Device::create();
    if (!device) {
        std::cerr << "Could not create Tier 1 device for cluster remap test.\n";
        return 1;
    }

    const auto mesh_id = required_resource("76000000-0000-4000-8000-000000000001");
    RuntimeGpuMeshletTablePlan meshlets;
    meshlets.mesh_ids.push_back(mesh_id);
    meshlets.meshes.push_back(RuntimeGpuMeshRecord{.first_meshlet = 0U, .meshlet_count = 2U});
    meshlets.meshlets.push_back(RuntimeGpuMeshletRecord{
        .bounds_center_radius = {0.0F, 0.0F, 0.0F, 0.25F},
        .source_first_index = 0U,
        .source_index_count = 3U,
        .mesh_index = 0U,
    });
    meshlets.meshlets.push_back(RuntimeGpuMeshletRecord{
        .bounds_center_radius = {0.5F, 0.0F, 0.0F, 0.25F},
        .source_first_index = 3U,
        .source_index_count = 3U,
        .mesh_index = 0U,
    });

    std::array<RenderMeshInstance, 2U> instances{};
    instances[0].mesh = mesh_id;
    instances[1].mesh = mesh_id;
    const auto candidate_build = build_runtime_gpu_cluster_candidate_plan(instances, meshlets);
    if (!candidate_build || candidate_build.plan.candidates.size() != 4U) {
        std::cerr << "Could not build cluster candidates: " << candidate_build.diagnostics << '\n';
        return 1;
    }
    const std::array<std::uint8_t, 2U> eligible{1U, 1U};
    const auto batch_build = build_runtime_gpu_cluster_batch_plan(
        candidate_build.plan, meshlets, eligible
    );
    if (!batch_build || batch_build.plan.batches.size() != 2U) {
        std::cerr << "Could not build cluster batches: " << batch_build.diagnostics << '\n';
        return 1;
    }

    RuntimeGpuMeshletTable meshlet_table;
    RuntimeGpuInstanceTable instance_table;
    RuntimeGpuIdentityVisibility parent_visibility;
    RuntimeGpuClusterVisibility cluster_visibility;
    RuntimeGpuDrawBatcher batcher;
    RuntimeGpuClusterInstanceRemap remap;
    std::string diagnostics;
    if (!meshlet_table.initialize(*device, &diagnostics) ||
        !instance_table.initialize(*device, &diagnostics) ||
        !parent_visibility.initialize(*device, &diagnostics) ||
        !cluster_visibility.initialize(*device, &diagnostics) ||
        !batcher.initialize(*device, &diagnostics) ||
        !remap.initialize(*device, &diagnostics)) {
        std::cerr << "Could not initialize cluster remap test resources: " << diagnostics << '\n';
        return 1;
    }

    std::array<math::Mat4, 2U> models{
        math::translation({0.0F, 0.0F, -5.0F}),
        math::translation({20.0F, 0.0F, -5.0F}),
    };
    const auto frustum = runtime_visibility_frustum(
        {0.0F, 0.0F, -1.0F},
        {0.0F, 1.0F, 0.0F},
        1.57079632679489661923F,
        1.0F,
        1.0F,
        10.0F
    );

    auto commands = device->create_command_list();
    if (!commands ||
        !meshlet_table.upload(*commands, meshlets, &diagnostics) ||
        !instance_table.upload(*commands, models, &diagnostics) ||
        !parent_visibility.prepare(*commands, 2U, &diagnostics) ||
        !cluster_visibility.cull(
            *commands,
            candidate_build.plan.candidates,
            parent_visibility.result(),
            meshlet_table.result(),
            instance_table.result(),
            frustum,
            &diagnostics
        ) || !batcher.build(
            *commands,
            cluster_visibility.result().visibility,
            batch_build.plan.candidate_batches,
            batch_build.plan.batches,
            &diagnostics
        ) || !remap.remap(
            *commands,
            batcher.result(),
            cluster_visibility.result(),
            &diagnostics
        )) {
        std::cerr << "Could not encode cluster visibility/remap chain: " << diagnostics << '\n';
        return 1;
    }

    const auto batch_result = batcher.result();
    const auto remap_result = remap.result();
    if (batch_result.indirect_arguments == nullptr || remap_result.scene_instance_indices == nullptr ||
        remap_result.capacity < candidate_build.plan.candidates.size()) {
        std::cerr << "Cluster remap result contract is incomplete.\n";
        return 1;
    }

    const std::uint64_t remap_bytes = static_cast<std::uint64_t>(
        candidate_build.plan.candidates.size()
    ) * sizeof(std::uint32_t);
    const std::uint64_t argument_bytes = static_cast<std::uint64_t>(
        batch_build.plan.batches.size()
    ) * sizeof(render::IndexedIndirectDrawArguments);
    auto remap_readback = readback_buffer(*device, remap_bytes, "cluster remap readback");
    auto argument_readback = readback_buffer(*device, argument_bytes, "cluster argument readback");
    if (!remap_readback || !argument_readback ||
        !commands->copy_buffer(
            *remap_result.scene_instance_indices, 0U, *remap_readback, 0U, remap_bytes
        ) || !commands->copy_buffer(
            *batch_result.indirect_arguments, 0U, *argument_readback, 0U, argument_bytes
        )) {
        std::cerr << "Could not encode cluster remap readback.\n";
        return 1;
    }

    const auto submission = device->submit(std::move(commands));
    if (submission == render::invalid_submission || !device->wait_for_submission(submission)) {
        std::cerr << "Cluster remap GPU submission failed.\n";
        return 1;
    }

    std::array<std::uint32_t, 4U> remapped{};
    std::array<render::IndexedIndirectDrawArguments, 2U> arguments{};
    if (!remap_readback->read(0U, std::as_writable_bytes(std::span{remapped})) ||
        !argument_readback->read(0U, std::as_writable_bytes(std::span{arguments}))) {
        std::cerr << "Could not read cluster remap output.\n";
        return 1;
    }

    if (arguments[0].instance_count != 1U || arguments[1].instance_count != 1U ||
        arguments[0].first_instance >= remapped.size() ||
        arguments[1].first_instance >= remapped.size() ||
        remapped[arguments[0].first_instance] != 0U ||
        remapped[arguments[1].first_instance] != 0U) {
        std::cerr << "Cluster candidate IDs were not remapped to the authoritative scene instance.\n";
        return 1;
    }

    return 0;
}
