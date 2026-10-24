#include <vice/render/ray_tracing.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace {

using namespace vice::render;

class TestBuffer final : public Buffer {
public:
    TestBuffer(const std::uint64_t size, const BufferUsage usage) : size_{size}, usage_{usage} {}

    [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }
    [[nodiscard]] BufferUsage usage() const noexcept override { return usage_; }
    [[nodiscard]] MemoryLocation memory_location() const noexcept override {
        return MemoryLocation::gpu_only;
    }
    bool write(std::uint64_t, std::span<const std::byte>) override { return false; }
    bool read(std::uint64_t, std::span<std::byte>) override { return false; }

private:
    std::uint64_t size_ = 0U;
    BufferUsage usage_ = BufferUsage::none;
};

class TestAccelerationStructure final : public AccelerationStructure {
public:
    TestAccelerationStructure(const AccelerationStructureType type, const std::uint64_t size)
        : type_{type}, size_{size} {}

    [[nodiscard]] AccelerationStructureType type() const noexcept override { return type_; }
    [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }

private:
    AccelerationStructureType type_ = AccelerationStructureType::bottom_level;
    std::uint64_t size_ = 0U;
};

class TestCommandList : public CommandList {
public:
    bool copy_buffer(Buffer&, std::uint64_t, Buffer&, std::uint64_t, std::uint64_t) override {
        return false;
    }
    bool begin_render_pass(Swapchain&, const RenderPassDesc&) override { return false; }
    bool begin_render_pass(Texture&, const RenderPassDesc&) override { return false; }
    bool begin_depth_render_pass(Texture&, float, bool) override { return false; }
    bool set_viewport(const Viewport&) override { return false; }
    bool set_scissor_rect(const ScissorRect&) override { return false; }
    bool set_graphics_pipeline(GraphicsPipeline&) override { return false; }
    bool set_graphics_constant_buffer(const ShaderBindingKey&, Buffer&, std::uint64_t, std::uint64_t) override {
        return false;
    }
    bool set_graphics_buffer(const ShaderBindingKey&, Buffer&, const BufferViewDesc&) override {
        return false;
    }
    bool set_graphics_storage_buffer(const ShaderBindingKey&, Buffer&, const BufferViewDesc&) override {
        return false;
    }
    bool set_graphics_texture(const ShaderBindingKey&, Texture&) override { return false; }
    bool set_graphics_storage_texture(const ShaderBindingKey&, Texture&) override { return false; }
    bool set_graphics_sampler(const ShaderBindingKey&, Sampler&) override { return false; }
    bool draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { return false; }
    bool end_render_pass() override { return false; }
    bool present(Swapchain&) override { return false; }
};

class TestDevice : public Device {
public:
    [[nodiscard]] Backend backend() const noexcept override { return Backend::direct3d12; }
    [[nodiscard]] DeviceCapabilities capabilities() const noexcept override { return {}; }
    [[nodiscard]] std::unique_ptr<Buffer> create_buffer(const BufferDesc&) override { return nullptr; }
    [[nodiscard]] std::unique_ptr<Texture> create_texture(const TextureDesc&) override { return nullptr; }
    [[nodiscard]] std::unique_ptr<Sampler> create_sampler(const SamplerDesc&) override { return nullptr; }
    [[nodiscard]] std::unique_ptr<ShaderModule> create_shader_module(const ShaderModuleDesc&) override {
        return nullptr;
    }
    [[nodiscard]] std::unique_ptr<ShaderModule> create_shader_module(const ShaderBinaryDesc&) override {
        return nullptr;
    }
    [[nodiscard]] std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(const GraphicsPipelineDesc&) override {
        return nullptr;
    }
    [[nodiscard]] std::unique_ptr<CommandList> create_command_list() override { return nullptr; }
    SubmissionId submit(std::unique_ptr<CommandList>) override { return invalid_submission; }
    [[nodiscard]] SubmissionId completed_submission() const noexcept override { return invalid_submission; }
    bool wait_for_submission(SubmissionId) override { return false; }
    bool wait_idle() override { return false; }
    [[nodiscard]] std::unique_ptr<Swapchain> create_swapchain(vice::platform::Window&) override { return nullptr; }
};

class TestRayDevice final : public TestDevice, public RayTracingDevice {
public:
    [[nodiscard]] DeviceCapabilities capabilities() const noexcept override {
        DeviceCapabilities result{};
        result.ray_tracing = true;
        return result;
    }

    bool query_acceleration_structure_build_sizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureBuildSizes& sizes
    ) override {
        if (!is_valid(desc)) return false;
        sizes = {
            .result_size = 4096U,
            .build_scratch_size = 2048U,
            .update_scratch_size = 1024U,
        };
        return true;
    }

    [[nodiscard]] std::unique_ptr<AccelerationStructure> create_acceleration_structure(
        const AccelerationStructureBuildDesc& desc,
        const AccelerationStructureBuildSizes& sizes
    ) override {
        if (!is_valid(desc) || !is_valid(sizes)) return nullptr;
        return std::make_unique<TestAccelerationStructure>(desc.type, sizes.result_size);
    }
};

class TestRayCommandList final : public TestCommandList, public RayTracingCommandList {
public:
    bool build_acceleration_structure(
        AccelerationStructure& destination,
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructure* source
    ) override {
        if (!is_valid(desc) || destination.type() != desc.type) return false;
        ++build_count;
        last_source = source;
        return true;
    }

    std::uint32_t build_count = 0U;
    AccelerationStructure* last_source = nullptr;
};

} // namespace

int main() {
    using namespace vice::render;

    TestBuffer vertices{4096U, BufferUsage::vertex};
    TestBuffer indices{2048U, BufferUsage::index};
    const AccelerationStructureTriangleGeometryDesc geometry{
        .vertex_buffer = &vertices,
        .vertex_offset = 0U,
        .vertex_count = 24U,
        .vertex_stride = 32U,
        .vertex_format = VertexFormat::float32x3,
        .index_buffer = &indices,
        .index_offset = 0U,
        .index_count = 36U,
        .index_format = IndexFormat::uint32,
        .opaque = true,
    };
    const std::array geometries{geometry};
    const AccelerationStructureBuildDesc desc{
        .type = AccelerationStructureType::bottom_level,
        .flags = AccelerationStructureBuildFlags::allow_update |
            AccelerationStructureBuildFlags::prefer_fast_trace,
        .geometries = geometries,
        .instances = {},
    };
    assert(is_valid(desc));

    TestDevice plain_device;
    AccelerationStructureBuildSizes sizes{
        .result_size = 1U,
        .build_scratch_size = 1U,
        .update_scratch_size = 1U,
    };
    assert(!query_acceleration_structure_build_sizes(plain_device, desc, sizes));
    assert(sizes.result_size == 0U);
    assert(sizes.build_scratch_size == 0U);
    assert(sizes.update_scratch_size == 0U);
    assert(create_acceleration_structure(plain_device, desc, sizes) == nullptr);

    TestRayDevice ray_device;
    assert(supports_acceleration_structures(ray_device.capabilities()));
    assert(query_acceleration_structure_build_sizes(ray_device, desc, sizes));
    assert(sizes.result_size == 4096U);
    assert(sizes.build_scratch_size == 2048U);
    assert(sizes.update_scratch_size == 1024U);

    auto first = create_acceleration_structure(ray_device, desc, sizes);
    auto updated = create_acceleration_structure(ray_device, desc, sizes);
    assert(first != nullptr);
    assert(updated != nullptr);
    assert(first->type() == AccelerationStructureType::bottom_level);
    assert(first->size() == 4096U);

    TestCommandList plain_commands;
    assert(!build_acceleration_structure(plain_commands, *first, desc));

    TestRayCommandList ray_commands;
    assert(build_acceleration_structure(ray_commands, *first, desc));
    assert(ray_commands.build_count == 1U);
    assert(ray_commands.last_source == nullptr);
    assert(build_acceleration_structure(ray_commands, *updated, desc, first.get()));
    assert(ray_commands.build_count == 2U);
    assert(ray_commands.last_source == first.get());

    return 0;
}
