#include <vice/runtime/gpu_cluster_instance_remap.hpp>

#include "gpu_cluster_instance_remap_shaders.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace vice::runtime {
namespace {

constexpr std::uint32_t cluster_instance_remap_group_size = 64U;

struct GpuClusterInstanceRemapConstants {
    std::uint32_t candidate_count = 0U;
    std::uint32_t remap_count = 0U;
    std::uint32_t padding[2]{};
};

static_assert(sizeof(GpuClusterInstanceRemapConstants) == 16U);

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

struct RuntimeGpuClusterInstanceRemap::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> shader;
    std::unique_ptr<render::ComputePipeline> pipeline;
    std::unique_ptr<render::Buffer> constants;
    std::unique_ptr<render::Buffer> output;
    std::uint32_t capacity = 0U;
    std::uint32_t active_count = 0U;

    [[nodiscard]] bool ensure_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (required == 0U) return true;
        if (required <= capacity && output) return true;
        if (device == nullptr) return false;

        const std::uint32_t next_capacity = grown_capacity(required);
        const std::uint64_t bytes = static_cast<std::uint64_t>(next_capacity) *
            sizeof(std::uint32_t);
        render::BufferDesc output_desc;
        output_desc.size = bytes;
        output_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_source;
        output_desc.memory = render::MemoryLocation::gpu_only;
        output_desc.debug_name = "VICE Runtime compacted cluster scene-instance indices";
        auto next_output = device->create_buffer(output_desc);
        if (!next_output) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate cluster instance remap output.");
            return false;
        }
        output = std::move(next_output);
        capacity = next_capacity;
        return true;
    }
};

RuntimeGpuClusterInstanceRemap::RuntimeGpuClusterInstanceRemap()
    : state_{std::make_unique<State>()} {}
RuntimeGpuClusterInstanceRemap::~RuntimeGpuClusterInstanceRemap() = default;
RuntimeGpuClusterInstanceRemap::RuntimeGpuClusterInstanceRemap(
    RuntimeGpuClusterInstanceRemap&&
) noexcept = default;
RuntimeGpuClusterInstanceRemap& RuntimeGpuClusterInstanceRemap::operator=(
    RuntimeGpuClusterInstanceRemap&&
) noexcept = default;

bool RuntimeGpuClusterInstanceRemap::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU cluster instance remap state is unavailable.");
        return false;
    }

    const bool metal = device.backend() == render::Backend::metal;
    render::ShaderModuleDesc shader_desc;
    shader_desc.stage = render::ShaderStage::compute;
    shader_desc.language = metal
        ? render::ShaderSourceLanguage::metal
        : render::ShaderSourceLanguage::hlsl;
    shader_desc.source = metal
        ? detail::cluster_instance_remap_metal_source
        : detail::cluster_instance_remap_hlsl_source;
    shader_desc.resources = detail::cluster_instance_remap_resources;
    shader_desc.entry_point = "vice_runtime_cluster_instance_remap_cs";
    shader_desc.debug_name = "VICE Runtime cluster instance remap compute";
    auto shader = device.create_shader_module(shader_desc);
    if (!shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the cluster instance remap shader.");
        return false;
    }

    render::ComputePipelineDesc pipeline_desc;
    pipeline_desc.compute_shader = shader.get();
    pipeline_desc.thread_group_size = {cluster_instance_remap_group_size, 1U, 1U};
    pipeline_desc.debug_name = "VICE Runtime cluster instance remap pipeline";
    auto pipeline = device.create_compute_pipeline(pipeline_desc);

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(GpuClusterInstanceRemapConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime cluster instance remap constants";
    auto constants = device.create_buffer(constants_desc);
    if (!pipeline || !constants) {
        set_diagnostics(diagnostics, "VICE Runtime could not create cluster instance remap resources.");
        return false;
    }

    state_->device = &device;
    state_->shader = std::move(shader);
    state_->pipeline = std::move(pipeline);
    state_->constants = std::move(constants);
    state_->output.reset();
    state_->capacity = 0U;
    state_->active_count = 0U;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuClusterInstanceRemap::remap(
    render::CommandList& commands,
    const RuntimeGpuDrawBatchResult& batches,
    const RuntimeGpuClusterVisibilityResult& clusters,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr || !state_->pipeline || !state_->constants) {
        set_diagnostics(diagnostics, "Runtime GPU cluster instance remap is not initialized.");
        return false;
    }
    if (clusters.candidate_count == 0U) {
        state_->active_count = 0U;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (batches.compacted_instance_indices == nullptr || clusters.candidates == nullptr ||
        batches.instance_capacity < clusters.candidate_count) {
        set_diagnostics(diagnostics, "Runtime GPU cluster instance remap requires compatible cluster batch buffers.");
        return false;
    }
    if (!state_->ensure_capacity(clusters.candidate_count, diagnostics)) return false;

    const GpuClusterInstanceRemapConstants constants{
        .candidate_count = clusters.candidate_count,
        .remap_count = clusters.candidate_count,
    };
    if (!state_->constants->write(0U, std::as_bytes(std::span{&constants, 1}))) {
        set_diagnostics(diagnostics, "VICE Runtime could not update cluster instance remap constants.");
        return false;
    }

    const auto compacted_view = structured_view(
        static_cast<std::uint64_t>(clusters.candidate_count) * sizeof(std::uint32_t),
        sizeof(std::uint32_t)
    );
    const auto candidate_view = structured_view(
        static_cast<std::uint64_t>(clusters.candidate_count) * sizeof(RuntimeGpuClusterCandidate),
        sizeof(RuntimeGpuClusterCandidate)
    );
    const auto output_view = structured_view(
        static_cast<std::uint64_t>(state_->capacity) * sizeof(std::uint32_t),
        sizeof(std::uint32_t)
    );
    const std::uint32_t group_count =
        (clusters.candidate_count + cluster_instance_remap_group_size - 1U) /
        cluster_instance_remap_group_size;

    if (!commands.set_compute_pipeline(*state_->pipeline) ||
        !commands.set_compute_constant_buffer(
            detail::cluster_instance_remap_constants_binding,
            *state_->constants,
            0U,
            sizeof(GpuClusterInstanceRemapConstants)
        ) || !commands.set_compute_storage_buffer(
            detail::cluster_instance_remap_compacted_binding,
            *batches.compacted_instance_indices,
            compacted_view
        ) || !commands.set_compute_storage_buffer(
            detail::cluster_instance_remap_candidates_binding,
            *clusters.candidates,
            candidate_view
        ) || !commands.set_compute_storage_buffer(
            detail::cluster_instance_remap_output_binding,
            *state_->output,
            output_view
        ) || !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = group_count,
            .group_count_y = 1U,
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode cluster instance remap compute.");
        return false;
    }

    state_->active_count = clusters.candidate_count;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeGpuClusterInstanceRemapResult RuntimeGpuClusterInstanceRemap::result() const noexcept {
    if (state_ == nullptr) return {};
    return RuntimeGpuClusterInstanceRemapResult{
        .scene_instance_indices = state_->output.get(),
        .capacity = state_->active_count,
    };
}

} // namespace vice::runtime
