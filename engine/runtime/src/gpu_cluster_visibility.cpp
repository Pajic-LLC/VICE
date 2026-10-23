#include <vice/runtime/gpu_cluster_visibility.hpp>

#include "gpu_cluster_visibility_shaders.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

constexpr std::uint32_t cluster_visibility_group_size = 64U;

struct GpuClusterVisibilityConstants {
    float frustum_planes[6][4]{};
    std::uint32_t candidate_count = 0U;
    std::uint32_t instance_count = 0U;
    std::uint32_t meshlet_count = 0U;
    std::uint32_t padding = 0U;
};

static_assert(sizeof(GpuClusterVisibilityConstants) == 112U);

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] std::uint32_t grown_capacity(const std::uint32_t required) noexcept {
    if (required <= 1U) return 1U;
    std::uint32_t capacity = 1U;
    while (capacity < required && capacity <= std::numeric_limits<std::uint32_t>::max() / 2U) {
        capacity *= 2U;
    }
    return std::max(capacity, required);
}

[[nodiscard]] render::BufferViewDesc structured_view(
    const std::uint64_t size,
    const std::uint32_t stride
) noexcept {
    return render::BufferViewDesc{
        .offset = 0U,
        .size = size,
        .structure_stride = stride,
    };
}

} // namespace

RuntimeGpuClusterCandidateBuildResult build_runtime_gpu_cluster_candidate_plan(
    const std::span<const RenderMeshInstance> instances,
    const RuntimeGpuMeshletTablePlan& meshlets
) {
    RuntimeGpuClusterCandidateBuildResult result;
    if (instances.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime GPU cluster candidates exceed the scene-instance index contract.";
        return result;
    }
    if (meshlets.meshes.size() != meshlets.mesh_ids.size()) {
        result.diagnostics = "Runtime GPU cluster candidates require aligned mesh records and IDs.";
        return result;
    }

    result.plan.instance_ranges.resize(instances.size());
    for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
        const auto& instance = instances[instance_index];
        const auto mesh_index = meshlets.mesh_index(instance.mesh);
        if (!mesh_index.has_value() || *mesh_index >= meshlets.meshes.size()) {
            result.diagnostics = "Runtime GPU cluster candidates reference a mesh outside the GPU mesh table.";
            return result;
        }
        const auto& mesh = meshlets.meshes[*mesh_index];
        const std::uint64_t meshlet_end = static_cast<std::uint64_t>(mesh.first_meshlet) +
            mesh.meshlet_count;
        if (meshlet_end > meshlets.meshlets.size()) {
            result.diagnostics = "Runtime GPU cluster candidates rejected an invalid mesh-to-cluster range.";
            return result;
        }
        if (result.plan.candidates.size() > std::numeric_limits<std::uint32_t>::max() ||
            mesh.meshlet_count > std::numeric_limits<std::uint32_t>::max() -
                static_cast<std::uint32_t>(result.plan.candidates.size())) {
            result.diagnostics = "Runtime GPU cluster candidate stream exceeds 32-bit indexing.";
            return result;
        }

        auto& range = result.plan.instance_ranges[instance_index];
        range.first_candidate = static_cast<std::uint32_t>(result.plan.candidates.size());
        range.candidate_count = mesh.meshlet_count;
        result.plan.candidates.reserve(result.plan.candidates.size() + mesh.meshlet_count);
        for (std::uint32_t local_meshlet = 0U; local_meshlet < mesh.meshlet_count; ++local_meshlet) {
            const std::uint32_t global_meshlet = mesh.first_meshlet + local_meshlet;
            if (meshlets.meshlets[global_meshlet].mesh_index != *mesh_index) {
                result.diagnostics = "Runtime GPU cluster candidates found inconsistent mesh/cluster identity.";
                return result;
            }
            result.plan.candidates.push_back(RuntimeGpuClusterCandidate{
                .scene_instance_index = static_cast<std::uint32_t>(instance_index),
                .mesh_index = *mesh_index,
                .meshlet_index = global_meshlet,
            });
        }
    }
    return result;
}

RuntimeGpuClusterBatchBuildResult build_runtime_gpu_cluster_batch_plan(
    const RuntimeGpuClusterCandidatePlan& candidates,
    const RuntimeGpuMeshletTablePlan& meshlets,
    const std::span<const std::uint8_t> eligible_scene_instances
) {
    RuntimeGpuClusterBatchBuildResult result;
    if (eligible_scene_instances.size() != candidates.instance_ranges.size()) {
        result.diagnostics = "Runtime GPU cluster batching requires one eligibility value per scene instance.";
        return result;
    }
    if (meshlets.meshes.size() != meshlets.mesh_ids.size()) {
        result.diagnostics = "Runtime GPU cluster batching requires aligned mesh records and IDs.";
        return result;
    }
    if (candidates.candidates.size() > std::numeric_limits<std::uint32_t>::max() ||
        meshlets.meshlets.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime GPU cluster batching exceeds the 32-bit identity contract.";
        return result;
    }

    std::vector<std::uint8_t> used_meshlets(meshlets.meshlets.size(), 0U);
    std::size_t expected_candidate = 0U;
    for (std::size_t instance_index = 0U; instance_index < candidates.instance_ranges.size(); ++instance_index) {
        const auto& range = candidates.instance_ranges[instance_index];
        if (range.first_candidate != expected_candidate ||
            static_cast<std::uint64_t>(range.first_candidate) + range.candidate_count >
                candidates.candidates.size()) {
            result.diagnostics = "Runtime GPU cluster batching rejected non-canonical candidate ranges.";
            return result;
        }
        for (std::uint32_t local = 0U; local < range.candidate_count; ++local) {
            const auto& candidate = candidates.candidates[range.first_candidate + local];
            if (candidate.scene_instance_index != instance_index ||
                candidate.mesh_index >= meshlets.meshes.size() ||
                candidate.meshlet_index >= meshlets.meshlets.size() ||
                meshlets.meshlets[candidate.meshlet_index].mesh_index != candidate.mesh_index) {
                result.diagnostics = "Runtime GPU cluster batching rejected inconsistent candidate identity.";
                return result;
            }
            if (eligible_scene_instances[instance_index] != 0U) {
                used_meshlets[candidate.meshlet_index] = 1U;
            }
        }
        expected_candidate += range.candidate_count;
    }
    if (expected_candidate != candidates.candidates.size()) {
        result.diagnostics = "Runtime GPU cluster batching candidate ranges do not cover the candidate stream.";
        return result;
    }

    std::vector<std::uint32_t> meshlet_to_batch(
        meshlets.meshlets.size(), runtime_gpu_draw_batch_unassigned
    );
    result.plan.batches.reserve(meshlets.meshlets.size());
    result.plan.batch_meshes.reserve(meshlets.meshlets.size());
    result.plan.batch_meshlets.reserve(meshlets.meshlets.size());
    for (std::uint32_t meshlet_index = 0U;
         meshlet_index < meshlets.meshlets.size();
         ++meshlet_index) {
        if (used_meshlets[meshlet_index] == 0U) continue;
        const auto& meshlet = meshlets.meshlets[meshlet_index];
        if (meshlet.mesh_index >= meshlets.mesh_ids.size() ||
            meshlet.source_index_count == 0U ||
            meshlet.source_index_count % 3U != 0U) {
            result.diagnostics = "Runtime GPU cluster batching rejected invalid cluster draw metadata.";
            return result;
        }
        const std::uint32_t batch_index = static_cast<std::uint32_t>(result.plan.batches.size());
        meshlet_to_batch[meshlet_index] = batch_index;
        result.plan.batches.push_back(RuntimeGpuDrawBatch{
            .index_count = meshlet.source_index_count,
            .first_index = meshlet.source_first_index,
            .vertex_offset = 0,
        });
        result.plan.batch_meshes.push_back(meshlets.mesh_ids[meshlet.mesh_index]);
        result.plan.batch_meshlets.push_back(meshlet_index);
    }

    result.plan.candidate_batches.assign(
        candidates.candidates.size(), runtime_gpu_draw_batch_unassigned
    );
    for (std::size_t candidate_index = 0U;
         candidate_index < candidates.candidates.size();
         ++candidate_index) {
        const auto& candidate = candidates.candidates[candidate_index];
        if (eligible_scene_instances[candidate.scene_instance_index] == 0U) continue;
        const std::uint32_t batch_index = meshlet_to_batch[candidate.meshlet_index];
        if (batch_index == runtime_gpu_draw_batch_unassigned) {
            result.diagnostics = "Runtime GPU cluster batching could not resolve an eligible cluster batch.";
            return result;
        }
        result.plan.candidate_batches[candidate_index] = batch_index;
    }
    return result;
}

struct RuntimeGpuClusterVisibility::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> scatter_shader;
    std::unique_ptr<render::ShaderModule> cull_shader;
    std::unique_ptr<render::ComputePipeline> scatter_pipeline;
    std::unique_ptr<render::ComputePipeline> cull_pipeline;
    std::unique_ptr<render::Buffer> constants;
    std::unique_ptr<render::Buffer> zero_count_upload;
    std::unique_ptr<render::Buffer> visible_count;
    std::unique_ptr<render::Buffer> candidate_upload;
    std::unique_ptr<render::Buffer> candidates;
    std::unique_ptr<render::Buffer> visible_indices;
    std::unique_ptr<render::Buffer> parent_zero_upload;
    std::unique_ptr<render::Buffer> parent_visible_flags;
    std::uint32_t candidate_capacity = 0U;
    std::uint32_t instance_capacity = 0U;
    std::uint32_t candidate_count = 0U;
    std::uint32_t instance_count = 0U;

    [[nodiscard]] bool ensure_candidate_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (required == 0U) return true;
        if (required <= candidate_capacity && candidate_upload && candidates && visible_indices) {
            return true;
        }
        if (device == nullptr) return false;
        const std::uint32_t next_capacity = grown_capacity(required);
        const std::uint64_t candidate_bytes = static_cast<std::uint64_t>(next_capacity) *
            sizeof(RuntimeGpuClusterCandidate);
        const std::uint64_t index_bytes = static_cast<std::uint64_t>(next_capacity) *
            sizeof(std::uint32_t);

        render::BufferDesc upload_desc;
        upload_desc.size = candidate_bytes;
        upload_desc.usage = render::BufferUsage::copy_source;
        upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
        upload_desc.debug_name = "VICE Runtime GPU cluster candidate upload";
        auto next_upload = device->create_buffer(upload_desc);

        render::BufferDesc candidate_desc;
        candidate_desc.size = candidate_bytes;
        candidate_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination |
            render::BufferUsage::copy_source;
        candidate_desc.memory = render::MemoryLocation::gpu_only;
        candidate_desc.debug_name = "VICE Runtime GPU cluster candidates";
        auto next_candidates = device->create_buffer(candidate_desc);

        render::BufferDesc output_desc;
        output_desc.size = index_bytes;
        output_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_source;
        output_desc.memory = render::MemoryLocation::gpu_only;
        output_desc.debug_name = "VICE Runtime visible cluster candidate indices";
        auto next_visible_indices = device->create_buffer(output_desc);
        if (!next_upload || !next_candidates || !next_visible_indices) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate GPU cluster candidate buffers.");
            return false;
        }
        candidate_upload = std::move(next_upload);
        candidates = std::move(next_candidates);
        visible_indices = std::move(next_visible_indices);
        candidate_capacity = next_capacity;
        return true;
    }

    [[nodiscard]] bool ensure_instance_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (required == 0U) return true;
        if (required <= instance_capacity && parent_zero_upload && parent_visible_flags) return true;
        if (device == nullptr) return false;
        const std::uint32_t next_capacity = grown_capacity(required);
        const std::uint64_t bytes = static_cast<std::uint64_t>(next_capacity) * sizeof(std::uint32_t);

        render::BufferDesc zero_desc;
        zero_desc.size = bytes;
        zero_desc.usage = render::BufferUsage::copy_source;
        zero_desc.memory = render::MemoryLocation::cpu_to_gpu;
        zero_desc.debug_name = "VICE Runtime cluster parent visibility zero upload";
        auto next_zero = device->create_buffer(zero_desc);

        render::BufferDesc flags_desc;
        flags_desc.size = bytes;
        flags_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination |
            render::BufferUsage::copy_source;
        flags_desc.memory = render::MemoryLocation::gpu_only;
        flags_desc.debug_name = "VICE Runtime cluster parent visibility flags";
        auto next_flags = device->create_buffer(flags_desc);
        std::vector<std::uint32_t> zeros(next_capacity, 0U);
        if (!next_zero || !next_flags ||
            !next_zero->write(0U, std::as_bytes(std::span{zeros}))) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate cluster parent visibility flags.");
            return false;
        }
        parent_zero_upload = std::move(next_zero);
        parent_visible_flags = std::move(next_flags);
        instance_capacity = next_capacity;
        return true;
    }
};

RuntimeGpuClusterVisibility::RuntimeGpuClusterVisibility() : state_{std::make_unique<State>()} {}
RuntimeGpuClusterVisibility::~RuntimeGpuClusterVisibility() = default;
RuntimeGpuClusterVisibility::RuntimeGpuClusterVisibility(RuntimeGpuClusterVisibility&&) noexcept = default;
RuntimeGpuClusterVisibility& RuntimeGpuClusterVisibility::operator=(RuntimeGpuClusterVisibility&&) noexcept = default;

bool RuntimeGpuClusterVisibility::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU cluster visibility state is unavailable.");
        return false;
    }

    const bool metal = device.backend() == render::Backend::metal;
    render::ShaderModuleDesc scatter_desc;
    scatter_desc.stage = render::ShaderStage::compute;
    scatter_desc.language = metal
        ? render::ShaderSourceLanguage::metal
        : render::ShaderSourceLanguage::hlsl;
    scatter_desc.source = metal
        ? detail::cluster_visibility_metal_source
        : detail::cluster_visibility_hlsl_source;
    scatter_desc.resources = detail::cluster_visibility_scatter_resources;
    scatter_desc.entry_point = "vice_runtime_cluster_parent_scatter_cs";
    scatter_desc.debug_name = "VICE Runtime cluster parent scatter compute";
    auto scatter_shader = device.create_shader_module(scatter_desc);

    render::ShaderModuleDesc cull_desc;
    cull_desc.stage = render::ShaderStage::compute;
    cull_desc.language = scatter_desc.language;
    cull_desc.source = scatter_desc.source;
    cull_desc.resources = detail::cluster_visibility_cull_resources;
    cull_desc.entry_point = "vice_runtime_cluster_visibility_cs";
    cull_desc.debug_name = "VICE Runtime cluster visibility compute";
    auto cull_shader = device.create_shader_module(cull_desc);
    if (!scatter_shader || !cull_shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create GPU cluster visibility shaders.");
        return false;
    }

    render::ComputePipelineDesc scatter_pipeline_desc;
    scatter_pipeline_desc.compute_shader = scatter_shader.get();
    scatter_pipeline_desc.thread_group_size = {cluster_visibility_group_size, 1U, 1U};
    scatter_pipeline_desc.debug_name = "VICE Runtime cluster parent scatter pipeline";
    auto scatter_pipeline = device.create_compute_pipeline(scatter_pipeline_desc);

    render::ComputePipelineDesc cull_pipeline_desc;
    cull_pipeline_desc.compute_shader = cull_shader.get();
    cull_pipeline_desc.thread_group_size = {cluster_visibility_group_size, 1U, 1U};
    cull_pipeline_desc.debug_name = "VICE Runtime cluster visibility pipeline";
    auto cull_pipeline = device.create_compute_pipeline(cull_pipeline_desc);
    if (!scatter_pipeline || !cull_pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create GPU cluster visibility pipelines.");
        return false;
    }

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(GpuClusterVisibilityConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime GPU cluster visibility constants";
    auto constants = device.create_buffer(constants_desc);

    render::BufferDesc zero_desc;
    zero_desc.size = sizeof(std::uint32_t);
    zero_desc.usage = render::BufferUsage::copy_source;
    zero_desc.memory = render::MemoryLocation::cpu_to_gpu;
    zero_desc.debug_name = "VICE Runtime GPU cluster visibility zero upload";
    auto zero_count_upload = device.create_buffer(zero_desc);

    render::BufferDesc count_desc;
    count_desc.size = sizeof(std::uint32_t);
    count_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_source |
        render::BufferUsage::copy_destination;
    count_desc.memory = render::MemoryLocation::gpu_only;
    count_desc.debug_name = "VICE Runtime visible cluster candidate count";
    auto visible_count = device.create_buffer(count_desc);
    const std::uint32_t zero = 0U;
    if (!constants || !zero_count_upload || !visible_count ||
        !zero_count_upload->write(0U, std::as_bytes(std::span{&zero, 1}))) {
        set_diagnostics(diagnostics, "VICE Runtime could not create cluster visibility control buffers.");
        return false;
    }

    state_->device = &device;
    state_->scatter_shader = std::move(scatter_shader);
    state_->cull_shader = std::move(cull_shader);
    state_->scatter_pipeline = std::move(scatter_pipeline);
    state_->cull_pipeline = std::move(cull_pipeline);
    state_->constants = std::move(constants);
    state_->zero_count_upload = std::move(zero_count_upload);
    state_->visible_count = std::move(visible_count);
    state_->candidate_upload.reset();
    state_->candidates.reset();
    state_->visible_indices.reset();
    state_->parent_zero_upload.reset();
    state_->parent_visible_flags.reset();
    state_->candidate_capacity = 0U;
    state_->instance_capacity = 0U;
    state_->candidate_count = 0U;
    state_->instance_count = 0U;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuClusterVisibility::cull(
    render::CommandList& commands,
    const std::span<const RuntimeGpuClusterCandidate> candidate_span,
    const RuntimeVisibilityGpuResult& parent_visibility,
    const RuntimeGpuMeshletTableResult& meshlet_table,
    const RuntimeGpuInstanceTableResult& camera_relative_instances,
    const RuntimeVisibilityFrustum& frustum,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr || !state_->scatter_pipeline ||
        !state_->cull_pipeline || !state_->constants || !state_->zero_count_upload ||
        !state_->visible_count) {
        set_diagnostics(diagnostics, "Runtime GPU cluster visibility is not initialized.");
        return false;
    }
    if (!valid(frustum)) {
        set_diagnostics(diagnostics, "Runtime GPU cluster visibility rejected an invalid frustum.");
        return false;
    }
    if (candidate_span.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(diagnostics, "Runtime GPU cluster candidate count exceeds 32-bit indexing.");
        return false;
    }

    const std::uint32_t instance_count = camera_relative_instances.instance_count;
    const std::uint32_t candidate_count = static_cast<std::uint32_t>(candidate_span.size());
    if (!commands.copy_buffer(
            *state_->zero_count_upload,
            0U,
            *state_->visible_count,
            0U,
            sizeof(std::uint32_t)
        )) {
        set_diagnostics(diagnostics, "VICE Runtime could not reset cluster visibility count.");
        return false;
    }
    state_->candidate_count = candidate_count;
    state_->instance_count = instance_count;
    if (candidate_span.empty()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    if (instance_count == 0U || camera_relative_instances.models == nullptr ||
        parent_visibility.visible_count == nullptr || parent_visibility.visible_indices == nullptr ||
        parent_visibility.capacity < instance_count || meshlet_table.meshlets == nullptr ||
        meshlet_table.meshlet_count == 0U) {
        set_diagnostics(diagnostics, "Runtime GPU cluster visibility requires compatible parent, meshlet, and model tables.");
        return false;
    }
    for (const auto& candidate : candidate_span) {
        if (candidate.scene_instance_index >= instance_count ||
            candidate.mesh_index >= meshlet_table.mesh_count ||
            candidate.meshlet_index >= meshlet_table.meshlet_count) {
            set_diagnostics(diagnostics, "Runtime GPU cluster visibility rejected an out-of-range candidate identity.");
            return false;
        }
    }
    if (!state_->ensure_candidate_capacity(candidate_count, diagnostics) ||
        !state_->ensure_instance_capacity(instance_count, diagnostics)) {
        return false;
    }

    GpuClusterVisibilityConstants constants{};
    for (std::size_t index = 0U; index < frustum.planes.size(); ++index) {
        constants.frustum_planes[index][0] = frustum.planes[index].normal.x;
        constants.frustum_planes[index][1] = frustum.planes[index].normal.y;
        constants.frustum_planes[index][2] = frustum.planes[index].normal.z;
        constants.frustum_planes[index][3] = frustum.planes[index].distance;
    }
    constants.candidate_count = candidate_count;
    constants.instance_count = instance_count;
    constants.meshlet_count = meshlet_table.meshlet_count;
    const auto candidate_bytes = std::as_bytes(candidate_span);
    const std::uint64_t parent_flag_bytes = static_cast<std::uint64_t>(state_->instance_capacity) *
        sizeof(std::uint32_t);
    if (!state_->constants->write(0U, std::as_bytes(std::span{&constants, 1})) ||
        !state_->candidate_upload->write(0U, candidate_bytes) ||
        !commands.copy_buffer(
            *state_->candidate_upload, 0U, *state_->candidates, 0U, candidate_bytes.size()
        ) || !commands.copy_buffer(
            *state_->parent_zero_upload, 0U, *state_->parent_visible_flags, 0U, parent_flag_bytes
        )) {
        set_diagnostics(diagnostics, "VICE Runtime could not upload/reset GPU cluster visibility data.");
        return false;
    }

    const auto count_view = structured_view(sizeof(std::uint32_t), sizeof(std::uint32_t));
    const auto parent_indices_view = structured_view(
        static_cast<std::uint64_t>(parent_visibility.capacity) * sizeof(std::uint32_t),
        sizeof(std::uint32_t)
    );
    const auto parent_flags_view = structured_view(parent_flag_bytes, sizeof(std::uint32_t));
    const auto candidates_view = structured_view(candidate_bytes.size(), sizeof(RuntimeGpuClusterCandidate));
    const auto meshlets_view = structured_view(
        static_cast<std::uint64_t>(meshlet_table.meshlet_count) * sizeof(RuntimeGpuMeshletRecord),
        sizeof(RuntimeGpuMeshletRecord)
    );
    const auto models_view = structured_view(
        static_cast<std::uint64_t>(instance_count) * sizeof(math::Mat4),
        sizeof(math::Mat4)
    );
    const auto output_indices_view = structured_view(
        static_cast<std::uint64_t>(state_->candidate_capacity) * sizeof(std::uint32_t),
        sizeof(std::uint32_t)
    );

    const std::uint32_t scatter_groups =
        (instance_count + cluster_visibility_group_size - 1U) / cluster_visibility_group_size;
    if (!commands.set_compute_pipeline(*state_->scatter_pipeline) ||
        !commands.set_compute_constant_buffer(
            detail::cluster_visibility_constants_binding,
            *state_->constants,
            0U,
            sizeof(GpuClusterVisibilityConstants)
        ) || !commands.set_compute_storage_buffer(
            detail::cluster_visibility_parent_count_binding,
            *parent_visibility.visible_count,
            count_view
        ) || !commands.set_compute_storage_buffer(
            detail::cluster_visibility_parent_indices_binding,
            *parent_visibility.visible_indices,
            parent_indices_view
        ) || !commands.set_compute_storage_buffer(
            detail::cluster_visibility_parent_flags_write_binding,
            *state_->parent_visible_flags,
            parent_flags_view
        ) || !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = scatter_groups,
            .group_count_y = 1U,
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode cluster parent visibility scatter.");
        return false;
    }

    const std::uint32_t cull_groups =
        (candidate_count + cluster_visibility_group_size - 1U) / cluster_visibility_group_size;
    if (!commands.set_compute_pipeline(*state_->cull_pipeline) ||
        !commands.set_compute_constant_buffer(
            detail::cluster_visibility_constants_binding,
            *state_->constants,
            0U,
            sizeof(GpuClusterVisibilityConstants)
        ) || !commands.set_compute_storage_buffer(
            detail::cluster_visibility_candidates_binding,
            *state_->candidates,
            candidates_view
        ) || !commands.set_compute_storage_buffer(
            detail::cluster_visibility_meshlets_binding,
            *meshlet_table.meshlets,
            meshlets_view
        ) || !commands.set_compute_storage_buffer(
            detail::cluster_visibility_models_binding,
            *camera_relative_instances.models,
            models_view
        ) || !commands.set_compute_storage_buffer(
            detail::cluster_visibility_parent_flags_read_binding,
            *state_->parent_visible_flags,
            parent_flags_view
        ) || !commands.set_compute_storage_buffer(
            detail::cluster_visibility_output_count_binding,
            *state_->visible_count,
            count_view
        ) || !commands.set_compute_storage_buffer(
            detail::cluster_visibility_output_indices_binding,
            *state_->visible_indices,
            output_indices_view
        ) || !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = cull_groups,
            .group_count_y = 1U,
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode GPU cluster visibility culling.");
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeGpuClusterVisibilityResult RuntimeGpuClusterVisibility::result() const noexcept {
    if (state_ == nullptr) return {};
    return RuntimeGpuClusterVisibilityResult{
        .visibility = RuntimeVisibilityGpuResult{
            .visible_count = state_->visible_count.get(),
            .visible_indices = state_->visible_indices.get(),
            .capacity = state_->candidate_capacity,
        },
        .candidates = state_->candidates.get(),
        .candidate_count = state_->candidate_count,
        .instance_count = state_->instance_count,
    };
}

} // namespace vice::runtime
