#include <vice/runtime/ray_tracing_scene_index_table.hpp>

#include <algorithm>
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

[[nodiscard]] std::uint32_t grown_capacity(const std::uint32_t required) noexcept {
    if (required <= 1U) return 1U;
    std::uint32_t capacity = 1U;
    while (capacity < required && capacity <= std::numeric_limits<std::uint32_t>::max() / 2U) {
        capacity *= 2U;
    }
    return std::max(capacity, required);
}

} // namespace

struct RuntimeRayTracingSceneIndexTable::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::Buffer> upload;
    std::unique_ptr<render::Buffer> indices;
    std::uint32_t index_count = 0U;
    std::uint32_t capacity = 0U;

    [[nodiscard]] bool ensure_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (device == nullptr || required == 0U) return false;
        if (required <= capacity && upload && indices) return true;

        const std::uint32_t next_capacity = grown_capacity(required);
        const std::uint64_t size = static_cast<std::uint64_t>(next_capacity) * sizeof(std::uint32_t);

        render::BufferDesc upload_desc;
        upload_desc.size = size;
        upload_desc.usage = render::BufferUsage::copy_source;
        upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
        upload_desc.debug_name = "VICE Runtime TLAS scene-index upload";
        auto next_upload = device->create_buffer(upload_desc);

        render::BufferDesc index_desc;
        index_desc.size = size;
        index_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination |
            render::BufferUsage::copy_source;
        index_desc.memory = render::MemoryLocation::gpu_only;
        index_desc.debug_name = "VICE Runtime TLAS scene-index table";
        auto next_indices = device->create_buffer(index_desc);

        if (!next_upload || !next_indices) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate TLAS scene-index buffers.");
            return false;
        }

        upload = std::move(next_upload);
        indices = std::move(next_indices);
        capacity = next_capacity;
        return true;
    }
};

RuntimeRayTracingSceneIndexTable::RuntimeRayTracingSceneIndexTable()
    : state_{std::make_unique<State>()} {}
RuntimeRayTracingSceneIndexTable::~RuntimeRayTracingSceneIndexTable() = default;
RuntimeRayTracingSceneIndexTable::RuntimeRayTracingSceneIndexTable(
    RuntimeRayTracingSceneIndexTable&&
) noexcept = default;
RuntimeRayTracingSceneIndexTable& RuntimeRayTracingSceneIndexTable::operator=(
    RuntimeRayTracingSceneIndexTable&&
) noexcept = default;

bool RuntimeRayTracingSceneIndexTable::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime TLAS scene-index table state is unavailable.");
        return false;
    }
    state_->device = &device;
    state_->upload.reset();
    state_->indices.reset();
    state_->index_count = 0U;
    state_->capacity = 0U;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayTracingSceneIndexTable::upload(
    render::CommandList& commands,
    const std::span<const std::uint32_t> scene_instance_indices,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime TLAS scene-index table is not initialized.");
        return false;
    }
    if (scene_instance_indices.empty()) {
        state_->index_count = 0U;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (scene_instance_indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(diagnostics, "Runtime TLAS scene-index table exceeds the GPU index contract.");
        return false;
    }

    const std::uint32_t index_count = static_cast<std::uint32_t>(scene_instance_indices.size());
    if (!state_->ensure_capacity(index_count, diagnostics)) return false;
    const auto bytes = std::as_bytes(scene_instance_indices);
    if (!state_->upload->write(0U, bytes) ||
        !commands.copy_buffer(*state_->upload, 0U, *state_->indices, 0U, bytes.size())) {
        set_diagnostics(diagnostics, "VICE Runtime could not upload TLAS scene-index mapping.");
        return false;
    }

    state_->index_count = index_count;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeRayTracingSceneIndexTableResult RuntimeRayTracingSceneIndexTable::result() const noexcept {
    if (state_ == nullptr) return {};
    return RuntimeRayTracingSceneIndexTableResult{
        .indices = state_->indices.get(),
        .index_count = state_->index_count,
        .capacity = state_->capacity,
    };
}

} // namespace vice::runtime
