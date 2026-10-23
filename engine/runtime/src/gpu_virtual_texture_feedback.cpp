#include <vice/runtime/gpu_virtual_texture_feedback.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] bool resource_less(
    const world::ResourceId left,
    const world::ResourceId right
) noexcept {
    const auto left_bytes = left.value.bytes();
    const auto right_bytes = right.value.bytes();
    return std::lexicographical_compare(
        left_bytes.begin(), left_bytes.end(), right_bytes.begin(), right_bytes.end()
    );
}

[[nodiscard]] bool key_less(
    const RuntimeVirtualTextureTileKey& left,
    const RuntimeVirtualTextureTileKey& right
) noexcept {
    if (left.texture != right.texture) return resource_less(left.texture, right.texture);
    return std::tie(left.mip_level, left.tile_y, left.tile_x) <
        std::tie(right.mip_level, right.tile_y, right.tile_x);
}

[[nodiscard]] std::unique_ptr<render::Buffer> make_buffer(
    render::Device& device,
    const std::uint64_t size,
    const render::BufferUsage usage,
    const render::MemoryLocation memory,
    const char* const debug_name
) {
    render::BufferDesc desc;
    desc.size = size;
    desc.usage = usage;
    desc.memory = memory;
    desc.debug_name = debug_name;
    return device.create_buffer(desc);
}

} // namespace

RuntimeGpuVirtualTextureFeedbackResult build_runtime_gpu_virtual_texture_feedback_requests(
    const RuntimeVirtualTextureSamplingPlan& sampling,
    const std::span<const std::uint32_t> page_flags
) {
    RuntimeGpuVirtualTextureFeedbackResult result;
    if (!sampling || sampling.pages.size() != sampling.page_keys.size() ||
        page_flags.size() != sampling.pages.size()) {
        result.diagnostics = "GPU virtual texture feedback is not aligned with the captured page table.";
        return result;
    }

    result.requests.reserve(page_flags.size());
    for (std::size_t page_index = 0U; page_index < page_flags.size(); ++page_index) {
        if (page_flags[page_index] == 0U) continue;
        const auto& desired = sampling.page_keys[page_index];
        const auto& resolved = sampling.pages[page_index];
        if (!desired.texture.valid() || resolved.physical_slot == runtime_virtual_texture_invalid_index) {
            result.diagnostics = "GPU virtual texture feedback references invalid page metadata.";
            result.requests.clear();
            return result;
        }
        if (resolved.resolved_mip <= desired.mip_level) continue;

        const std::uint32_t refinement_mip = resolved.resolved_mip - 1U;
        const std::uint32_t shift = refinement_mip - desired.mip_level;
        if (shift >= 32U) {
            result.diagnostics = "GPU virtual texture feedback refinement exceeds the tile coordinate contract.";
            result.requests.clear();
            return result;
        }
        result.requests.push_back(RuntimeVirtualTextureTileKey{
            .texture = desired.texture,
            .mip_level = refinement_mip,
            .tile_x = desired.tile_x >> shift,
            .tile_y = desired.tile_y >> shift,
        });
    }

    std::sort(result.requests.begin(), result.requests.end(), key_less);
    result.requests.erase(
        std::unique(result.requests.begin(), result.requests.end()),
        result.requests.end()
    );
    return result;
}

struct RuntimeGpuVirtualTextureFeedback::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::Buffer> dummy;
    std::unique_ptr<render::Buffer> flags;
    std::unique_ptr<render::Buffer> zero_upload;
    std::unique_ptr<render::Buffer> readback;
    RuntimeVirtualTextureSamplingPlan captured_sampling;
    render::SubmissionId submission = render::invalid_submission;
    std::uint32_t capacity = 0U;
    std::uint32_t page_count = 0U;
    bool prepared = false;
    bool captured = false;

    [[nodiscard]] bool ensure_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (device == nullptr || required == 0U) {
            set_diagnostics(diagnostics, "GPU virtual texture feedback requires a non-zero page capacity.");
            return false;
        }
        if (required <= capacity && flags && zero_upload && readback) return true;
        const std::uint64_t bytes = static_cast<std::uint64_t>(required) * sizeof(std::uint32_t);
        auto next_flags = make_buffer(
            *device,
            bytes,
            render::BufferUsage::storage | render::BufferUsage::copy_source |
                render::BufferUsage::copy_destination,
            render::MemoryLocation::gpu_only,
            "VICE Runtime virtual texture feedback flags"
        );
        auto next_zero = make_buffer(
            *device,
            bytes,
            render::BufferUsage::copy_source,
            render::MemoryLocation::cpu_to_gpu,
            "VICE Runtime virtual texture feedback zero upload"
        );
        auto next_readback = make_buffer(
            *device,
            bytes,
            render::BufferUsage::copy_destination,
            render::MemoryLocation::gpu_to_cpu,
            "VICE Runtime virtual texture feedback readback"
        );
        std::vector<std::uint32_t> zero(required, 0U);
        if (!next_flags || !next_zero || !next_readback ||
            !next_zero->write(0U, std::as_bytes(std::span{zero}))) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate virtual texture feedback buffers.");
            return false;
        }
        flags = std::move(next_flags);
        zero_upload = std::move(next_zero);
        readback = std::move(next_readback);
        capacity = required;
        return true;
    }
};

RuntimeGpuVirtualTextureFeedback::RuntimeGpuVirtualTextureFeedback()
    : state_{std::make_unique<State>()} {}
RuntimeGpuVirtualTextureFeedback::~RuntimeGpuVirtualTextureFeedback() = default;
RuntimeGpuVirtualTextureFeedback::RuntimeGpuVirtualTextureFeedback(
    RuntimeGpuVirtualTextureFeedback&&
) noexcept = default;
RuntimeGpuVirtualTextureFeedback& RuntimeGpuVirtualTextureFeedback::operator=(
    RuntimeGpuVirtualTextureFeedback&&
) noexcept = default;

bool RuntimeGpuVirtualTextureFeedback::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU virtual texture feedback state is unavailable.");
        return false;
    }
    auto dummy = make_buffer(
        device,
        sizeof(std::uint32_t),
        render::BufferUsage::storage,
        render::MemoryLocation::gpu_only,
        "VICE Runtime virtual texture feedback dummy"
    );
    if (!dummy) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the virtual texture feedback dummy buffer.");
        return false;
    }
    state_->device = &device;
    state_->dummy = std::move(dummy);
    state_->flags.reset();
    state_->zero_upload.reset();
    state_->readback.reset();
    state_->captured_sampling = {};
    state_->submission = render::invalid_submission;
    state_->capacity = 0U;
    state_->page_count = 0U;
    state_->prepared = false;
    state_->captured = false;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeGpuVirtualTextureFeedbackResult RuntimeGpuVirtualTextureFeedback::poll() {
    RuntimeGpuVirtualTextureFeedbackResult result;
    if (state_ == nullptr || state_->device == nullptr) {
        result.diagnostics = "Runtime GPU virtual texture feedback is not initialized.";
        return result;
    }
    if (state_->submission == render::invalid_submission ||
        state_->device->completed_submission() < state_->submission) {
        return result;
    }
    if (!state_->readback || state_->page_count == 0U ||
        state_->captured_sampling.pages.size() != state_->page_count) {
        result.diagnostics = "Runtime GPU virtual texture feedback completed without a valid capture snapshot.";
        return result;
    }

    std::vector<std::uint32_t> flags(state_->page_count, 0U);
    if (!state_->readback->read(0U, std::as_writable_bytes(std::span{flags}))) {
        result.diagnostics = "VICE Runtime could not read virtual texture feedback flags.";
        return result;
    }
    result = build_runtime_gpu_virtual_texture_feedback_requests(
        state_->captured_sampling,
        flags
    );
    if (result) {
        state_->captured_sampling = {};
        state_->submission = render::invalid_submission;
        state_->page_count = 0U;
    }
    return result;
}

bool RuntimeGpuVirtualTextureFeedback::prepare(
    render::CommandList& commands,
    const RuntimeVirtualTextureSamplingPlan& sampling,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr || !state_->dummy) {
        set_diagnostics(diagnostics, "Runtime GPU virtual texture feedback is not initialized.");
        return false;
    }
    state_->prepared = false;
    state_->captured = false;
    if (state_->submission != render::invalid_submission) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (sampling.pages.empty()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (!sampling || sampling.pages.size() != sampling.page_keys.size() ||
        sampling.pages.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(diagnostics, "Runtime GPU virtual texture feedback rejected an invalid page table snapshot.");
        return false;
    }
    const auto page_count = static_cast<std::uint32_t>(sampling.pages.size());
    if (!state_->ensure_capacity(page_count, diagnostics)) return false;
    const std::uint64_t bytes = static_cast<std::uint64_t>(page_count) * sizeof(std::uint32_t);
    if (!commands.copy_buffer(*state_->zero_upload, 0U, *state_->flags, 0U, bytes)) {
        set_diagnostics(diagnostics, "VICE Runtime could not clear virtual texture feedback flags.");
        return false;
    }
    state_->captured_sampling = sampling;
    state_->captured_sampling.physical_textures.clear();
    state_->page_count = page_count;
    state_->prepared = true;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

render::Buffer* RuntimeGpuVirtualTextureFeedback::binding_buffer() const noexcept {
    if (state_ == nullptr) return nullptr;
    return state_->prepared && state_->flags ? state_->flags.get() : state_->dummy.get();
}

bool RuntimeGpuVirtualTextureFeedback::active() const noexcept {
    return state_ != nullptr && state_->prepared && state_->flags != nullptr;
}

bool RuntimeGpuVirtualTextureFeedback::capture(
    render::CommandList& commands,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU virtual texture feedback is not initialized.");
        return false;
    }
    if (!state_->prepared) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (!state_->flags || !state_->readback || state_->page_count == 0U) {
        set_diagnostics(diagnostics, "Runtime GPU virtual texture feedback capture state is incomplete.");
        return false;
    }
    const std::uint64_t bytes = static_cast<std::uint64_t>(state_->page_count) * sizeof(std::uint32_t);
    if (!commands.copy_buffer(*state_->flags, 0U, *state_->readback, 0U, bytes)) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode virtual texture feedback readback.");
        return false;
    }
    state_->captured = true;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuVirtualTextureFeedback::commit(
    const render::SubmissionId submission,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU virtual texture feedback is not initialized.");
        return false;
    }
    if (!state_->prepared) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (!state_->captured || submission == render::invalid_submission) {
        set_diagnostics(diagnostics, "Runtime GPU virtual texture feedback cannot commit an uncaptured frame.");
        return false;
    }
    state_->submission = submission;
    state_->prepared = false;
    state_->captured = false;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

void RuntimeGpuVirtualTextureFeedback::cancel() noexcept {
    if (state_ == nullptr || state_->submission != render::invalid_submission) return;
    state_->prepared = false;
    state_->captured = false;
    state_->captured_sampling = {};
    state_->page_count = 0U;
}

} // namespace vice::runtime
