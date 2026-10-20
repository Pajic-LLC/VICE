#include <vice/runtime/gpu_instance_materials.hpp>

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

RuntimeGpuInstanceMaterialBuildResult build_runtime_gpu_instance_material_records(
    const std::span<const RenderMeshInstance> instances,
    const RuntimeGpuMaterialTablePlan& materials
) {
    RuntimeGpuInstanceMaterialBuildResult result;
    if (instances.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime GPU scene instance count exceeds the GPU index contract.";
        return result;
    }

    result.records.reserve(instances.size());
    for (const RenderMeshInstance& instance : instances) {
        RuntimeGpuInstanceMaterialRecord record;
        if (instance.material.has_value()) {
            const auto material_index = materials.material_index(*instance.material);
            if (!material_index.has_value()) {
                result.records.clear();
                result.diagnostics = "Runtime GPU scene instance references unknown material `" +
                    instance.material->value.to_string() + "`.";
                return result;
            }
            record.material_index = *material_index;
        }
        result.records.push_back(record);
    }
    return result;
}

struct RuntimeGpuInstanceMaterialTable::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::Buffer> upload;
    std::unique_ptr<render::Buffer> records;
    std::uint32_t instance_count = 0U;
    std::uint32_t capacity = 0U;

    [[nodiscard]] bool ensure_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (device == nullptr || required == 0U) return false;
        if (required <= capacity && upload && records) return true;

        const std::uint32_t next_capacity = grown_capacity(required);
        const std::uint64_t size = static_cast<std::uint64_t>(next_capacity) *
            sizeof(RuntimeGpuInstanceMaterialRecord);

        render::BufferDesc upload_desc;
        upload_desc.size = size;
        upload_desc.usage = render::BufferUsage::copy_source;
        upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
        upload_desc.debug_name = "VICE Runtime GPU instance material upload";
        auto next_upload = device->create_buffer(upload_desc);

        render::BufferDesc record_desc;
        record_desc.size = size;
        record_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination |
            render::BufferUsage::copy_source;
        record_desc.memory = render::MemoryLocation::gpu_only;
        record_desc.debug_name = "VICE Runtime GPU instance material table";
        auto next_records = device->create_buffer(record_desc);

        if (!next_upload || !next_records) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not allocate GPU scene instance material buffers."
            );
            return false;
        }

        upload = std::move(next_upload);
        records = std::move(next_records);
        capacity = next_capacity;
        return true;
    }
};

RuntimeGpuInstanceMaterialTable::RuntimeGpuInstanceMaterialTable()
    : state_{std::make_unique<State>()} {}
RuntimeGpuInstanceMaterialTable::~RuntimeGpuInstanceMaterialTable() = default;
RuntimeGpuInstanceMaterialTable::RuntimeGpuInstanceMaterialTable(
    RuntimeGpuInstanceMaterialTable&&
) noexcept = default;
RuntimeGpuInstanceMaterialTable& RuntimeGpuInstanceMaterialTable::operator=(
    RuntimeGpuInstanceMaterialTable&&
) noexcept = default;

bool RuntimeGpuInstanceMaterialTable::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU instance material table state is unavailable.");
        return false;
    }
    state_->device = &device;
    state_->upload.reset();
    state_->records.reset();
    state_->instance_count = 0U;
    state_->capacity = 0U;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuInstanceMaterialTable::upload(
    render::CommandList& commands,
    const std::span<const RuntimeGpuInstanceMaterialRecord> records,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU instance material table is not initialized.");
        return false;
    }
    if (records.empty()) {
        state_->instance_count = 0U;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (records.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(diagnostics, "Runtime GPU instance material table exceeds the GPU index contract.");
        return false;
    }

    const std::uint32_t instance_count = static_cast<std::uint32_t>(records.size());
    if (!state_->ensure_capacity(instance_count, diagnostics)) return false;
    const auto bytes = std::as_bytes(records);
    if (!state_->upload->write(0U, bytes) ||
        !commands.copy_buffer(*state_->upload, 0U, *state_->records, 0U, bytes.size())) {
        set_diagnostics(diagnostics, "VICE Runtime could not upload GPU instance material records.");
        return false;
    }

    state_->instance_count = instance_count;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeGpuInstanceMaterialTableResult RuntimeGpuInstanceMaterialTable::result() const noexcept {
    if (state_ == nullptr) return {};
    return RuntimeGpuInstanceMaterialTableResult{
        .records = state_->records.get(),
        .instance_count = state_->instance_count,
        .capacity = state_->capacity,
    };
}

} // namespace vice::runtime
