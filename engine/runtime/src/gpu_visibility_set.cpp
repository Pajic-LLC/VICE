#include <vice/runtime/gpu_visibility_set.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>

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

struct RuntimeGpuIdentityVisibility::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::Buffer> count_upload;
    std::unique_ptr<render::Buffer> count_gpu;
    std::unique_ptr<render::Buffer> indices_upload;
    std::unique_ptr<render::Buffer> indices_gpu;
    std::uint32_t capacity = 0U;
    std::uint32_t uploaded_capacity = 0U;
    std::uint32_t instance_count = 0U;

    [[nodiscard]] bool ensure_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (device == nullptr || required == 0U) return false;
        if (required <= capacity && indices_upload && indices_gpu) return true;

        const std::uint32_t new_capacity = grown_capacity(required);
        const std::uint64_t size =
            static_cast<std::uint64_t>(new_capacity) * sizeof(std::uint32_t);

        render::BufferDesc upload_desc;
        upload_desc.size = size;
        upload_desc.usage = render::BufferUsage::copy_source;
        upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
        upload_desc.debug_name = "VICE Runtime identity visibility index upload";
        auto new_upload = device->create_buffer(upload_desc);

        render::BufferDesc gpu_desc;
        gpu_desc.size = size;
        gpu_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination;
        gpu_desc.memory = render::MemoryLocation::gpu_only;
        gpu_desc.debug_name = "VICE Runtime identity visibility indices";
        auto new_gpu = device->create_buffer(gpu_desc);

        if (!new_upload || !new_gpu) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate identity visibility indices.");
            return false;
        }

        std::vector<std::uint32_t> indices(new_capacity);
        for (std::uint32_t index = 0U; index < new_capacity; ++index) indices[index] = index;
        if (!new_upload->write(0U, std::as_bytes(std::span{indices}))) {
            set_diagnostics(diagnostics, "VICE Runtime could not stage identity visibility indices.");
            return false;
        }

        indices_upload = std::move(new_upload);
        indices_gpu = std::move(new_gpu);
        capacity = new_capacity;
        uploaded_capacity = 0U;
        return true;
    }
};

RuntimeGpuIdentityVisibility::RuntimeGpuIdentityVisibility() : state_{std::make_unique<State>()} {}
RuntimeGpuIdentityVisibility::~RuntimeGpuIdentityVisibility() = default;
RuntimeGpuIdentityVisibility::RuntimeGpuIdentityVisibility(RuntimeGpuIdentityVisibility&&) noexcept = default;
RuntimeGpuIdentityVisibility& RuntimeGpuIdentityVisibility::operator=(RuntimeGpuIdentityVisibility&&) noexcept = default;

bool RuntimeGpuIdentityVisibility::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime identity visibility state is unavailable.");
        return false;
    }

    render::BufferDesc upload_desc;
    upload_desc.size = sizeof(std::uint32_t);
    upload_desc.usage = render::BufferUsage::copy_source;
    upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
    upload_desc.debug_name = "VICE Runtime identity visibility count upload";
    auto count_upload = device.create_buffer(upload_desc);

    render::BufferDesc count_desc;
    count_desc.size = sizeof(std::uint32_t);
    count_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination;
    count_desc.memory = render::MemoryLocation::gpu_only;
    count_desc.debug_name = "VICE Runtime identity visibility count";
    auto count_gpu = device.create_buffer(count_desc);

    if (!count_upload || !count_gpu) {
        set_diagnostics(diagnostics, "VICE Runtime could not allocate identity visibility count buffers.");
        return false;
    }

    state_->device = &device;
    state_->count_upload = std::move(count_upload);
    state_->count_gpu = std::move(count_gpu);
    state_->indices_upload.reset();
    state_->indices_gpu.reset();
    state_->capacity = 0U;
    state_->uploaded_capacity = 0U;
    state_->instance_count = 0U;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuIdentityVisibility::prepare(
    render::CommandList& commands,
    const std::uint32_t instance_count,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr || !state_->count_upload || !state_->count_gpu) {
        set_diagnostics(diagnostics, "Runtime identity visibility is not initialized.");
        return false;
    }
    if (instance_count == 0U) {
        state_->instance_count = 0U;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    if (!state_->ensure_capacity(instance_count, diagnostics)) return false;
    const bool upload_indices = state_->uploaded_capacity < state_->capacity;
    if (!state_->count_upload->write(
            0U, std::as_bytes(std::span{&instance_count, 1})
        ) ||
        !commands.copy_buffer(
            *state_->count_upload,
            0U,
            *state_->count_gpu,
            0U,
            sizeof(std::uint32_t)
        ) ||
        (upload_indices && !commands.copy_buffer(
            *state_->indices_upload,
            0U,
            *state_->indices_gpu,
            0U,
            static_cast<std::uint64_t>(state_->capacity) * sizeof(std::uint32_t)
        ))) {
        set_diagnostics(diagnostics, "VICE Runtime could not upload identity visibility state.");
        return false;
    }

    if (upload_indices) state_->uploaded_capacity = state_->capacity;
    state_->instance_count = instance_count;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeVisibilityGpuResult RuntimeGpuIdentityVisibility::result() const noexcept {
    if (state_ == nullptr || state_->instance_count == 0U) return {};
    return RuntimeVisibilityGpuResult{
        .visible_count = state_->count_gpu.get(),
        .visible_indices = state_->indices_gpu.get(),
        .capacity = state_->capacity,
    };
}

} // namespace vice::runtime
