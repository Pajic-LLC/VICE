#include <vice/runtime/gpu_instance_table.hpp>

#include <algorithm>
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

struct RuntimeGpuInstanceTable::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::Buffer> upload;
    std::unique_ptr<render::Buffer> models;
    std::uint32_t instance_count = 0U;
    std::uint32_t capacity = 0U;

    [[nodiscard]] bool ensure_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (device == nullptr || required == 0U) return false;
        if (required <= capacity && upload && models) return true;

        const std::uint32_t next_capacity = grown_capacity(required);
        const std::uint64_t size = static_cast<std::uint64_t>(next_capacity) * sizeof(math::Mat4);

        render::BufferDesc upload_desc;
        upload_desc.size = size;
        upload_desc.usage = render::BufferUsage::copy_source;
        upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
        upload_desc.debug_name = "VICE Runtime GPU instance model upload";
        auto next_upload = device->create_buffer(upload_desc);

        render::BufferDesc model_desc;
        model_desc.size = size;
        model_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination |
            render::BufferUsage::copy_source;
        model_desc.memory = render::MemoryLocation::gpu_only;
        model_desc.debug_name = "VICE Runtime GPU instance model table";
        auto next_models = device->create_buffer(model_desc);

        if (!next_upload || !next_models) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate GPU instance model buffers.");
            return false;
        }

        upload = std::move(next_upload);
        models = std::move(next_models);
        capacity = next_capacity;
        return true;
    }
};

RuntimeGpuInstanceTable::RuntimeGpuInstanceTable() : state_{std::make_unique<State>()} {}
RuntimeGpuInstanceTable::~RuntimeGpuInstanceTable() = default;
RuntimeGpuInstanceTable::RuntimeGpuInstanceTable(RuntimeGpuInstanceTable&&) noexcept = default;
RuntimeGpuInstanceTable& RuntimeGpuInstanceTable::operator=(RuntimeGpuInstanceTable&&) noexcept = default;

bool RuntimeGpuInstanceTable::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU instance table state is unavailable.");
        return false;
    }
    state_->device = &device;
    state_->upload.reset();
    state_->models.reset();
    state_->instance_count = 0U;
    state_->capacity = 0U;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuInstanceTable::upload(
    render::CommandList& commands,
    const std::span<const math::Mat4> models,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU instance table is not initialized.");
        return false;
    }
    if (models.empty()) {
        state_->instance_count = 0U;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (models.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(diagnostics, "Runtime GPU instance table exceeds the GPU instance contract.");
        return false;
    }

    const std::uint32_t instance_count = static_cast<std::uint32_t>(models.size());
    if (!state_->ensure_capacity(instance_count, diagnostics)) return false;
    const auto bytes = std::as_bytes(models);
    if (!state_->upload->write(0U, bytes) ||
        !commands.copy_buffer(*state_->upload, 0U, *state_->models, 0U, bytes.size())) {
        set_diagnostics(diagnostics, "VICE Runtime could not upload GPU instance models.");
        return false;
    }

    state_->instance_count = instance_count;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeGpuInstanceTableResult RuntimeGpuInstanceTable::result() const noexcept {
    if (state_ == nullptr) return {};
    return RuntimeGpuInstanceTableResult{
        .models = state_->models.get(),
        .instance_count = state_->instance_count,
        .capacity = state_->capacity,
    };
}

} // namespace vice::runtime
