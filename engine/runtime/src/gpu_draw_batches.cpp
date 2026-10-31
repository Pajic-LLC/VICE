#include <vice/runtime/gpu_draw_batches.hpp>

#include "gpu_draw_batch_shaders.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

constexpr std::uint32_t gpu_batch_thread_group_size = 64U;

struct GpuBatchConstants {
    std::uint32_t instance_count = 0U;
    std::uint32_t batch_count = 0U;
    std::uint32_t visible_capacity = 0U;
    std::uint32_t padding = 0U;
};

struct GpuBatchMetadata {
    std::uint32_t index_count = 0U;
    std::uint32_t first_index = 0U;
    std::int32_t vertex_offset = 0;
    std::uint32_t output_offset = 0U;
    std::uint32_t capacity = 0U;
    std::uint32_t padding[3]{};
};

static_assert(sizeof(GpuBatchConstants) == 16U);
static_assert(sizeof(GpuBatchMetadata) == 32U);
static_assert(sizeof(render::IndexedIndirectDrawArguments) == 20U);

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

struct RuntimeGpuDrawBatcher::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> compact_shader;
    std::unique_ptr<render::ShaderModule> finalize_shader;
    std::unique_ptr<render::ComputePipeline> compact_pipeline;
    std::unique_ptr<render::ComputePipeline> finalize_pipeline;
    std::unique_ptr<render::Buffer> constants;
    std::unique_ptr<render::Buffer> assignments_upload;
    std::unique_ptr<render::Buffer> assignments_gpu;
    std::unique_ptr<render::Buffer> metadata_upload;
    std::unique_ptr<render::Buffer> metadata_gpu;
    std::unique_ptr<render::Buffer> zero_upload;
    std::unique_ptr<render::Buffer> batch_counts;
    std::unique_ptr<render::Buffer> compacted_instances;
    std::unique_ptr<render::Buffer> indirect_arguments;
    std::uint32_t instance_capacity = 0U;
    std::uint32_t batch_capacity = 0U;
    std::uint32_t batch_count = 0U;
    std::vector<RuntimeGpuDrawBatchRange> ranges;

    [[nodiscard]] bool ensure_capacity(
        const std::uint32_t required_instances,
        const std::uint32_t required_batches,
        std::string* const diagnostics
    ) {
        if (device == nullptr || required_instances == 0U || required_batches == 0U) return false;
        if (required_instances <= instance_capacity && required_batches <= batch_capacity &&
            assignments_upload && assignments_gpu && metadata_upload && metadata_gpu && zero_upload &&
            batch_counts && compacted_instances && indirect_arguments) {
            return true;
        }

        const std::uint32_t new_instance_capacity = grown_capacity(
            std::max(required_instances, instance_capacity)
        );
        const std::uint32_t new_batch_capacity = grown_capacity(
            std::max(required_batches, batch_capacity)
        );
        const std::uint64_t assignment_size =
            static_cast<std::uint64_t>(new_instance_capacity) * sizeof(std::uint32_t);
        const std::uint64_t metadata_size =
            static_cast<std::uint64_t>(new_batch_capacity) * sizeof(GpuBatchMetadata);
        const std::uint64_t count_size =
            static_cast<std::uint64_t>(new_batch_capacity) * sizeof(std::uint32_t);
        const std::uint64_t indirect_size =
            static_cast<std::uint64_t>(new_batch_capacity) *
            sizeof(render::IndexedIndirectDrawArguments);

        render::BufferDesc upload_desc;
        upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
        upload_desc.usage = render::BufferUsage::copy_source;

        upload_desc.size = assignment_size;
        upload_desc.debug_name = "VICE Runtime GPU batch assignment upload";
        auto new_assignments_upload = device->create_buffer(upload_desc);

        render::BufferDesc assignment_desc;
        assignment_desc.size = assignment_size;
        assignment_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination;
        assignment_desc.memory = render::MemoryLocation::gpu_only;
        assignment_desc.debug_name = "VICE Runtime GPU batch assignments";
        auto new_assignments_gpu = device->create_buffer(assignment_desc);

        upload_desc.size = metadata_size;
        upload_desc.debug_name = "VICE Runtime GPU batch metadata upload";
        auto new_metadata_upload = device->create_buffer(upload_desc);

        render::BufferDesc metadata_desc;
        metadata_desc.size = metadata_size;
        metadata_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination;
        metadata_desc.memory = render::MemoryLocation::gpu_only;
        metadata_desc.debug_name = "VICE Runtime GPU batch metadata";
        auto new_metadata_gpu = device->create_buffer(metadata_desc);

        upload_desc.size = count_size;
        upload_desc.debug_name = "VICE Runtime GPU batch zero upload";
        auto new_zero_upload = device->create_buffer(upload_desc);

        render::BufferDesc count_desc;
        count_desc.size = count_size;
        count_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_source |
            render::BufferUsage::copy_destination;
        count_desc.memory = render::MemoryLocation::gpu_only;
        count_desc.debug_name = "VICE Runtime GPU batch visible counts";
        auto new_batch_counts = device->create_buffer(count_desc);

        render::BufferDesc compacted_desc;
        compacted_desc.size = assignment_size;
        compacted_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_source;
        compacted_desc.memory = render::MemoryLocation::gpu_only;
        compacted_desc.debug_name = "VICE Runtime GPU batch compacted instances";
        auto new_compacted_instances = device->create_buffer(compacted_desc);

        render::BufferDesc indirect_desc;
        indirect_desc.size = indirect_size;
        indirect_desc.usage = render::BufferUsage::storage | render::BufferUsage::indirect |
            render::BufferUsage::copy_source;
        indirect_desc.memory = render::MemoryLocation::gpu_only;
        indirect_desc.debug_name = "VICE Runtime GPU batch indirect arguments";
        auto new_indirect_arguments = device->create_buffer(indirect_desc);

        if (!new_assignments_upload || !new_assignments_gpu || !new_metadata_upload ||
            !new_metadata_gpu || !new_zero_upload || !new_batch_counts ||
            !new_compacted_instances || !new_indirect_arguments) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate GPU draw batch buffers.");
            return false;
        }

        assignments_upload = std::move(new_assignments_upload);
        assignments_gpu = std::move(new_assignments_gpu);
        metadata_upload = std::move(new_metadata_upload);
        metadata_gpu = std::move(new_metadata_gpu);
        zero_upload = std::move(new_zero_upload);
        batch_counts = std::move(new_batch_counts);
        compacted_instances = std::move(new_compacted_instances);
        indirect_arguments = std::move(new_indirect_arguments);
        instance_capacity = new_instance_capacity;
        batch_capacity = new_batch_capacity;
        return true;
    }
};

RuntimeGpuDrawBatcher::RuntimeGpuDrawBatcher() : state_{std::make_unique<State>()} {}
RuntimeGpuDrawBatcher::~RuntimeGpuDrawBatcher() = default;
RuntimeGpuDrawBatcher::RuntimeGpuDrawBatcher(RuntimeGpuDrawBatcher&&) noexcept = default;
RuntimeGpuDrawBatcher& RuntimeGpuDrawBatcher::operator=(RuntimeGpuDrawBatcher&&) noexcept = default;

bool RuntimeGpuDrawBatcher::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU draw batch state is unavailable.");
        return false;
    }

    const bool metal = device.backend() == render::Backend::metal;
    const auto language = metal
        ? render::ShaderSourceLanguage::metal
        : render::ShaderSourceLanguage::hlsl;
    const auto source = metal ? detail::gpu_batch_metal_source : detail::gpu_batch_hlsl_source;

    render::ShaderModuleDesc compact_desc;
    compact_desc.stage = render::ShaderStage::compute;
    compact_desc.language = language;
    compact_desc.source = source;
    compact_desc.resources = detail::gpu_batch_compact_resources;
    compact_desc.entry_point = "vice_runtime_batch_compact_cs";
    compact_desc.debug_name = "VICE Runtime GPU batch compaction";
    auto compact_shader = device.create_shader_module(compact_desc);

    render::ShaderModuleDesc finalize_desc;
    finalize_desc.stage = render::ShaderStage::compute;
    finalize_desc.language = language;
    finalize_desc.source = source;
    finalize_desc.resources = detail::gpu_batch_finalize_resources;
    finalize_desc.entry_point = "vice_runtime_batch_finalize_cs";
    finalize_desc.debug_name = "VICE Runtime GPU batch indirect finalize";
    auto finalize_shader = device.create_shader_module(finalize_desc);

    if (!compact_shader || !finalize_shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create GPU draw batch shaders.");
        return false;
    }

    render::ComputePipelineDesc compact_pipeline_desc;
    compact_pipeline_desc.compute_shader = compact_shader.get();
    compact_pipeline_desc.thread_group_size = {gpu_batch_thread_group_size, 1U, 1U};
    compact_pipeline_desc.debug_name = "VICE Runtime GPU batch compaction pipeline";
    auto compact_pipeline = device.create_compute_pipeline(compact_pipeline_desc);

    render::ComputePipelineDesc finalize_pipeline_desc;
    finalize_pipeline_desc.compute_shader = finalize_shader.get();
    finalize_pipeline_desc.thread_group_size = {gpu_batch_thread_group_size, 1U, 1U};
    finalize_pipeline_desc.debug_name = "VICE Runtime GPU batch finalize pipeline";
    auto finalize_pipeline = device.create_compute_pipeline(finalize_pipeline_desc);

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(GpuBatchConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime GPU batch constants";
    auto constants = device.create_buffer(constants_desc);

    if (!compact_pipeline || !finalize_pipeline || !constants) {
        set_diagnostics(diagnostics, "VICE Runtime could not create GPU draw batch execution state.");
        return false;
    }

    state_->device = &device;
    state_->compact_shader = std::move(compact_shader);
    state_->finalize_shader = std::move(finalize_shader);
    state_->compact_pipeline = std::move(compact_pipeline);
    state_->finalize_pipeline = std::move(finalize_pipeline);
    state_->constants = std::move(constants);
    state_->assignments_upload.reset();
    state_->assignments_gpu.reset();
    state_->metadata_upload.reset();
    state_->metadata_gpu.reset();
    state_->zero_upload.reset();
    state_->batch_counts.reset();
    state_->compacted_instances.reset();
    state_->indirect_arguments.reset();
    state_->instance_capacity = 0U;
    state_->batch_capacity = 0U;
    state_->batch_count = 0U;
    state_->ranges.clear();
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuDrawBatcher::build(
    render::CommandList& commands,
    const RuntimeVisibilityGpuResult& visibility,
    const std::span<const std::uint32_t> instance_batches,
    const std::span<const RuntimeGpuDrawBatch> batches,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr || !state_->compact_pipeline ||
        !state_->finalize_pipeline || !state_->constants) {
        set_diagnostics(diagnostics, "Runtime GPU draw batcher is not initialized.");
        return false;
    }
    if (instance_batches.empty() || batches.empty()) {
        set_diagnostics(diagnostics, "Runtime GPU draw batches require instances and at least one batch.");
        return false;
    }
    if (instance_batches.size() > std::numeric_limits<std::uint32_t>::max() ||
        batches.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(diagnostics, "Runtime GPU draw batch input exceeds the GPU contract.");
        return false;
    }
    if (visibility.visible_count == nullptr || visibility.visible_indices == nullptr ||
        visibility.capacity < instance_batches.size()) {
        set_diagnostics(diagnostics, "Runtime GPU draw batches require compatible GPU visibility output.");
        return false;
    }
    if (std::any_of(batches.begin(), batches.end(), [](const auto& batch) { return !valid(batch); })) {
        set_diagnostics(diagnostics, "Runtime GPU draw batches rejected invalid draw metadata.");
        return false;
    }

    const std::uint32_t instance_count = static_cast<std::uint32_t>(instance_batches.size());
    const std::uint32_t batch_count = static_cast<std::uint32_t>(batches.size());
    std::vector<std::uint32_t> batch_capacities(batch_count, 0U);
    for (const std::uint32_t batch_index : instance_batches) {
        if (batch_index == runtime_gpu_draw_batch_unassigned) continue;
        if (batch_index >= batch_count) {
            set_diagnostics(diagnostics, "Runtime GPU draw batch assignment references an invalid batch.");
            return false;
        }
        if (batch_capacities[batch_index] == std::numeric_limits<std::uint32_t>::max()) {
            set_diagnostics(diagnostics, "Runtime GPU draw batch capacity overflowed.");
            return false;
        }
        ++batch_capacities[batch_index];
    }

    if (!state_->ensure_capacity(instance_count, batch_count, diagnostics)) return false;

    std::vector<GpuBatchMetadata> metadata;
    metadata.reserve(batch_count);
    std::uint32_t output_offset = 0U;
    state_->ranges.clear();
    for (std::uint32_t batch_index = 0U; batch_index < batch_count; ++batch_index) {
        const auto& batch = batches[batch_index];
        const std::uint32_t capacity = batch_capacities[batch_index];
        metadata.push_back(GpuBatchMetadata{
            .index_count = batch.index_count,
            .first_index = batch.first_index,
            .vertex_offset = batch.vertex_offset,
            .output_offset = output_offset,
            .capacity = capacity,
        });
        state_->ranges.push_back({output_offset, capacity});
        output_offset += capacity;
    }

    GpuBatchConstants constants{
        .instance_count = instance_count,
        .batch_count = batch_count,
        .visible_capacity = visibility.capacity,
    };
    const auto assignment_bytes = std::as_bytes(instance_batches);
    const auto metadata_bytes = std::as_bytes(std::span{metadata});
    std::vector<std::uint32_t> zeros(batch_count, 0U);
    const auto zero_bytes = std::as_bytes(std::span{zeros});
    if (!state_->constants->write(0U, std::as_bytes(std::span{&constants, 1})) ||
        !state_->assignments_upload->write(0U, assignment_bytes) ||
        !state_->metadata_upload->write(0U, metadata_bytes) ||
        !state_->zero_upload->write(0U, zero_bytes) ||
        !commands.copy_buffer(
            *state_->assignments_upload, 0U, *state_->assignments_gpu, 0U, assignment_bytes.size()
        ) ||
        !commands.copy_buffer(
            *state_->metadata_upload, 0U, *state_->metadata_gpu, 0U, metadata_bytes.size()
        ) ||
        !commands.copy_buffer(
            *state_->zero_upload, 0U, *state_->batch_counts, 0U, zero_bytes.size()
        )) {
        set_diagnostics(diagnostics, "VICE Runtime could not upload GPU draw batch metadata.");
        return false;
    }

    const auto count_view = structured_view(sizeof(std::uint32_t), sizeof(std::uint32_t));
    const auto visible_view = structured_view(
        static_cast<std::uint64_t>(visibility.capacity) * sizeof(std::uint32_t),
        sizeof(std::uint32_t)
    );
    const auto assignment_view = structured_view(assignment_bytes.size(), sizeof(std::uint32_t));
    const auto metadata_view = structured_view(metadata_bytes.size(), sizeof(GpuBatchMetadata));
    const auto batch_count_view = structured_view(zero_bytes.size(), sizeof(std::uint32_t));
    const auto compacted_view = structured_view(
        static_cast<std::uint64_t>(instance_count) * sizeof(std::uint32_t),
        sizeof(std::uint32_t)
    );
    const auto indirect_view = structured_view(
        static_cast<std::uint64_t>(batch_count) * sizeof(render::IndexedIndirectDrawArguments),
        sizeof(std::uint32_t)
    );

    const std::uint32_t compact_groups =
        (visibility.capacity + gpu_batch_thread_group_size - 1U) / gpu_batch_thread_group_size;
    if (!commands.set_compute_pipeline(*state_->compact_pipeline) ||
        !commands.set_compute_constant_buffer(
            detail::gpu_batch_constants_binding, *state_->constants, 0U, sizeof(GpuBatchConstants)
        ) ||
        !commands.set_compute_storage_buffer(
            detail::gpu_batch_visible_count_binding, *visibility.visible_count, count_view
        ) ||
        !commands.set_compute_storage_buffer(
            detail::gpu_batch_visible_indices_binding, *visibility.visible_indices, visible_view
        ) ||
        !commands.set_compute_storage_buffer(
            detail::gpu_batch_assignments_binding, *state_->assignments_gpu, assignment_view
        ) ||
        !commands.set_compute_storage_buffer(
            detail::gpu_batch_metadata_binding, *state_->metadata_gpu, metadata_view
        ) ||
        !commands.set_compute_storage_buffer(
            detail::gpu_batch_counts_binding, *state_->batch_counts, batch_count_view
        ) ||
        !commands.set_compute_storage_buffer(
            detail::gpu_batch_compacted_binding, *state_->compacted_instances, compacted_view
        ) ||
        !commands.dispatch(render::ComputeDispatchDesc{compact_groups, 1U, 1U})) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode GPU draw batch compaction.");
        return false;
    }

    const std::uint32_t finalize_groups =
        (batch_count + gpu_batch_thread_group_size - 1U) / gpu_batch_thread_group_size;
    if (!commands.set_compute_pipeline(*state_->finalize_pipeline) ||
        !commands.set_compute_constant_buffer(
            detail::gpu_batch_constants_binding, *state_->constants, 0U, sizeof(GpuBatchConstants)
        ) ||
        !commands.set_compute_storage_buffer(
            detail::gpu_batch_finalize_metadata_binding, *state_->metadata_gpu, metadata_view
        ) ||
        !commands.set_compute_storage_buffer(
            detail::gpu_batch_finalize_counts_binding, *state_->batch_counts, batch_count_view
        ) ||
        !commands.set_compute_storage_buffer(
            detail::gpu_batch_indirect_binding, *state_->indirect_arguments, indirect_view
        ) ||
        !commands.dispatch(render::ComputeDispatchDesc{finalize_groups, 1U, 1U})) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode GPU indirect batch arguments.");
        return false;
    }

    state_->batch_count = batch_count;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeGpuDrawBatchResult RuntimeGpuDrawBatcher::result() const noexcept {
    if (state_ == nullptr) return {};
    return RuntimeGpuDrawBatchResult{
        .compacted_instance_indices = state_->compacted_instances.get(),
        .indirect_arguments = state_->indirect_arguments.get(),
        .ranges = state_->ranges,
        .batch_count = state_->batch_count,
        .instance_capacity = state_->instance_capacity,
    };
}

} // namespace vice::runtime
