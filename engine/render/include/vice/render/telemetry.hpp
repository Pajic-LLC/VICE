#pragma once

#include <vice/render/rhi.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace vice::render {

struct DeviceStats {
    std::uint64_t submitted_command_lists = 0;
    SubmissionId completed_submission = invalid_submission;
    std::uint64_t created_buffers = 0;
    std::uint64_t created_textures = 0;
    std::uint64_t created_samplers = 0;
    std::uint64_t created_shader_modules = 0;
    std::uint64_t created_graphics_pipelines = 0;
};

class InstrumentedDevice final : public Device {
public:
    explicit InstrumentedDevice(std::unique_ptr<Device> device) noexcept;
    ~InstrumentedDevice() override;

    InstrumentedDevice(const InstrumentedDevice&) = delete;
    InstrumentedDevice& operator=(const InstrumentedDevice&) = delete;
    InstrumentedDevice(InstrumentedDevice&&) = delete;
    InstrumentedDevice& operator=(InstrumentedDevice&&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] DeviceStats stats() const noexcept;

    [[nodiscard]] Backend backend() const noexcept override;
    [[nodiscard]] DeviceCapabilities capabilities() const noexcept override;

    [[nodiscard]] std::unique_ptr<Buffer> create_buffer(const BufferDesc& desc) override;
    [[nodiscard]] std::unique_ptr<Texture> create_texture(const TextureDesc& desc) override;
    [[nodiscard]] std::unique_ptr<Sampler> create_sampler(const SamplerDesc& desc) override;
    [[nodiscard]] std::unique_ptr<ShaderModule> create_shader_module(const ShaderModuleDesc& desc) override;
    [[nodiscard]] std::unique_ptr<ShaderModule> create_shader_module(const ShaderBinaryDesc& desc) override;
    [[nodiscard]] std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDesc& desc
    ) override;
    [[nodiscard]] std::unique_ptr<CommandList> create_command_list() override;

    SubmissionId submit(std::unique_ptr<CommandList> command_list) override;
    [[nodiscard]] SubmissionId completed_submission() const noexcept override;
    bool wait_for_submission(SubmissionId submission) override;
    bool wait_idle() override;

    [[nodiscard]] std::unique_ptr<Swapchain> create_swapchain(platform::Window& window) override;

private:
    std::unique_ptr<Device> device_;
    std::atomic<std::uint64_t> submitted_command_lists_{0};
    std::atomic<std::uint64_t> created_buffers_{0};
    std::atomic<std::uint64_t> created_textures_{0};
    std::atomic<std::uint64_t> created_samplers_{0};
    std::atomic<std::uint64_t> created_shader_modules_{0};
    std::atomic<std::uint64_t> created_graphics_pipelines_{0};
};

[[nodiscard]] std::unique_ptr<InstrumentedDevice> instrument_device(
    std::unique_ptr<Device> device
) noexcept;

} // namespace vice::render
