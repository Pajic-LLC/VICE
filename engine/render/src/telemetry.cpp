#include <vice/render/telemetry.hpp>

#include <utility>

namespace vice::render {

InstrumentedDevice::InstrumentedDevice(std::unique_ptr<Device> device) noexcept
    : device_{std::move(device)} {}

InstrumentedDevice::~InstrumentedDevice() = default;

bool InstrumentedDevice::valid() const noexcept {
    return device_ != nullptr;
}

DeviceStats InstrumentedDevice::stats() const noexcept {
    DeviceStats result;
    result.submitted_command_lists = submitted_command_lists_.load(std::memory_order_relaxed);
    result.completed_submission = completed_submission();
    result.created_buffers = created_buffers_.load(std::memory_order_relaxed);
    result.created_textures = created_textures_.load(std::memory_order_relaxed);
    result.created_samplers = created_samplers_.load(std::memory_order_relaxed);
    result.created_shader_modules = created_shader_modules_.load(std::memory_order_relaxed);
    result.created_graphics_pipelines = created_graphics_pipelines_.load(std::memory_order_relaxed);
    return result;
}

Backend InstrumentedDevice::backend() const noexcept {
    return device_ != nullptr ? device_->backend() : Backend::unknown;
}

DeviceCapabilities InstrumentedDevice::capabilities() const noexcept {
    return device_ != nullptr ? device_->capabilities() : DeviceCapabilities{};
}

std::unique_ptr<Buffer> InstrumentedDevice::create_buffer(const BufferDesc& desc) {
    if (device_ == nullptr) return nullptr;
    auto result = device_->create_buffer(desc);
    if (result) created_buffers_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

std::unique_ptr<Texture> InstrumentedDevice::create_texture(const TextureDesc& desc) {
    if (device_ == nullptr) return nullptr;
    auto result = device_->create_texture(desc);
    if (result) created_textures_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

std::unique_ptr<Sampler> InstrumentedDevice::create_sampler(const SamplerDesc& desc) {
    if (device_ == nullptr) return nullptr;
    auto result = device_->create_sampler(desc);
    if (result) created_samplers_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

std::unique_ptr<ShaderModule> InstrumentedDevice::create_shader_module(const ShaderModuleDesc& desc) {
    if (device_ == nullptr) return nullptr;
    auto result = device_->create_shader_module(desc);
    if (result) created_shader_modules_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

std::unique_ptr<ShaderModule> InstrumentedDevice::create_shader_module(const ShaderBinaryDesc& desc) {
    if (device_ == nullptr) return nullptr;
    auto result = device_->create_shader_module(desc);
    if (result) created_shader_modules_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

std::unique_ptr<GraphicsPipeline> InstrumentedDevice::create_graphics_pipeline(
    const GraphicsPipelineDesc& desc
) {
    if (device_ == nullptr) return nullptr;
    auto result = device_->create_graphics_pipeline(desc);
    if (result) created_graphics_pipelines_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

std::unique_ptr<CommandList> InstrumentedDevice::create_command_list() {
    return device_ != nullptr ? device_->create_command_list() : nullptr;
}

SubmissionId InstrumentedDevice::submit(std::unique_ptr<CommandList> command_list) {
    if (device_ == nullptr) return invalid_submission;
    const SubmissionId submission = device_->submit(std::move(command_list));
    if (submission != invalid_submission) {
        submitted_command_lists_.fetch_add(1, std::memory_order_relaxed);
    }
    return submission;
}

SubmissionId InstrumentedDevice::completed_submission() const noexcept {
    return device_ != nullptr ? device_->completed_submission() : invalid_submission;
}

bool InstrumentedDevice::wait_for_submission(const SubmissionId submission) {
    return device_ != nullptr && device_->wait_for_submission(submission);
}

bool InstrumentedDevice::wait_idle() {
    return device_ != nullptr && device_->wait_idle();
}

std::unique_ptr<Swapchain> InstrumentedDevice::create_swapchain(platform::Window& window) {
    return device_ != nullptr ? device_->create_swapchain(window) : nullptr;
}

std::unique_ptr<InstrumentedDevice> instrument_device(std::unique_ptr<Device> device) noexcept {
    if (!device) return nullptr;
    return std::make_unique<InstrumentedDevice>(std::move(device));
}

} // namespace vice::render
