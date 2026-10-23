#include <vice/math/math.hpp>
#include <vice/render/rhi.hpp>
#include <vice/runtime/gpu_cluster_visibility.hpp>
#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/gpu_meshlet_table.hpp>
#include <vice/runtime/scene.hpp>
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

[[nodiscard]] bool test_candidate_and_batch_plans() {
    using namespace vice;
    using namespace vice::runtime;

    const world::ResourceId mesh_id{
        required_id("75000000-0000-4000-8000-000000000001")
    };
    RuntimeGpuMeshletTablePlan meshlets;
    meshlets.mesh_ids.push_back(mesh_id);
    meshlets.meshes.push_back(RuntimeGpuMeshRecord{
        .first_meshlet = 0U,
        .meshlet_count = 2U,
    });
    meshlets.meshlets.push_back(RuntimeGpuMeshletRecord{
        .bounds_center_radius = {0.0F, 0.0F, -5.0F, 0.5F},
        .vertex_offset = 0U,
        .vertex_count = 3U,
        .triangle_offset = 0U,
        .triangle_count = 1U,
        .source_first_index = 0U,
        .source_index_count = 3U,
        .mesh_index = 0U,
    });
    meshlets.meshlets.push_back(RuntimeGpuMeshletRecord{
        .bounds_center_radius = {20.0F, 0.0F, -5.0F, 0.5F},
        .vertex_offset = 3U,
        .vertex_count = 3U,
        .triangle_offset = 3U,
        .triangle_count = 1U,
        .source_first_index = 3U,
        .source_index_count = 3U,
        .mesh_index = 0U,
    });
    meshlets.vertex_indices = {0U, 1U, 2U, 3U, 4U, 5U};
    meshlets.triangle_indices = {0U, 1U, 2U, 0U, 1U, 2U};

    const std::array instances{
        RenderMeshInstance{.mesh = mesh_id},
        RenderMeshInstance{.mesh = mesh_id},
    };
    const auto candidates = build_runtime_gpu_cluster_candidate_plan(instances, meshlets);
    if (!candidates || candidates.plan.candidates.size() != 4U ||
        candidates.plan.instance_ranges.size() != 2U ||
        candidates.plan.instance_ranges[0].first_candidate != 0U ||
        candidates.plan.instance_ranges[0].candidate_count != 2U ||
        candidates.plan.instance_ranges[1].first_candidate != 2U ||
        candidates.plan.instance_ranges[1].candidate_count != 2U) {
        std::cerr << "Cluster candidate plan did not preserve authoritative instance ranges: "
                  << candidates.diagnostics << '\n';
        return false;
    }
    const std::array expected_candidates{
        RuntimeGpuClusterCandidate{0U, 0U, 0U, 0U},
        RuntimeGpuClusterCandidate{0U, 0U, 1U, 0U},
        RuntimeGpuClusterCandidate{1U, 0U, 0U, 0U},
        RuntimeGpuClusterCandidate{1U, 0U, 1U, 0U},
    };
    for (std::size_t index = 0U; index < expected_candidates.size(); ++index) {
        const auto& actual = candidates.plan.candidates[index];
        const auto& expected = expected_candidates[index];
        if (actual.scene_instance_index != expected.scene_instance_index ||
            actual.mesh_index != expected.mesh_index ||
            actual.meshlet_index != expected.meshlet_index) {
            std::cerr << "Cluster candidate identity is not stable.\n";
            return false;
        }
    }

    const std::array<std::uint8_t, 2U> eligible{1U, 1U};
    const auto batches = build_runtime_gpu_cluster_batch_plan(
        candidates.plan, meshlets, eligible
    );
    if (!batches || batches.plan.batches.size() != 2U ||
        batches.plan.batch_meshes != std::vector<world::ResourceId>{mesh_id, mesh_id} ||
        batches.plan.batch_meshlets != std::vector<std::uint32_t>{0U, 1U} ||
        batches.plan.candidate_batches != std::vector<std::uint32_t>{0U, 1U, 0U, 1U} ||
        batches.plan.batches[0].index_count != 3U ||
        batches.plan.batches[0].first_index != 0U ||
        batches.plan.batches[1].index_count != 3U ||
        batches.plan.batches[1].first_index != 3U) {
        std::cerr << "Cluster batch plan did not preserve meshlet draw identity: "
                  << batches.diagnostics << '\n';
        return false;
    }

    const std::array<std::uint8_t, 2U> first_only{1U, 0U};
    const auto first_only_batches = build_runtime_gpu_cluster_batch_plan(
        candidates.plan, meshlets, first_only
    );
    if (!first_only_batches ||
        first_only_batches.plan.candidate_batches !=
            std::vector<std::uint32_t>{0U, 1U, runtime_gpu_draw_batch_unassigned,
                                       runtime_gpu_draw_batch_unassigned}) {
        std::cerr << "Cluster batch eligibility did not preserve fallback assignments.\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool test_native_cluster_visibility_to_batches() {
    using namespace vice;
    using namespace vice::runtime;

    const world::ResourceId mesh_id{
        required_id("76000000-0000-4000-8000-000000000001")
    };
    RuntimeGpuMeshletTablePlan meshlet_plan;
    meshlet_plan.mesh_ids.push_back(mesh_id);
    meshlet_plan.meshes.push_back(RuntimeGpuMeshRecord{
        .first_meshlet = 0U,
        .meshlet_count = 2U,
    });
    meshlet_plan.meshlets.push_back(RuntimeGpuMeshletRecord{
        .bounds_center_radius = {0.0F, 0.0F, -5.0F, 0.5F},
        .vertex_offset = 0U,
        .vertex_count = 3U,
        .triangle_offset = 0U,
        .triangle_count = 1U,
        .source_first_index = 0U,
        .source_index_count = 3U,
        .mesh_index = 0U,
    });
    meshlet_plan.meshlets.push_back(RuntimeGpuMeshletRecord{
        .bounds_center_radius = {20.0F, 0.0F, -5.0F, 0.5F},
        .vertex_offset = 3U,
        .vertex_count = 3U,
        .triangle_offset = 3U,
        .triangle_count = 1U,
        .source_first_index = 3U,
        .source_index_count = 3U,
        .mesh_index = 0U,
    });
    meshlet_plan.vertex_indices = {0U, 1U, 2U, 3U, 4U, 5U};
    meshlet_plan.triangle_indices = {0U, 1U, 2U, 0U, 1U, 2U};

    const std::array instances{
        RenderMeshInstance{.mesh = mesh_id},
        RenderMeshInstance{.mesh = mesh_id},
    };
    const auto candidate_build = build_runtime_gpu_cluster_candidate_plan(
        instances, meshlet_plan
    );
    const std::array<std::uint8_t, 2U> eligible{1U, 1U};
    const auto batch_build = candidate_build
        ? build_runtime_gpu_cluster_batch_plan(candidate_build.plan, meshlet_plan, eligible)
        : RuntimeGpuClusterBatchBuildResult{};
    if (!candidate_build || !batch_build) {
        std::cerr << "Could not build cluster execution plans: "
                  << candidate_build.diagnostics << ' ' << batch_build.diagnostics << '\n';
        return false;
    }

    auto device = render::Device::create();
    if (!device) {
        std::cerr << "Could not create Tier 1 device for cluster visibility smoke.\n";
        return false;
    }

    RuntimeGpuMeshletTable meshlet_table;
    RuntimeGpuInstanceTable model_table;
    RuntimeVisibilityCuller parent_visibility;
    RuntimeGpuClusterVisibility cluster_visibility;
    RuntimeGpuDrawBatcher batcher;
    std::string diagnostics;
    if (!meshlet_table.initialize(*device, &diagnostics) ||
        !model_table.initialize(*device, &diagnostics) ||
        !parent_visibility.initialize(*device, &diagnostics) ||
        !cluster_visibility.initialize(*device, &diagnostics) ||
        !batcher.initialize(*device, &diagnostics)) {
        std::cerr << "Could not initialize cluster execution resources: " << diagnostics << '\n';
        return false;
    }

    const auto frustum = runtime_visibility_frustum(
        {0.0F, 0.0F, -1.0F},
        {0.0F, 1.0F, 0.0F},
        1.57079632679489661923F,
        1.0F,
        1.0F,
        10.0F
    );
    const std::array parent_spheres{
        RuntimeVisibilitySphere{{0.0F, 0.0F, -5.0F}, 1.0F, 0U},
        RuntimeVisibilitySphere{{100.0F, 0.0F, -5.0F}, 1.0F, 1U},
    };
    const std::array models{math::identity(), math::identity()};

    auto commands = device->create_command_list();
    if (!commands ||
        !meshlet_table.upload(*commands, meshlet_plan, &diagnostics) ||
        !model_table.upload(*commands, models, &diagnostics) ||
        !parent_visibility.cull(*commands, parent_spheres, frustum, &diagnostics) ||
        !cluster_visibility.cull(
            *commands,
            candidate_build.plan.candidates,
            parent_visibility.result(),
            meshlet_table.result(),
            model_table.result(),
            frustum,
            &diagnostics
        ) || !batcher.build(
            *commands,
            cluster_visibility.result().visibility,
            batch_build.plan.candidate_batches,
            batch_build.plan.batches,
            &diagnostics
        )) {
        std::cerr << "Could not encode parent -> cluster -> batch execution: "
                  << diagnostics << '\n';
        return false;
    }

    const auto cluster_result = cluster_visibility.result();
    const auto batch_result = batcher.result();
    if (cluster_result.candidates == nullptr || cluster_result.candidate_count != 4U ||
        cluster_result.instance_count != 2U || cluster_result.visibility.visible_count == nullptr ||
        cluster_result.visibility.visible_indices == nullptr ||
        cluster_result.visibility.capacity < 4U || batch_result.batch_count != 2U ||
        batch_result.compacted_instance_indices == nullptr || batch_result.indirect_arguments == nullptr) {
        std::cerr << "Cluster execution exposed an invalid GPU result contract.\n";
        return false;
    }

    auto cluster_count_readback = make_readback(
        *device, sizeof(std::uint32_t), "Cluster visible count readback"
    );
    auto cluster_indices_readback = make_readback(
        *device,
        static_cast<std::uint64_t>(cluster_result.visibility.capacity) * sizeof(std::uint32_t),
        "Cluster visible indices readback"
    );
    auto candidate_readback = make_readback(
        *device,
        candidate_build.plan.candidates.size() * sizeof(RuntimeGpuClusterCandidate),
        "Cluster candidate identity readback"
    );
    auto compacted_readback = make_readback(
        *device,
        candidate_build.plan.candidates.size() * sizeof(std::uint32_t),
        "Cluster compacted candidate readback"
    );
    auto arguments_readback = make_readback(
        *device,
        batch_result.batch_count * sizeof(render::IndexedIndirectDrawArguments),
        "Cluster indirect arguments readback"
    );
    const std::uint64_t visible_indices_bytes =
        static_cast<std::uint64_t>(cluster_result.visibility.capacity) * sizeof(std::uint32_t);
    const std::uint64_t candidate_bytes =
        candidate_build.plan.candidates.size() * sizeof(RuntimeGpuClusterCandidate);
    const std::uint64_t compacted_bytes =
        candidate_build.plan.candidates.size() * sizeof(std::uint32_t);
    const std::uint64_t argument_bytes =
        batch_result.batch_count * sizeof(render::IndexedIndirectDrawArguments);
    if (!cluster_count_readback || !cluster_indices_readback || !candidate_readback ||
        !compacted_readback || !arguments_readback ||
        !commands->copy_buffer(
            *cluster_result.visibility.visible_count,
            0U,
            *cluster_count_readback,
            0U,
            sizeof(std::uint32_t)
        ) || !commands->copy_buffer(
            *cluster_result.visibility.visible_indices,
            0U,
            *cluster_indices_readback,
            0U,
            visible_indices_bytes
        ) || !commands->copy_buffer(
            *cluster_result.candidates,
            0U,
            *candidate_readback,
            0U,
            candidate_bytes
        ) || !commands->copy_buffer(
            *batch_result.compacted_instance_indices,
            0U,
            *compacted_readback,
            0U,
            compacted_bytes
        ) || !commands->copy_buffer(
            *batch_result.indirect_arguments,
            0U,
            *arguments_readback,
            0U,
            argument_bytes
        )) {
        std::cerr << "Could not encode cluster execution readbacks.\n";
        return false;
    }

    const auto submission = device->submit(std::move(commands));
    if (submission == render::invalid_submission || !device->wait_for_submission(submission)) {
        std::cerr << "Cluster visibility execution submission failed.\n";
        return false;
    }

    std::uint32_t visible_count = 0U;
    std::vector<std::uint32_t> visible_indices(cluster_result.visibility.capacity, 0U);
    std::array<RuntimeGpuClusterCandidate, 4U> gpu_candidates{};
    std::array<std::uint32_t, 4U> compacted{};
    std::array<render::IndexedIndirectDrawArguments, 2U> arguments{};
    if (!cluster_count_readback->read(
            0U, std::as_writable_bytes(std::span{&visible_count, 1})
        ) || !cluster_indices_readback->read(
            0U, std::as_writable_bytes(std::span{visible_indices})
        ) || !candidate_readback->read(
            0U, std::as_writable_bytes(std::span{gpu_candidates})
        ) || !compacted_readback->read(
            0U, std::as_writable_bytes(std::span{compacted})
        ) || !arguments_readback->read(
            0U, std::as_writable_bytes(std::span{arguments})
        )) {
        std::cerr << "Could not read cluster execution outputs.\n";
        return false;
    }

    if (visible_count != 1U || visible_indices[0] != 0U ||
        gpu_candidates[0].scene_instance_index != 0U ||
        gpu_candidates[0].mesh_index != 0U || gpu_candidates[0].meshlet_index != 0U ||
        compacted[0] != 0U ||
        arguments[0].index_count != 3U || arguments[0].first_index != 0U ||
        arguments[0].instance_count != 1U || arguments[0].first_instance != 0U ||
        arguments[1].index_count != 3U || arguments[1].first_index != 3U ||
        arguments[1].instance_count != 0U || arguments[1].first_instance != 2U) {
        std::cerr << "GPU cluster visibility/compaction did not preserve expected candidate identity.\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    if (!test_candidate_and_batch_plans()) return 1;
    if (!test_native_cluster_visibility_to_batches()) return 1;
    return 0;
}
