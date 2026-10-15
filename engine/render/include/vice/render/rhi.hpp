#pragma once

#include <vice/platform/types.hpp>
#include <vice/platform/window.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace vice::render {

enum class Backend {
    metal,
    direct3d12,
};

[[nodiscard]] constexpr std::string_view backend_name(const Backend backend) noexcept {
    switch (backend) {
    case Backend::metal: return "Metal";
    case Backend::direct3d12: return "Direct3D 12";
    }
    return "Unknown";
}

struct Color {
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    float alpha = 1.0F;
};

enum class MemoryLocation : std::uint8_t {
    gpu_only,
    cpu_to_gpu,
    gpu_to_cpu,
};

enum class BufferUsage : std::uint32_t {
    none = 0,
    vertex = 1U << 0U,
    index = 1U << 1U,
    uniform = 1U << 2U,
    storage = 1U << 3U,
    copy_source = 1U << 4U,
    copy_destination = 1U << 5U,
};

[[nodiscard]] constexpr BufferUsage operator|(const BufferUsage left, const BufferUsage right) noexcept {
    return static_cast<BufferUsage>(
        static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right)
    );
}

[[nodiscard]] constexpr BufferUsage operator&(const BufferUsage left, const BufferUsage right) noexcept {
    return static_cast<BufferUsage>(
        static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right)
    );
}

constexpr BufferUsage& operator|=(BufferUsage& left, const BufferUsage right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool has_usage(const BufferUsage value, const BufferUsage flag) noexcept {
    return (value & flag) == flag;
}

struct BufferDesc {
    std::uint64_t size = 0;
    BufferUsage usage = BufferUsage::none;
    MemoryLocation memory = MemoryLocation::gpu_only;
    std::string_view debug_name;
};

enum class TextureDimension : std::uint8_t {
    d1,
    d2,
    d3,
};

enum class TextureFormat : std::uint8_t {
    undefined,
    rgba8_unorm,
    bgra8_unorm,
    rgba16_float,
    r32_float,
    depth32_float,
};

enum class TextureUsage : std::uint32_t {
    none = 0,
    sampled = 1U << 0U,
    storage = 1U << 1U,
    color_attachment = 1U << 2U,
    depth_attachment = 1U << 3U,
    copy_source = 1U << 4U,
    copy_destination = 1U << 5U,
};

[[nodiscard]] constexpr TextureUsage operator|(
    const TextureUsage left,
    const TextureUsage right
) noexcept {
    return static_cast<TextureUsage>(
        static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right)
    );
}

[[nodiscard]] constexpr TextureUsage operator&(
    const TextureUsage left,
    const TextureUsage right
) noexcept {
    return static_cast<TextureUsage>(
        static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right)
    );
}

constexpr TextureUsage& operator|=(TextureUsage& left, const TextureUsage right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool has_usage(const TextureUsage value, const TextureUsage flag) noexcept {
    return (value & flag) == flag;
}

struct TextureDesc {
    TextureDimension dimension = TextureDimension::d2;
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t depth = 1;
    std::uint32_t mip_levels = 1;
    std::uint32_t array_layers = 1;
    std::uint32_t sample_count = 1;
    TextureFormat format = TextureFormat::rgba8_unorm;
    TextureUsage usage = TextureUsage::sampled;
    std::string_view debug_name;
};

enum class Filter : std::uint8_t {
    nearest,
    linear,
};

enum class AddressMode : std::uint8_t {
    clamp_to_edge,
    repeat,
    mirrored_repeat,
};

struct SamplerDesc {
    Filter min_filter = Filter::linear;
    Filter mag_filter = Filter::linear;
    Filter mip_filter = Filter::linear;
    AddressMode address_u = AddressMode::clamp_to_edge;
    AddressMode address_v = AddressMode::clamp_to_edge;
    AddressMode address_w = AddressMode::clamp_to_edge;
    std::uint32_t max_anisotropy = 1;
    std::string_view debug_name;
};

[[nodiscard]] constexpr bool is_valid(const BufferDesc& desc) noexcept {
    if (desc.size == 0 || desc.usage == BufferUsage::none) {
        return false;
    }

    if (desc.memory == MemoryLocation::cpu_to_gpu) {
        if (has_usage(desc.usage, BufferUsage::copy_destination) ||
            has_usage(desc.usage, BufferUsage::storage)) {
            return false;
        }
    }

    if (desc.memory == MemoryLocation::gpu_to_cpu) {
        return desc.usage == BufferUsage::copy_destination;
    }

    return true;
}

[[nodiscard]] constexpr bool is_valid(const TextureDesc& desc) noexcept {
    if (desc.width == 0 || desc.height == 0 || desc.depth == 0 ||
        desc.mip_levels == 0 || desc.array_layers == 0 ||
        desc.format == TextureFormat::undefined || desc.usage == TextureUsage::none) {
        return false;
    }

    if (desc.dimension == TextureDimension::d1 && (desc.height != 1 || desc.depth != 1)) {
        return false;
    }
    if (desc.dimension == TextureDimension::d2 && desc.depth != 1) {
        return false;
    }
    if (desc.dimension == TextureDimension::d3 && desc.array_layers != 1) {
        return false;
    }

    if (desc.sample_count != 1 && desc.sample_count != 2 &&
        desc.sample_count != 4 && desc.sample_count != 8) {
        return false;
    }
    if (desc.sample_count > 1 &&
        (desc.dimension != TextureDimension::d2 || desc.array_layers != 1 ||
        desc.mip_levels != 1 || has_usage(desc.usage, TextureUsage::storage))) {
        return false;
    }

    const bool depth_format = desc.format == TextureFormat::depth32_float;
    if (depth_format && (has_usage(desc.usage, TextureUsage::color_attachment) ||
        has_usage(desc.usage, TextureUsage::storage))) {
        return false;
    }
    if (!depth_format && has_usage(desc.usage, TextureUsage::depth_attachment)) {
        return false;
    }

    return true;
}

[[nodiscard]] constexpr bool is_valid(const SamplerDesc& desc) noexcept {
    if (desc.max_anisotropy < 1 || desc.max_anisotropy > 16) {
        return false;
    }
    if (desc.max_anisotropy > 1 &&
        (desc.min_filter != Filter::linear || desc.mag_filter != Filter::linear ||
            desc.mip_filter != Filter::linear)) {
        return false;
    }
    return true;
}

[[nodiscard]] constexpr std::uint32_t texture_format_bytes_per_pixel(
    const TextureFormat format
) noexcept {
    switch (format) {
    case TextureFormat::rgba8_unorm:
    case TextureFormat::bgra8_unorm:
    case TextureFormat::r32_float:
    case TextureFormat::depth32_float:
        return 4;
    case TextureFormat::rgba16_float:
        return 8;
    case TextureFormat::undefined:
        return 0;
    }
    return 0;
}

enum class ShaderStage : std::uint8_t {
    vertex,
    fragment,
    compute,
};

enum class ShaderSourceLanguage : std::uint8_t {
    metal,
    hlsl,
};

[[nodiscard]] constexpr std::string_view shader_stage_name(const ShaderStage stage) noexcept {
    switch (stage) {
    case ShaderStage::vertex: return "vertex";
    case ShaderStage::fragment: return "fragment";
    case ShaderStage::compute: return "compute";
    }
    return "unknown";
}

struct ShaderModuleDesc {
    ShaderStage stage = ShaderStage::vertex;
    ShaderSourceLanguage language = ShaderSourceLanguage::hlsl;
    std::string_view source;
    std::string_view entry_point = "main";
    std::string_view debug_name;
};

[[nodiscard]] constexpr bool is_valid(const ShaderModuleDesc& desc) noexcept {
    return !desc.source.empty() && !desc.entry_point.empty();
}

enum class PrimitiveTopology : std::uint8_t {
    triangle_list,
};

enum class CullMode : std::uint8_t {
    none,
    front,
    back,
};

enum class FrontFace : std::uint8_t {
    counter_clockwise,
    clockwise,
};

class ShaderModule;

struct GraphicsPipelineDesc {
    ShaderModule* vertex_shader = nullptr;
    ShaderModule* fragment_shader = nullptr;
    TextureFormat color_format = TextureFormat::bgra8_unorm;
    PrimitiveTopology topology = PrimitiveTopology::triangle_list;
    CullMode cull_mode = CullMode::back;
    FrontFace front_face = FrontFace::counter_clockwise;
    std::string_view debug_name;
};

[[nodiscard]] constexpr bool is_valid(const GraphicsPipelineDesc& desc) noexcept {
    return desc.vertex_shader != nullptr && desc.fragment_shader != nullptr &&
        desc.color_format != TextureFormat::undefined &&
        desc.color_format != TextureFormat::depth32_float;
}

struct RenderPassDesc {
    Color clear_color{0.0F, 0.0F, 0.0F, 1.0F};
    bool clear_color_attachment = true;
};

struct DeviceCapabilities {
    bool unified_memory = false;
    bool ray_tracing = false;
    bool mesh_shaders = false;
    std::uint32_t max_sampler_anisotropy = 16;
    std::uint32_t max_texture_dimension_1d = 0;
    std::uint32_t max_texture_dimension_2d = 0;
    std::uint32_t max_texture_dimension_3d = 0;
    std::uint32_t max_texture_array_layers = 0;
    std::uint32_t recommended_frames_in_flight = 2;
};

class Buffer {
public:
    virtual ~Buffer() = default;

    [[nodiscard]] virtual std::uint64_t size() const noexcept = 0;
    [[nodiscard]] virtual BufferUsage usage() const noexcept = 0;
    [[nodiscard]] virtual MemoryLocation memory_location() const noexcept = 0;
    virtual bool write(std::uint64_t offset, std::span<const std::byte> data) = 0;
    virtual bool read(std::uint64_t offset, std::span<std::byte> data) = 0;
};

class Texture {
public:
    virtual ~Texture() = default;

    [[nodiscard]] virtual TextureDimension dimension() const noexcept = 0;
    [[nodiscard]] virtual TextureFormat format() const noexcept = 0;
    [[nodiscard]] virtual TextureUsage usage() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t depth() const noexcept = 0;
};

class Sampler {
public:
    virtual ~Sampler() = default;
};

class ShaderModule {
public:
    virtual ~ShaderModule() = default;
    [[nodiscard]] virtual ShaderStage stage() const noexcept = 0;
};

class GraphicsPipeline {
public:
    virtual ~GraphicsPipeline() = default;
    [[nodiscard]] virtual PrimitiveTopology topology() const noexcept = 0;
};

class Swapchain;

class CommandList {
public:
    virtual ~CommandList() = default;

    virtual bool copy_buffer(
        Buffer& source,
        std::uint64_t source_offset,
        Buffer& destination,
        std::uint64_t destination_offset,
        std::uint64_t size
    ) = 0;

    virtual bool begin_render_pass(Swapchain& swapchain, const RenderPassDesc& desc) = 0;
    virtual bool set_graphics_pipeline(GraphicsPipeline& pipeline) = 0;
    virtual bool draw(
        std::uint32_t vertex_count,
        std::uint32_t instance_count = 1,
        std::uint32_t first_vertex = 0,
        std::uint32_t first_instance = 0
    ) = 0;
    virtual bool end_render_pass() = 0;
    virtual bool present(Swapchain& swapchain) = 0;
};

using SubmissionId = std::uint64_t;
inline constexpr SubmissionId invalid_submission = 0;

class Swapchain {
public:
    virtual ~Swapchain() = default;

    [[nodiscard]] virtual platform::WindowId window_id() const noexcept = 0;
    [[nodiscard]] virtual TextureFormat color_format() const noexcept = 0;
    virtual bool resize(platform::WindowMetrics metrics) = 0;
    virtual bool clear_and_present(Color color) = 0;
};

class Device {
public:
    virtual ~Device() = default;

    [[nodiscard]] static std::unique_ptr<Device> create();

    [[nodiscard]] virtual Backend backend() const noexcept = 0;
    [[nodiscard]] virtual DeviceCapabilities capabilities() const noexcept = 0;

    [[nodiscard]] virtual std::unique_ptr<Buffer> create_buffer(const BufferDesc& desc) = 0;
    [[nodiscard]] virtual std::unique_ptr<Texture> create_texture(const TextureDesc& desc) = 0;
    [[nodiscard]] virtual std::unique_ptr<Sampler> create_sampler(const SamplerDesc& desc) = 0;
    [[nodiscard]] virtual std::unique_ptr<ShaderModule> create_shader_module(
        const ShaderModuleDesc& desc
    ) = 0;
    [[nodiscard]] virtual std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDesc& desc
    ) = 0;
    [[nodiscard]] virtual std::unique_ptr<CommandList> create_command_list() = 0;

    virtual SubmissionId submit(std::unique_ptr<CommandList> command_list) = 0;
    [[nodiscard]] virtual SubmissionId completed_submission() const noexcept = 0;
    virtual bool wait_for_submission(SubmissionId submission) = 0;
    virtual bool wait_idle() = 0;

    [[nodiscard]] virtual std::unique_ptr<Swapchain> create_swapchain(platform::Window& window) = 0;
};

} // namespace vice::render
