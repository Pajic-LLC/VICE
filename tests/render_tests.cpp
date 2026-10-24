#include <vice/render/compute.hpp>
#include <vice/render/ray_tracing.hpp>
#include <vice/render/rhi.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <limits>

namespace {

class TestShaderModule final : public vice::render::ShaderModule {
public:
    explicit TestShaderModule(const vice::render::ShaderStage stage) : stage_{stage} {}
    [[nodiscard]] vice::render::ShaderStage stage() const noexcept override { return stage_; }

private:
    vice::render::ShaderStage stage_;
};

class TestBuffer final : public vice::render::Buffer {
public:
    TestBuffer(const std::uint64_t size, const vice::render::BufferUsage usage)
        : size_{size}, usage_{usage} {}

    [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }
    [[nodiscard]] vice::render::BufferUsage usage() const noexcept override { return usage_; }
    [[nodiscard]] vice::render::MemoryLocation memory_location() const noexcept override {
        return vice::render::MemoryLocation::gpu_only;
    }
    bool write(std::uint64_t, std::span<const std::byte>) override { return false; }
    bool read(std::uint64_t, std::span<std::byte>) override { return false; }

private:
    std::uint64_t size_ = 0U;
    vice::render::BufferUsage usage_ = vice::render::BufferUsage::none;
};

class TestAccelerationStructure final : public vice::render::AccelerationStructure {
public:
    TestAccelerationStructure(
        const vice::render::AccelerationStructureType type,
        const std::uint64_t size
    ) : type_{type}, size_{size} {}

    [[nodiscard]] vice::render::AccelerationStructureType type() const noexcept override {
        return type_;
    }
    [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }

private:
    vice::render::AccelerationStructureType type_;
    std::uint64_t size_ = 0U;
};

} // namespace

int main() {
    using namespace vice::render;

    assert(backend_name(Backend::metal) == "Metal");
    assert(backend_name(Backend::direct3d12) == "Direct3D 12");
    assert(shader_stage_name(ShaderStage::vertex) == "vertex");
    assert(shader_stage_name(ShaderStage::fragment) == "fragment");
    assert(shader_stage_name(ShaderStage::compute) == "compute");
    assert(shader_register_class_name(ShaderRegisterClass::constant_buffer) == "constant_buffer");
    assert(shader_register_class_name(ShaderRegisterClass::shader_resource) == "shader_resource");
    assert(shader_register_class_name(ShaderRegisterClass::unordered_access) == "unordered_access");
    assert(shader_register_class_name(ShaderRegisterClass::sampler) == "sampler");

    const DeviceCapabilities capabilities{};
    assert(!capabilities.unified_memory);
    assert(!capabilities.ray_tracing);
    assert(!capabilities.mesh_shaders);
    assert(!supports_acceleration_structures(capabilities));
    assert(capabilities.max_sampler_anisotropy == 16U);
    assert(capabilities.max_texture_dimension_1d == 0U);
    assert(capabilities.max_texture_dimension_2d == 0U);
    assert(capabilities.max_texture_dimension_3d == 0U);
    assert(capabilities.max_texture_array_layers == 0U);
    assert(capabilities.recommended_frames_in_flight == 2U);

    DeviceCapabilities ray_capabilities{};
    ray_capabilities.ray_tracing = true;
    assert(supports_acceleration_structures(ray_capabilities));

    constexpr Color black{};
    static_assert(black.red == 0.0F);
    static_assert(black.green == 0.0F);
    static_assert(black.blue == 0.0F);
    static_assert(black.alpha == 1.0F);

    constexpr BufferDesc upload{
        .size = 4096,
        .usage = BufferUsage::vertex | BufferUsage::copy_source,
        .memory = MemoryLocation::cpu_to_gpu,
        .debug_name = {},
    };
    static_assert(is_valid(upload));
    static_assert(has_usage(upload.usage, BufferUsage::vertex));
    static_assert(has_usage(upload.usage, BufferUsage::copy_source));
    static_assert(!has_usage(upload.usage, BufferUsage::copy_destination));

    constexpr BufferDesc invalid_upload{
        .size = 4096,
        .usage = BufferUsage::storage | BufferUsage::copy_source,
        .memory = MemoryLocation::cpu_to_gpu,
        .debug_name = {},
    };
    static_assert(!is_valid(invalid_upload));

    constexpr BufferDesc readback{
        .size = 4096,
        .usage = BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_to_cpu,
        .debug_name = {},
    };
    static_assert(is_valid(readback));

    constexpr BufferDesc invalid_readback{
        .size = 4096,
        .usage = BufferUsage::copy_destination | BufferUsage::vertex,
        .memory = MemoryLocation::gpu_to_cpu,
        .debug_name = {},
    };
    static_assert(!is_valid(invalid_readback));

    TestBuffer ray_vertices{1024U, BufferUsage::vertex};
    TestBuffer ray_indices{512U, BufferUsage::index};
    AccelerationStructureTriangleGeometryDesc ray_geometry{
        .vertex_buffer = &ray_vertices,
        .vertex_offset = 0U,
        .vertex_count = 24U,
        .vertex_stride = 32U,
        .vertex_format = VertexFormat::float32x3,
        .index_buffer = &ray_indices,
        .index_offset = 0U,
        .index_count = 36U,
        .index_format = IndexFormat::uint32,
        .opaque = true,
    };
    assert(is_valid(ray_geometry));

    auto invalid_ray_geometry = ray_geometry;
    invalid_ray_geometry.vertex_format = VertexFormat::float32x4;
    assert(!is_valid(invalid_ray_geometry));

    invalid_ray_geometry = ray_geometry;
    invalid_ray_geometry.index_count = 35U;
    assert(!is_valid(invalid_ray_geometry));

    invalid_ray_geometry = ray_geometry;
    invalid_ray_geometry.vertex_offset = ray_vertices.size();
    assert(!is_valid(invalid_ray_geometry));

    constexpr auto conflicting_build_flags =
        AccelerationStructureBuildFlags::prefer_fast_trace |
        AccelerationStructureBuildFlags::prefer_fast_build;
    static_assert(!is_valid(conflicting_build_flags));
    static_assert(is_valid(AccelerationStructureBuildFlags::allow_update |
        AccelerationStructureBuildFlags::prefer_fast_trace));

    const std::array ray_geometries{ray_geometry};
    const AccelerationStructureBuildDesc bottom_level_build{
        .type = AccelerationStructureType::bottom_level,
        .flags = AccelerationStructureBuildFlags::allow_update |
            AccelerationStructureBuildFlags::prefer_fast_trace,
        .geometries = ray_geometries,
        .instances = {},
    };
    assert(is_valid(bottom_level_build));

    TestAccelerationStructure bottom_level{AccelerationStructureType::bottom_level, 4096U};
    AccelerationStructureInstanceDesc ray_instance{
        .bottom_level = &bottom_level,
        .instance_id = 17U,
        .mask = 0x7FU,
    };
    assert(is_valid(ray_instance));

    auto invalid_ray_instance = ray_instance;
    invalid_ray_instance.instance_id = acceleration_structure_instance_id_max + 1U;
    assert(!is_valid(invalid_ray_instance));

    invalid_ray_instance = ray_instance;
    invalid_ray_instance.mask = acceleration_structure_instance_mask_max + 1U;
    assert(!is_valid(invalid_ray_instance));

    invalid_ray_instance = ray_instance;
    invalid_ray_instance.transform[0] = std::numeric_limits<float>::infinity();
    assert(!is_valid(invalid_ray_instance));

    const std::array ray_instances{ray_instance};
    const AccelerationStructureBuildDesc top_level_build{
        .type = AccelerationStructureType::top_level,
        .flags = AccelerationStructureBuildFlags::prefer_fast_trace,
        .geometries = {},
        .instances = ray_instances,
    };
    assert(is_valid(top_level_build));

    auto invalid_top_level_build = top_level_build;
    invalid_top_level_build.geometries = ray_geometries;
    assert(!is_valid(invalid_top_level_build));

    constexpr AccelerationStructureBuildSizes ray_build_sizes{
        .result_size = 4096U,
        .build_scratch_size = 2048U,
        .update_scratch_size = 1024U,
    };
    static_assert(is_valid(ray_build_sizes));

    constexpr TextureDesc color_texture{
        .dimension = TextureDimension::d2,
        .width = 1920,
        .height = 1080,
        .depth = 1,
        .mip_levels = 1,
        .array_layers = 1,
        .sample_count = 1,
        .format = TextureFormat::rgba16_float,
        .usage = TextureUsage::sampled | TextureUsage::color_attachment,
        .debug_name = {},
    };
    static_assert(is_valid(color_texture));
    static_assert(texture_format_bytes_per_pixel(color_texture.format) == 8U);

    constexpr TextureDesc depth_texture{
        .dimension = TextureDimension::d2,
        .width = 1920,
        .height = 1080,
        .depth = 1,
        .mip_levels = 1,
        .array_layers = 1,
        .sample_count = 1,
        .format = TextureFormat::depth32_float,
        .usage = TextureUsage::sampled | TextureUsage::depth_attachment,
        .debug_name = {},
    };
    static_assert(is_valid(depth_texture));

    constexpr TextureDesc invalid_depth{
        .dimension = TextureDimension::d2,
        .width = 1920,
        .height = 1080,
        .depth = 1,
        .mip_levels = 1,
        .array_layers = 1,
        .sample_count = 1,
        .format = TextureFormat::depth32_float,
        .usage = TextureUsage::color_attachment,
        .debug_name = {},
    };
    static_assert(!is_valid(invalid_depth));

    constexpr TextureDesc invalid_multisample_storage{
        .dimension = TextureDimension::d2,
        .width = 512,
        .height = 512,
        .depth = 1,
        .mip_levels = 1,
        .array_layers = 1,
        .sample_count = 4,
        .format = TextureFormat::rgba8_unorm,
        .usage = TextureUsage::storage,
        .debug_name = {},
    };
    static_assert(!is_valid(invalid_multisample_storage));

    constexpr SamplerDesc sampler{};
    static_assert(is_valid(sampler));

    constexpr SamplerDesc invalid_sampler{
        .max_anisotropy = 17,
        .debug_name = {},
    };
    static_assert(!is_valid(invalid_sampler));

    constexpr SamplerDesc invalid_anisotropic_filter{
        .min_filter = Filter::nearest,
        .mag_filter = Filter::linear,
        .mip_filter = Filter::linear,
        .address_u = AddressMode::clamp_to_edge,
        .address_v = AddressMode::clamp_to_edge,
        .address_w = AddressMode::clamp_to_edge,
        .max_anisotropy = 4,
        .debug_name = {},
    };
    static_assert(!is_valid(invalid_anisotropic_filter));

    constexpr ShaderResourceBindingDesc constant_binding{
        .register_class = ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 1,
        .count = 1,
        .unbounded = false,
    };
    static_assert(is_valid(constant_binding));

    constexpr ShaderResourceBindingDesc unbounded_binding{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 4,
        .space = 2,
        .count = 0,
        .unbounded = true,
    };
    static_assert(is_valid(unbounded_binding));

    constexpr ShaderResourceBindingDesc invalid_binding{
        .register_class = ShaderRegisterClass::unordered_access,
        .binding = 2,
        .space = 0,
        .count = 0,
        .unbounded = false,
    };
    static_assert(!is_valid(invalid_binding));

    constexpr MetalShaderResourceLocationDesc metal_location{
        .register_class = ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 1,
        .argument_buffer_offset = 24,
        .size = 24,
    };
    static_assert(is_valid(metal_location));

    constexpr MetalShaderResourceLocationDesc metal_array_location{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 4,
        .space = 2,
        .argument_buffer_offset = 48,
        .size = 72,
    };
    static_assert(is_valid(metal_array_location));

    constexpr MetalShaderResourceLocationDesc invalid_metal_size{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 4,
        .space = 2,
        .argument_buffer_offset = 48,
        .size = 16,
    };
    static_assert(!is_valid(invalid_metal_size));

    constexpr MetalShaderResourceLocationDesc invalid_metal_alignment{
        .register_class = ShaderRegisterClass::sampler,
        .binding = 1,
        .space = 0,
        .argument_buffer_offset = 4,
        .size = 24,
    };
    static_assert(!is_valid(invalid_metal_alignment));

    constexpr ShaderBindingKey texture_element{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 4,
        .space = 2,
        .array_element = 5,
    };
    constexpr ShaderBindingKey same_texture_namespace{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 4,
        .space = 2,
        .array_element = 1,
    };
    constexpr ShaderBindingKey different_texture_space{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 4,
        .space = 3,
        .array_element = 5,
    };
    static_assert(same_binding_namespace(texture_element, same_texture_namespace));
    static_assert(!same_binding_namespace(texture_element, different_texture_space));

    constexpr BufferViewDesc raw_view{
        .offset = 16,
        .size = 64,
        .structure_stride = 0,
    };
    static_assert(is_valid(raw_view));

    constexpr BufferViewDesc structured_view{
        .offset = 32,
        .size = 128,
        .structure_stride = 16,
    };
    static_assert(is_valid(structured_view));

    constexpr BufferViewDesc invalid_structured_view{
        .offset = 0,
        .size = 130,
        .structure_stride = 16,
    };
    static_assert(!is_valid(invalid_structured_view));

    constexpr ShaderModuleDesc valid_shader{
        .stage = ShaderStage::vertex,
        .language = ShaderSourceLanguage::hlsl,
        .source = "void main() {}",
        .entry_point = "main",
        .debug_name = {},
    };
    static_assert(is_valid(valid_shader));

    constexpr ShaderModuleDesc invalid_shader{
        .stage = ShaderStage::fragment,
        .language = ShaderSourceLanguage::metal,
        .source = {},
        .entry_point = "main",
        .debug_name = {},
    };
    static_assert(!is_valid(invalid_shader));

    static constexpr std::array<std::byte, 4> binary_bytes{};
    static constexpr std::array<ShaderResourceBindingDesc, 2> binary_bindings{
        constant_binding,
        unbounded_binding,
    };
    constexpr ShaderBinaryDesc valid_binary{
        .stage = ShaderStage::vertex,
        .format = ShaderBinaryFormat::dxil,
        .binary = std::span<const std::byte>{binary_bytes},
        .resources = std::span<const ShaderResourceBindingDesc>{binary_bindings},
        .entry_point = "main",
        .debug_name = {},
    };
    static_assert(is_valid(valid_binary));

    static constexpr std::array<ShaderResourceBindingDesc, 1> metal_binary_bindings{
        constant_binding,
    };
    static constexpr std::array<MetalShaderResourceLocationDesc, 1> metal_binary_locations{
        metal_location,
    };
    constexpr ShaderBinaryDesc valid_metal_binary{
        .stage = ShaderStage::vertex,
        .format = ShaderBinaryFormat::metallib,
        .binary = std::span<const std::byte>{binary_bytes},
        .resources = std::span<const ShaderResourceBindingDesc>{metal_binary_bindings},
        .entry_point = "main",
        .debug_name = {},
        .metal_resource_locations = std::span<const MetalShaderResourceLocationDesc>{
            metal_binary_locations
        },
    };
    static_assert(is_valid(valid_metal_binary));

    static constexpr std::array<MetalShaderResourceLocationDesc, 1> invalid_metal_locations{
        invalid_metal_size,
    };
    constexpr ShaderBinaryDesc invalid_metal_binary{
        .stage = ShaderStage::vertex,
        .format = ShaderBinaryFormat::metallib,
        .binary = std::span<const std::byte>{binary_bytes},
        .resources = std::span<const ShaderResourceBindingDesc>{metal_binary_bindings},
        .entry_point = "main",
        .debug_name = {},
        .metal_resource_locations = std::span<const MetalShaderResourceLocationDesc>{
            invalid_metal_locations
        },
    };
    static_assert(!is_valid(invalid_metal_binary));

    static constexpr std::array<ShaderResourceBindingDesc, 1> invalid_binary_bindings{
        invalid_binding,
    };
    constexpr ShaderBinaryDesc invalid_binding_binary{
        .stage = ShaderStage::fragment,
        .format = ShaderBinaryFormat::dxil,
        .binary = std::span<const std::byte>{binary_bytes},
        .resources = std::span<const ShaderResourceBindingDesc>{invalid_binary_bindings},
        .entry_point = "main",
        .debug_name = {},
    };
    static_assert(!is_valid(invalid_binding_binary));

    constexpr ShaderBinaryDesc invalid_binary{
        .stage = ShaderStage::fragment,
        .format = ShaderBinaryFormat::metallib,
        .binary = {},
        .resources = {},
        .entry_point = "main",
        .debug_name = {},
    };
    static_assert(!is_valid(invalid_binary));

    constexpr GraphicsPipelineDesc invalid_pipeline{};
    static_assert(!is_valid(invalid_pipeline));

    TestShaderModule vertex_shader{ShaderStage::vertex};
    TestShaderModule fragment_shader{ShaderStage::fragment};
    TestShaderModule compute_shader{ShaderStage::compute};

    ComputePipelineDesc compute_pipeline{
        .compute_shader = &compute_shader,
        .debug_name = "test compute pipeline",
    };
    assert(is_valid(compute_pipeline));

    auto invalid_compute_pipeline = compute_pipeline;
    invalid_compute_pipeline.compute_shader = &fragment_shader;
    assert(!is_valid(invalid_compute_pipeline));

    constexpr ComputeDispatchDesc compute_dispatch{
        .group_count_x = 8U,
        .group_count_y = 4U,
        .group_count_z = 2U,
    };
    static_assert(is_valid(compute_dispatch));

    constexpr ComputeDispatchDesc invalid_compute_dispatch{
        .group_count_x = 0U,
        .group_count_y = 1U,
        .group_count_z = 1U,
    };
    static_assert(!is_valid(invalid_compute_dispatch));

    GraphicsPipelineDesc depth_only_pipeline;
    depth_only_pipeline.vertex_shader = &vertex_shader;
    depth_only_pipeline.fragment_shader = nullptr;
    depth_only_pipeline.color_format = TextureFormat::undefined;
    depth_only_pipeline.depth.format = TextureFormat::depth32_float;
    depth_only_pipeline.depth.test_enabled = true;
    depth_only_pipeline.depth.write_enabled = true;
    assert(is_valid(depth_only_pipeline));

    auto invalid_depth_only = depth_only_pipeline;
    invalid_depth_only.depth.write_enabled = false;
    assert(!is_valid(invalid_depth_only));

    auto invalid_color_without_fragment = depth_only_pipeline;
    invalid_color_without_fragment.color_format = TextureFormat::bgra8_unorm;
    assert(!is_valid(invalid_color_without_fragment));

    GraphicsPipelineDesc color_pipeline;
    color_pipeline.vertex_shader = &vertex_shader;
    color_pipeline.fragment_shader = &fragment_shader;
    color_pipeline.color_format = TextureFormat::bgra8_unorm;
    assert(is_valid(color_pipeline));

    constexpr RenderPassDesc render_pass{};
    static_assert(render_pass.clear_color_attachment);
    static_assert(render_pass.clear_color.alpha == 1.0F);

    static_assert(texture_format_bytes_per_pixel(TextureFormat::rgba8_unorm) == 4U);
    static_assert(texture_format_bytes_per_pixel(TextureFormat::undefined) == 0U);
    static_assert(invalid_submission == 0U);

    return 0;
}
