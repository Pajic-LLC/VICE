#include <vice/render/rhi.hpp>

#include <array>
#include <cassert>
#include <cstddef>

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
    assert(capabilities.max_sampler_anisotropy == 16U);
    assert(capabilities.max_texture_dimension_1d == 0U);
    assert(capabilities.max_texture_dimension_2d == 0U);
    assert(capabilities.max_texture_dimension_3d == 0U);
    assert(capabilities.max_texture_array_layers == 0U);
    assert(capabilities.recommended_frames_in_flight == 2U);

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

    constexpr RenderPassDesc render_pass{};
    static_assert(render_pass.clear_color_attachment);
    static_assert(render_pass.clear_color.alpha == 1.0F);

    static_assert(texture_format_bytes_per_pixel(TextureFormat::rgba8_unorm) == 4U);
    static_assert(texture_format_bytes_per_pixel(TextureFormat::undefined) == 0U);
    static_assert(invalid_submission == 0U);

    return 0;
}
