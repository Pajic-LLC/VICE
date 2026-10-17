#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <dispatch/dispatch.h>

#include <vice/core/log.hpp>
#include <vice/platform/native_window.hpp>
#include <vice/render/pipeline_layout.hpp>
#include <vice/render/rhi.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vice::render {
namespace {

constexpr NSUInteger metal_shader_converter_argument_buffer_bind_point = 2U;
constexpr NSUInteger metal_shader_converter_vertex_buffer_base = 6U;
constexpr NSUInteger metal_shader_converter_vertex_attribute_base = 11U;
constexpr std::uint64_t metal_descriptor_entry_size = 3U * sizeof(std::uint64_t);

struct alignas(std::uint64_t) MetalDescriptorTableEntry {
    std::uint64_t gpu_address = 0;
    std::uint64_t texture_view_id = 0;
    std::uint64_t metadata = 0;
};

static_assert(sizeof(MetalDescriptorTableEntry) == metal_descriptor_entry_size);

[[nodiscard]] NSString* make_label(const std::string_view text) {
    if (text.empty()) {
        return nil;
    }
    return [[NSString alloc] initWithBytes:text.data()
                                    length:text.size()
                                  encoding:NSUTF8StringEncoding];
}

void log_error(const char* prefix, NSError* error) {
    std::string message{prefix};
    if (error != nil && error.localizedDescription != nil) {
        message += " ";
        message += error.localizedDescription.UTF8String;
    }
    core::log(core::LogLevel::error, message);
}

[[nodiscard]] MTLResourceOptions resource_options(const MemoryLocation memory) noexcept {
    switch (memory) {
    case MemoryLocation::gpu_only:
        return MTLResourceStorageModePrivate;
    case MemoryLocation::cpu_to_gpu:
    case MemoryLocation::gpu_to_cpu:
        return MTLResourceStorageModeShared;
    }
    return MTLResourceStorageModePrivate;
}

[[nodiscard]] MTLPixelFormat pixel_format(const TextureFormat format) noexcept {
    switch (format) {
    case TextureFormat::rgba8_unorm: return MTLPixelFormatRGBA8Unorm;
    case TextureFormat::bgra8_unorm: return MTLPixelFormatBGRA8Unorm;
    case TextureFormat::rgba16_float: return MTLPixelFormatRGBA16Float;
    case TextureFormat::r32_float: return MTLPixelFormatR32Float;
    case TextureFormat::depth32_float: return MTLPixelFormatDepth32Float;
    case TextureFormat::undefined: return MTLPixelFormatInvalid;
    }
    return MTLPixelFormatInvalid;
}

[[nodiscard]] MTLVertexFormat vertex_format(const VertexFormat format) noexcept {
    switch (format) {
    case VertexFormat::float32: return MTLVertexFormatFloat;
    case VertexFormat::float32x2: return MTLVertexFormatFloat2;
    case VertexFormat::float32x3: return MTLVertexFormatFloat3;
    case VertexFormat::float32x4: return MTLVertexFormatFloat4;
    case VertexFormat::float16x2: return MTLVertexFormatHalf2;
    case VertexFormat::float16x4: return MTLVertexFormatHalf4;
    case VertexFormat::uint32: return MTLVertexFormatUInt;
    case VertexFormat::uint32x2: return MTLVertexFormatUInt2;
    case VertexFormat::uint32x3: return MTLVertexFormatUInt3;
    case VertexFormat::uint32x4: return MTLVertexFormatUInt4;
    case VertexFormat::sint32: return MTLVertexFormatInt;
    case VertexFormat::sint32x2: return MTLVertexFormatInt2;
    case VertexFormat::sint32x3: return MTLVertexFormatInt3;
    case VertexFormat::sint32x4: return MTLVertexFormatInt4;
    case VertexFormat::uint8x2: return MTLVertexFormatUChar2;
    case VertexFormat::uint8x4: return MTLVertexFormatUChar4;
    case VertexFormat::sint8x2: return MTLVertexFormatChar2;
    case VertexFormat::sint8x4: return MTLVertexFormatChar4;
    case VertexFormat::unorm8x2: return MTLVertexFormatUChar2Normalized;
    case VertexFormat::unorm8x4: return MTLVertexFormatUChar4Normalized;
    case VertexFormat::snorm8x2: return MTLVertexFormatChar2Normalized;
    case VertexFormat::snorm8x4: return MTLVertexFormatChar4Normalized;
    case VertexFormat::uint16x2: return MTLVertexFormatUShort2;
    case VertexFormat::uint16x4: return MTLVertexFormatUShort4;
    case VertexFormat::sint16x2: return MTLVertexFormatShort2;
    case VertexFormat::sint16x4: return MTLVertexFormatShort4;
    case VertexFormat::unorm16x2: return MTLVertexFormatUShort2Normalized;
    case VertexFormat::unorm16x4: return MTLVertexFormatUShort4Normalized;
    case VertexFormat::snorm16x2: return MTLVertexFormatShort2Normalized;
    case VertexFormat::snorm16x4: return MTLVertexFormatShort4Normalized;
    }
    return MTLVertexFormatInvalid;
}

[[nodiscard]] MTLIndexType index_type(const IndexFormat format) noexcept {
    return format == IndexFormat::uint16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

[[nodiscard]] MTLCompareFunction comparison_func(const CompareOp compare) noexcept {
    switch (compare) {
    case CompareOp::never: return MTLCompareFunctionNever;
    case CompareOp::less: return MTLCompareFunctionLess;
    case CompareOp::less_equal: return MTLCompareFunctionLessEqual;
    case CompareOp::greater: return MTLCompareFunctionGreater;
    case CompareOp::greater_equal: return MTLCompareFunctionGreaterEqual;
    case CompareOp::equal: return MTLCompareFunctionEqual;
    case CompareOp::not_equal: return MTLCompareFunctionNotEqual;
    case CompareOp::always: return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionAlways;
}

[[nodiscard]] MTLTextureUsage texture_usage(const TextureUsage usage) noexcept {
    MTLTextureUsage result = MTLTextureUsageUnknown;
    if (has_usage(usage, TextureUsage::sampled)) {
        result |= MTLTextureUsageShaderRead;
    }
    if (has_usage(usage, TextureUsage::storage)) {
        result |= MTLTextureUsageShaderWrite;
    }
    if (has_usage(usage, TextureUsage::color_attachment) ||
        has_usage(usage, TextureUsage::depth_attachment)) {
        result |= MTLTextureUsageRenderTarget;
    }
    return result;
}

[[nodiscard]] MTLTextureType texture_type(const TextureDesc& desc) noexcept {
    switch (desc.dimension) {
    case TextureDimension::d1:
        return desc.array_layers > 1 ? MTLTextureType1DArray : MTLTextureType1D;
    case TextureDimension::d2:
        if (desc.sample_count > 1) {
            return MTLTextureType2DMultisample;
        }
        return desc.array_layers > 1 ? MTLTextureType2DArray : MTLTextureType2D;
    case TextureDimension::d3:
        return MTLTextureType3D;
    }
    return MTLTextureType2D;
}

[[nodiscard]] MTLSamplerMinMagFilter min_mag_filter(const Filter filter) noexcept {
    return filter == Filter::nearest ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
}

[[nodiscard]] MTLSamplerMipFilter mip_filter(const Filter filter) noexcept {
    return filter == Filter::nearest ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
}

[[nodiscard]] MTLSamplerAddressMode address_mode(const AddressMode mode) noexcept {
    switch (mode) {
    case AddressMode::clamp_to_edge: return MTLSamplerAddressModeClampToEdge;
    case AddressMode::repeat: return MTLSamplerAddressModeRepeat;
    case AddressMode::mirrored_repeat: return MTLSamplerAddressModeMirrorRepeat;
    }
    return MTLSamplerAddressModeClampToEdge;
}

[[nodiscard]] MTLCullMode cull_mode(const CullMode mode) noexcept {
    switch (mode) {
    case CullMode::none: return MTLCullModeNone;
    case CullMode::front: return MTLCullModeFront;
    case CullMode::back: return MTLCullModeBack;
    }
    return MTLCullModeNone;
}

[[nodiscard]] MTLWinding front_winding(const FrontFace face) noexcept {
    return face == FrontFace::counter_clockwise ? MTLWindingCounterClockwise : MTLWindingClockwise;
}

[[nodiscard]] ShaderStageMask stage_mask(const ShaderStage stage) noexcept {
    switch (stage) {
    case ShaderStage::vertex: return ShaderStageMask::vertex;
    case ShaderStage::fragment: return ShaderStageMask::fragment;
    case ShaderStage::compute: return ShaderStageMask::compute;
    }
    return ShaderStageMask::none;
}

[[nodiscard]] bool same_binding(
    const ShaderBindingKey& left,
    const ShaderBindingKey& right
) noexcept {
    return left.register_class == right.register_class &&
        left.binding == right.binding && left.space == right.space &&
        left.array_element == right.array_element;
}

[[nodiscard]] bool same_location_namespace(
    const MetalShaderResourceLocationDesc& location,
    const ShaderResourceBindingDesc& resource
) noexcept {
    return location.register_class == resource.register_class &&
        location.binding == resource.binding && location.space == resource.space;
}

[[nodiscard]] bool validate_metal_shader_layout(
    const std::span<const ShaderResourceBindingDesc> resources,
    const std::span<const MetalShaderResourceLocationDesc> locations,
    std::string& diagnostics
) {
    if (resources.empty()) {
        if (!locations.empty()) {
            diagnostics = "Metal shader locations exist without reflected logical resources.";
            return false;
        }
        return true;
    }
    for (const auto& resource : resources) {
        if (resource.unbounded) {
            diagnostics =
                "Metal automatic resource layout does not support unbounded ranges. "
                "Use the explicit root-signature conversion path.";
            return false;
        }
        const auto found = std::find_if(
            locations.begin(),
            locations.end(),
            [&](const MetalShaderResourceLocationDesc& location) {
                return same_location_namespace(location, resource);
            }
        );
        if (found == locations.end()) {
            diagnostics = "Metal shader is missing a converter-reflected resource location.";
            return false;
        }
        const std::uint64_t required =
            static_cast<std::uint64_t>(resource.count) * metal_descriptor_entry_size;
        if (found->size < required) {
            diagnostics = "Metal resource location is too small for the reflected resource array.";
            return false;
        }
    }
    for (std::size_t index = 0; index < locations.size(); ++index) {
        const auto& location = locations[index];
        const auto logical = std::find_if(
            resources.begin(),
            resources.end(),
            [&](const ShaderResourceBindingDesc& resource) {
                return same_location_namespace(location, resource);
            }
        );
        if (logical == resources.end()) {
            diagnostics = "Metal converter location does not match a logical shader resource.";
            return false;
        }
        for (std::size_t other = index + 1U; other < locations.size(); ++other) {
            if (locations[other].register_class == location.register_class &&
                locations[other].binding == location.binding &&
                locations[other].space == location.space) {
                diagnostics = "Metal shader contains duplicate converter resource locations.";
                return false;
            }
        }
    }
    return true;
}

struct MetalVertexStreamInfo {
    std::uint32_t stride = 0;
    VertexStepMode step_mode = VertexStepMode::per_vertex;
    std::uint32_t step_rate = 1;
};

class MetalBuffer final : public Buffer {
public:
    MetalBuffer(const BufferDesc& desc, id<MTLBuffer> buffer)
        : size_{desc.size}, usage_{desc.usage}, memory_{desc.memory}, buffer_{buffer} {}

    [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }
    [[nodiscard]] BufferUsage usage() const noexcept override { return usage_; }
    [[nodiscard]] MemoryLocation memory_location() const noexcept override { return memory_; }

    bool write(const std::uint64_t offset, const std::span<const std::byte> data) override {
        if (memory_ != MemoryLocation::cpu_to_gpu || buffer_ == nil ||
            offset > size_ || data.size() > size_ - offset) {
            return false;
        }
        if (data.empty()) {
            return true;
        }
        auto* destination = static_cast<std::byte*>([buffer_ contents]);
        if (destination == nullptr) {
            return false;
        }
        std::memcpy(destination + static_cast<std::size_t>(offset), data.data(), data.size());
        return true;
    }

    bool read(const std::uint64_t offset, const std::span<std::byte> data) override {
        if (memory_ != MemoryLocation::gpu_to_cpu || buffer_ == nil ||
            offset > size_ || data.size() > size_ - offset) {
            return false;
        }
        if (data.empty()) {
            return true;
        }
        const auto* source = static_cast<const std::byte*>([buffer_ contents]);
        if (source == nullptr) {
            return false;
        }
        std::memcpy(data.data(), source + static_cast<std::size_t>(offset), data.size());
        return true;
    }

    [[nodiscard]] id<MTLBuffer> native() const noexcept { return buffer_; }

private:
    std::uint64_t size_ = 0;
    BufferUsage usage_ = BufferUsage::none;
    MemoryLocation memory_ = MemoryLocation::gpu_only;
    id<MTLBuffer> buffer_ = nil;
};

class MetalTexture final : public Texture {
public:
    MetalTexture(const TextureDesc& desc, id<MTLTexture> texture)
        : desc_{desc}, texture_{texture} {
        desc_.debug_name = {};
    }

    [[nodiscard]] TextureDimension dimension() const noexcept override { return desc_.dimension; }
    [[nodiscard]] TextureFormat format() const noexcept override { return desc_.format; }
    [[nodiscard]] TextureUsage usage() const noexcept override { return desc_.usage; }
    [[nodiscard]] std::uint32_t width() const noexcept override { return desc_.width; }
    [[nodiscard]] std::uint32_t height() const noexcept override { return desc_.height; }
    [[nodiscard]] std::uint32_t depth() const noexcept override { return desc_.depth; }
    [[nodiscard]] id<MTLTexture> native() const noexcept { return texture_; }

private:
    TextureDesc desc_{};
    id<MTLTexture> texture_ = nil;
};

class MetalSampler final : public Sampler {
public:
    explicit MetalSampler(id<MTLSamplerState> sampler) : sampler_{sampler} {}
    [[nodiscard]] id<MTLSamplerState> native() const noexcept { return sampler_; }

private:
    id<MTLSamplerState> sampler_ = nil;
};

class MetalShaderModule final : public ShaderModule {
public:
    MetalShaderModule(
        const ShaderStage stage,
        id<MTLLibrary> library,
        id<MTLFunction> function,
        std::vector<ShaderResourceBindingDesc> resources = {},
        std::vector<MetalShaderResourceLocationDesc> locations = {}
    ) : stage_{stage}, library_{library}, function_{function},
        resources_{std::move(resources)}, locations_{std::move(locations)} {}

    [[nodiscard]] ShaderStage stage() const noexcept override { return stage_; }
    [[nodiscard]] std::span<const ShaderResourceBindingDesc> resources() const noexcept override {
        return resources_;
    }
    [[nodiscard]] std::span<const MetalShaderResourceLocationDesc> locations() const noexcept {
        return locations_;
    }
    [[nodiscard]] id<MTLFunction> function() const noexcept { return function_; }

private:
    ShaderStage stage_ = ShaderStage::vertex;
    id<MTLLibrary> library_ = nil;
    id<MTLFunction> function_ = nil;
    std::vector<ShaderResourceBindingDesc> resources_;
    std::vector<MetalShaderResourceLocationDesc> locations_;
};

class MetalGraphicsPipeline final : public GraphicsPipeline {
public:
    MetalGraphicsPipeline(
        id<MTLRenderPipelineState> state,
        id<MTLDepthStencilState> depth_state,
        const PrimitiveTopology topology,
        const CullMode cull,
        const FrontFace front,
        const DepthStateDesc depth,
        PipelineLayout layout,
        std::vector<MetalShaderResourceLocationDesc> vertex_locations,
        std::vector<MetalShaderResourceLocationDesc> fragment_locations,
        std::vector<MetalVertexStreamInfo> vertex_streams
    ) : state_{state}, depth_state_{depth_state}, topology_{topology}, cull_{cull}, front_{front},
        depth_{depth}, layout_{std::move(layout)}, vertex_locations_{std::move(vertex_locations)},
        fragment_locations_{std::move(fragment_locations)},
        vertex_streams_{std::move(vertex_streams)} {}

    [[nodiscard]] PrimitiveTopology topology() const noexcept override { return topology_; }
    [[nodiscard]] id<MTLRenderPipelineState> native() const noexcept { return state_; }
    [[nodiscard]] id<MTLDepthStencilState> depth_state() const noexcept { return depth_state_; }
    [[nodiscard]] CullMode cull() const noexcept { return cull_; }
    [[nodiscard]] FrontFace front() const noexcept { return front_; }
    [[nodiscard]] const DepthStateDesc& depth() const noexcept { return depth_; }
    [[nodiscard]] const PipelineLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] std::size_t vertex_stream_count() const noexcept { return vertex_streams_.size(); }
    [[nodiscard]] const MetalVertexStreamInfo* vertex_stream(const std::uint32_t slot) const noexcept {
        return slot < vertex_streams_.size() ? &vertex_streams_[slot] : nullptr;
    }

    [[nodiscard]] bool has_binding(
        const ShaderBindingKey& key,
        const ShaderRegisterClass expected_class
    ) const noexcept {
        if (key.register_class != expected_class) {
            return false;
        }
        const auto found = std::find_if(
            layout_.resources.begin(),
            layout_.resources.end(),
            [&](const PipelineResourceBinding& resource) {
                if (resource.register_class != key.register_class ||
                    resource.binding != key.binding || resource.space != key.space) {
                    return false;
                }
                return !resource.unbounded && key.array_element < resource.count;
            }
        );
        return found != layout_.resources.end();
    }

    [[nodiscard]] std::span<const MetalShaderResourceLocationDesc> locations(
        const ShaderStage stage
    ) const noexcept {
        return stage == ShaderStage::vertex
            ? std::span<const MetalShaderResourceLocationDesc>{vertex_locations_}
            : std::span<const MetalShaderResourceLocationDesc>{fragment_locations_};
    }

private:
    id<MTLRenderPipelineState> state_ = nil;
    id<MTLDepthStencilState> depth_state_ = nil;
    PrimitiveTopology topology_ = PrimitiveTopology::triangle_list;
    CullMode cull_ = CullMode::back;
    FrontFace front_ = FrontFace::counter_clockwise;
    DepthStateDesc depth_{};
    PipelineLayout layout_;
    std::vector<MetalShaderResourceLocationDesc> vertex_locations_;
    std::vector<MetalShaderResourceLocationDesc> fragment_locations_;
    std::vector<MetalVertexStreamInfo> vertex_streams_;
};

class MetalSwapchain final : public Swapchain {
public:
    MetalSwapchain(
        platform::Window& window,
        id<MTLDevice> device,
        id<MTLCommandQueue> command_queue
    ) : window_{&window}, device_{device}, command_queue_{command_queue} {
        const auto native = platform::NativeWindowAccess::get(window);
        if (native.system != platform::NativeWindowSystem::cocoa || native.view == nullptr) {
            return;
        }

        view_ = (__bridge NSView*)native.view;
        layer_ = [CAMetalLayer layer];
        [layer_ setDevice:device_];
        [layer_ setPixelFormat:MTLPixelFormatBGRA8Unorm];
        [layer_ setFramebufferOnly:YES];
        [layer_ setOpaque:YES];
        [view_ setWantsLayer:YES];
        [view_ setLayer:layer_];
        valid_ = resize(window.metrics());
    }

    ~MetalSwapchain() override {
        @autoreleasepool {
            if (view_ != nil && [view_ layer] == layer_) {
                [view_ setLayer:nil];
            }
            layer_ = nil;
            view_ = nil;
        }
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }

    [[nodiscard]] platform::WindowId window_id() const noexcept override {
        return window_ != nullptr ? window_->id() : platform::invalid_window_id;
    }

    [[nodiscard]] TextureFormat color_format() const noexcept override {
        return TextureFormat::bgra8_unorm;
    }

    bool resize(const platform::WindowMetrics metrics) override {
        if (layer_ == nil) {
            return false;
        }
        const auto width = std::max<std::uint32_t>(1U, metrics.drawable_width);
        const auto height = std::max<std::uint32_t>(1U, metrics.drawable_height);
        const CGFloat scale = metrics.scale_factor > 0.0F
            ? static_cast<CGFloat>(metrics.scale_factor)
            : 1.0;
        [layer_ setContentsScale:scale];
        [layer_ setDrawableSize:CGSizeMake(
            static_cast<CGFloat>(width),
            static_cast<CGFloat>(height)
        )];
        return true;
    }

    bool clear_and_present(const Color color) override {
        @autoreleasepool {
            if (!valid_ || layer_ == nil || command_queue_ == nil) {
                return false;
            }
            id<CAMetalDrawable> drawable = [layer_ nextDrawable];
            if (drawable == nil) {
                return false;
            }
            id<MTLCommandBuffer> command_buffer = [command_queue_ commandBuffer];
            if (command_buffer == nil) {
                return false;
            }
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = drawable.texture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(
                color.red, color.green, color.blue, color.alpha
            );
            id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:pass];
            if (encoder == nil) {
                return false;
            }
            [encoder endEncoding];
            [command_buffer presentDrawable:drawable];
            [command_buffer commit];
            return true;
        }
    }

    [[nodiscard]] id<CAMetalDrawable> acquire_drawable() const {
        return layer_ != nil ? [layer_ nextDrawable] : nil;
    }

private:
    platform::Window* window_ = nullptr;
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> command_queue_ = nil;
    NSView* view_ = nil;
    CAMetalLayer* layer_ = nil;
    bool valid_ = false;
};

enum class MetalBoundKind : std::uint8_t {
    constant_buffer,
    read_buffer,
    write_buffer,
    texture,
    storage_texture,
    sampler,
};

struct MetalBoundResource {
    ShaderBindingKey key{};
    MetalBoundKind kind = MetalBoundKind::read_buffer;
    MetalBuffer* buffer = nullptr;
    MetalTexture* texture = nullptr;
    MetalSampler* sampler = nullptr;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

class MetalCommandList final : public CommandList {
public:
    MetalCommandList(id<MTLDevice> device, id<MTLCommandBuffer> command_buffer)
        : device_{device}, command_buffer_{command_buffer},
          retained_argument_buffers_{[[NSMutableArray alloc] init]},
          retained_samplers_{[[NSMutableArray alloc] init]} {}

    ~MetalCommandList() override {
        if (render_encoder_ != nil) {
            [render_encoder_ endEncoding];
        }
    }

    [[nodiscard]] bool valid() const noexcept { return command_buffer_ != nil; }

    bool copy_buffer(
        Buffer& source,
        const std::uint64_t source_offset,
        Buffer& destination,
        const std::uint64_t destination_offset,
        const std::uint64_t size
    ) override {
        if (render_encoder_ != nil) {
            return false;
        }
        auto* source_buffer = dynamic_cast<MetalBuffer*>(&source);
        auto* destination_buffer = dynamic_cast<MetalBuffer*>(&destination);
        if (source_buffer == nullptr || destination_buffer == nullptr || command_buffer_ == nil ||
            !has_usage(source.usage(), BufferUsage::copy_source) ||
            !has_usage(destination.usage(), BufferUsage::copy_destination) ||
            source_offset > source.size() || size > source.size() - source_offset ||
            destination_offset > destination.size() || size > destination.size() - destination_offset) {
            return false;
        }
        if (size == 0) {
            return true;
        }
        id<MTLBlitCommandEncoder> encoder = [command_buffer_ blitCommandEncoder];
        if (encoder == nil) {
            return false;
        }
        [encoder copyFromBuffer:source_buffer->native()
                   sourceOffset:static_cast<NSUInteger>(source_offset)
                       toBuffer:destination_buffer->native()
              destinationOffset:static_cast<NSUInteger>(destination_offset)
                           size:static_cast<NSUInteger>(size)];
        [encoder endEncoding];
        return true;
    }

    bool begin_render_pass(Swapchain& swapchain, const RenderPassDesc& desc) override {
        if (render_encoder_ != nil || drawable_ != nil || command_buffer_ == nil) {
            return false;
        }
        auto* metal_swapchain = dynamic_cast<MetalSwapchain*>(&swapchain);
        if (metal_swapchain == nullptr) {
            return false;
        }
        drawable_ = metal_swapchain->acquire_drawable();
        if (drawable_ == nil) {
            return false;
        }

        MetalTexture* depth_texture = nullptr;
        if (desc.depth_attachment != nullptr) {
            depth_texture = dynamic_cast<MetalTexture*>(desc.depth_attachment);
            if (depth_texture == nullptr || depth_texture->native() == nil ||
                depth_texture->format() != TextureFormat::depth32_float ||
                !has_usage(depth_texture->usage(), TextureUsage::depth_attachment) ||
                depth_texture->width() != drawable_.texture.width ||
                depth_texture->height() != drawable_.texture.height) {
                return false;
            }
        }

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable_.texture;
        pass.colorAttachments[0].loadAction = desc.clear_color_attachment
            ? MTLLoadActionClear
            : MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(
            desc.clear_color.red,
            desc.clear_color.green,
            desc.clear_color.blue,
            desc.clear_color.alpha
        );
        if (depth_texture != nullptr) {
            pass.depthAttachment.texture = depth_texture->native();
            pass.depthAttachment.loadAction = desc.clear_depth_attachment
                ? MTLLoadActionClear
                : MTLLoadActionLoad;
            pass.depthAttachment.storeAction = MTLStoreActionStore;
            pass.depthAttachment.clearDepth = desc.clear_depth;
        }

        render_encoder_ = [command_buffer_ renderCommandEncoderWithDescriptor:pass];
        active_swapchain_ = metal_swapchain;
        active_depth_texture_ = depth_texture;
        reset_mesh_state();
        active_pipeline_ = nullptr;
        bound_resources_.clear();
        pipeline_bound_ = false;
        return render_encoder_ != nil;
    }

    bool set_viewport(const Viewport& viewport) override {
        if (render_encoder_ == nil || !is_valid(viewport)) {
            return false;
        }
        const MTLViewport native{
            static_cast<double>(viewport.x),
            static_cast<double>(viewport.y),
            static_cast<double>(viewport.width),
            static_cast<double>(viewport.height),
            static_cast<double>(viewport.min_depth),
            static_cast<double>(viewport.max_depth),
        };
        [render_encoder_ setViewport:native];
        return true;
    }

    bool set_scissor_rect(const ScissorRect& scissor) override {
        if (render_encoder_ == nil || !is_valid(scissor) || scissor.x < 0 || scissor.y < 0) {
            return false;
        }
        const std::uint64_t right =
            static_cast<std::uint64_t>(scissor.x) + static_cast<std::uint64_t>(scissor.width);
        const std::uint64_t bottom =
            static_cast<std::uint64_t>(scissor.y) + static_cast<std::uint64_t>(scissor.height);
        if (right > std::numeric_limits<NSUInteger>::max() ||
            bottom > std::numeric_limits<NSUInteger>::max()) {
            return false;
        }
        const MTLScissorRect native{
            static_cast<NSUInteger>(scissor.x),
            static_cast<NSUInteger>(scissor.y),
            static_cast<NSUInteger>(scissor.width),
            static_cast<NSUInteger>(scissor.height),
        };
        [render_encoder_ setScissorRect:native];
        return true;
    }

    bool set_graphics_pipeline(GraphicsPipeline& pipeline) override {
        if (render_encoder_ == nil) {
            return false;
        }
        auto* metal_pipeline = dynamic_cast<MetalGraphicsPipeline*>(&pipeline);
        if (metal_pipeline == nullptr || metal_pipeline->native() == nil) {
            return false;
        }
        const auto& depth = metal_pipeline->depth();
        if (depth.format != TextureFormat::undefined &&
            (active_depth_texture_ == nullptr || active_depth_texture_->format() != depth.format)) {
            return false;
        }
        [render_encoder_ setRenderPipelineState:metal_pipeline->native()];
        [render_encoder_ setDepthStencilState:metal_pipeline->depth_state()];
        [render_encoder_ setCullMode:cull_mode(metal_pipeline->cull())];
        [render_encoder_ setFrontFacingWinding:front_winding(metal_pipeline->front())];
        active_pipeline_ = metal_pipeline;
        bound_resources_.clear();
        reset_mesh_state();
        pipeline_bound_ = true;
        return true;
    }

    bool set_vertex_buffer(
        const std::uint32_t slot,
        Buffer& buffer,
        const std::uint64_t offset
    ) override {
        auto* metal_buffer = dynamic_cast<MetalBuffer*>(&buffer);
        if (render_encoder_ == nil || !pipeline_bound_ || active_pipeline_ == nullptr ||
            metal_buffer == nullptr || !has_usage(buffer.usage(), BufferUsage::vertex) ||
            offset > buffer.size()) {
            return false;
        }
        const MetalVertexStreamInfo* stream = active_pipeline_->vertex_stream(slot);
        if (stream == nullptr || stream->stride == 0U) {
            return false;
        }
        const std::uint64_t remaining = buffer.size() - offset;
        if (remaining < stream->stride ||
            metal_shader_converter_vertex_buffer_base + slot >= 31U) {
            return false;
        }
        [render_encoder_ setVertexBuffer:metal_buffer->native()
                                  offset:static_cast<NSUInteger>(offset)
                                 atIndex:metal_shader_converter_vertex_buffer_base + slot];
        vertex_buffers_bound_[slot] = true;
        vertex_capacities_[slot] = remaining / stream->stride;
        return true;
    }

    bool set_index_buffer(
        Buffer& buffer,
        const IndexFormat format,
        const std::uint64_t offset
    ) override {
        auto* metal_buffer = dynamic_cast<MetalBuffer*>(&buffer);
        const std::uint32_t element_size = index_format_size(format);
        if (render_encoder_ == nil || !pipeline_bound_ || metal_buffer == nullptr ||
            !has_usage(buffer.usage(), BufferUsage::index) || element_size == 0U ||
            offset > buffer.size() || offset % element_size != 0U) {
            return false;
        }
        const std::uint64_t remaining = buffer.size() - offset;
        if (remaining < element_size) {
            return false;
        }
        index_buffer_ = metal_buffer;
        index_format_ = format;
        index_offset_ = offset;
        index_capacity_ = remaining / element_size;
        index_buffer_bound_ = true;
        return true;
    }

    bool set_graphics_constant_buffer(
        const ShaderBindingKey& binding,
        Buffer& buffer,
        const std::uint64_t offset,
        const std::uint64_t size
    ) override {
        auto* metal_buffer = dynamic_cast<MetalBuffer*>(&buffer);
        if (!binding_allowed(binding, ShaderRegisterClass::constant_buffer) ||
            metal_buffer == nullptr || !has_usage(buffer.usage(), BufferUsage::uniform) ||
            offset > buffer.size() || (size != 0U && size > buffer.size() - offset)) {
            return false;
        }
        const std::uint64_t resolved_size = size == 0U ? buffer.size() - offset : size;
        if (resolved_size == 0U) {
            return false;
        }
        upsert_bound(MetalBoundResource{
            .key = binding,
            .kind = MetalBoundKind::constant_buffer,
            .buffer = metal_buffer,
            .offset = offset,
            .size = resolved_size,
        });
        return true;
    }

    bool set_graphics_buffer(
        const ShaderBindingKey& binding,
        Buffer& buffer,
        const BufferViewDesc& view
    ) override {
        auto* metal_buffer = dynamic_cast<MetalBuffer*>(&buffer);
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        if (!binding_allowed(binding, ShaderRegisterClass::shader_resource) ||
            metal_buffer == nullptr || !has_usage(buffer.usage(), BufferUsage::storage) ||
            !resolve_buffer_view(buffer, view, offset, size)) {
            return false;
        }
        upsert_bound(MetalBoundResource{
            .key = binding,
            .kind = MetalBoundKind::read_buffer,
            .buffer = metal_buffer,
            .offset = offset,
            .size = size,
        });
        return true;
    }

    bool set_graphics_storage_buffer(
        const ShaderBindingKey& binding,
        Buffer& buffer,
        const BufferViewDesc& view
    ) override {
        auto* metal_buffer = dynamic_cast<MetalBuffer*>(&buffer);
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        if (!binding_allowed(binding, ShaderRegisterClass::unordered_access) ||
            metal_buffer == nullptr || !has_usage(buffer.usage(), BufferUsage::storage) ||
            !resolve_buffer_view(buffer, view, offset, size)) {
            return false;
        }
        upsert_bound(MetalBoundResource{
            .key = binding,
            .kind = MetalBoundKind::write_buffer,
            .buffer = metal_buffer,
            .offset = offset,
            .size = size,
        });
        return true;
    }

    bool set_graphics_texture(const ShaderBindingKey& binding, Texture& texture) override {
        auto* metal_texture = dynamic_cast<MetalTexture*>(&texture);
        if (!binding_allowed(binding, ShaderRegisterClass::shader_resource) ||
            metal_texture == nullptr || !has_usage(texture.usage(), TextureUsage::sampled)) {
            return false;
        }
        upsert_bound(MetalBoundResource{
            .key = binding,
            .kind = MetalBoundKind::texture,
            .texture = metal_texture,
        });
        return true;
    }

    bool set_graphics_storage_texture(
        const ShaderBindingKey& binding,
        Texture& texture
    ) override {
        auto* metal_texture = dynamic_cast<MetalTexture*>(&texture);
        if (!binding_allowed(binding, ShaderRegisterClass::unordered_access) ||
            metal_texture == nullptr || !has_usage(texture.usage(), TextureUsage::storage)) {
            return false;
        }
        upsert_bound(MetalBoundResource{
            .key = binding,
            .kind = MetalBoundKind::storage_texture,
            .texture = metal_texture,
        });
        return true;
    }

    bool set_graphics_sampler(const ShaderBindingKey& binding, Sampler& sampler) override {
        auto* metal_sampler = dynamic_cast<MetalSampler*>(&sampler);
        if (!binding_allowed(binding, ShaderRegisterClass::sampler) || metal_sampler == nullptr) {
            return false;
        }
        upsert_bound(MetalBoundResource{
            .key = binding,
            .kind = MetalBoundKind::sampler,
            .sampler = metal_sampler,
        });
        return true;
    }

    bool draw(
        const std::uint32_t vertex_count,
        const std::uint32_t instance_count,
        const std::uint32_t first_vertex,
        const std::uint32_t first_instance
    ) override {
        if (render_encoder_ == nil || !pipeline_bound_ || active_pipeline_ == nullptr ||
            vertex_count == 0 || instance_count == 0 ||
            !vertex_streams_cover_draw(vertex_count, instance_count, first_vertex, first_instance)) {
            return false;
        }
        if (!prepare_draw_resources()) {
            return false;
        }
        [render_encoder_ drawPrimitives:MTLPrimitiveTypeTriangle
                            vertexStart:first_vertex
                            vertexCount:vertex_count
                          instanceCount:instance_count
                           baseInstance:first_instance];
        return true;
    }

    bool draw_indexed(
        const std::uint32_t index_count,
        const std::uint32_t instance_count,
        const std::uint32_t first_index,
        const std::int32_t vertex_offset,
        const std::uint32_t first_instance
    ) override {
        if (render_encoder_ == nil || !pipeline_bound_ || active_pipeline_ == nullptr ||
            !index_buffer_bound_ || index_buffer_ == nullptr || index_buffer_->native() == nil ||
            index_count == 0U || instance_count == 0U ||
            first_index > index_capacity_ || index_count > index_capacity_ - first_index ||
            !vertex_streams_cover_instances(instance_count, first_instance)) {
            return false;
        }
        if (!prepare_draw_resources()) {
            return false;
        }
        const std::uint64_t byte_offset = index_offset_ +
            static_cast<std::uint64_t>(first_index) * index_format_size(index_format_);
        if (byte_offset > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())) {
            return false;
        }
        [render_encoder_ drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                    indexCount:index_count
                                     indexType:index_type(index_format_)
                                   indexBuffer:index_buffer_->native()
                             indexBufferOffset:static_cast<NSUInteger>(byte_offset)
                                 instanceCount:instance_count
                                    baseVertex:vertex_offset
                                  baseInstance:first_instance];
        return true;
    }

    bool end_render_pass() override {
        if (render_encoder_ == nil) {
            return false;
        }
        [render_encoder_ endEncoding];
        render_encoder_ = nil;
        active_pipeline_ = nullptr;
        active_depth_texture_ = nullptr;
        bound_resources_.clear();
        reset_mesh_state();
        pipeline_bound_ = false;
        return true;
    }

    bool present(Swapchain& swapchain) override {
        auto* metal_swapchain = dynamic_cast<MetalSwapchain*>(&swapchain);
        if (render_encoder_ != nil || drawable_ == nil || metal_swapchain == nullptr ||
            metal_swapchain != active_swapchain_ || present_scheduled_) {
            return false;
        }
        [command_buffer_ presentDrawable:drawable_];
        present_scheduled_ = true;
        return true;
    }

    [[nodiscard]] id<MTLCommandBuffer> native() const noexcept { return command_buffer_; }

private:
    [[nodiscard]] bool binding_allowed(
        const ShaderBindingKey& binding,
        const ShaderRegisterClass expected_class
    ) const noexcept {
        return render_encoder_ != nil && pipeline_bound_ && active_pipeline_ != nullptr &&
            active_pipeline_->has_binding(binding, expected_class);
    }

    [[nodiscard]] bool all_vertex_buffers_bound() const noexcept {
        if (active_pipeline_ == nullptr) {
            return false;
        }
        for (std::size_t slot = 0; slot < active_pipeline_->vertex_stream_count(); ++slot) {
            if (!vertex_buffers_bound_[slot]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool vertex_streams_cover_instances(
        const std::uint32_t instance_count,
        const std::uint32_t first_instance
    ) const noexcept {
        if (!all_vertex_buffers_bound() || active_pipeline_ == nullptr || instance_count == 0U) {
            return false;
        }
        const std::uint64_t last_instance =
            static_cast<std::uint64_t>(first_instance) + instance_count - 1U;
        for (std::size_t slot = 0; slot < active_pipeline_->vertex_stream_count(); ++slot) {
            const auto* stream = active_pipeline_->vertex_stream(static_cast<std::uint32_t>(slot));
            if (stream == nullptr || stream->step_mode != VertexStepMode::per_instance) {
                continue;
            }
            const std::uint64_t required_element = last_instance / stream->step_rate;
            if (required_element >= vertex_capacities_[slot]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool vertex_streams_cover_draw(
        const std::uint32_t vertex_count,
        const std::uint32_t instance_count,
        const std::uint32_t first_vertex,
        const std::uint32_t first_instance
    ) const noexcept {
        if (!vertex_streams_cover_instances(instance_count, first_instance) || active_pipeline_ == nullptr) {
            return false;
        }
        const std::uint64_t vertex_end = static_cast<std::uint64_t>(first_vertex) + vertex_count;
        for (std::size_t slot = 0; slot < active_pipeline_->vertex_stream_count(); ++slot) {
            const auto* stream = active_pipeline_->vertex_stream(static_cast<std::uint32_t>(slot));
            if (stream != nullptr && stream->step_mode == VertexStepMode::per_vertex &&
                vertex_end > vertex_capacities_[slot]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static bool resolve_buffer_view(
        const Buffer& buffer,
        const BufferViewDesc& view,
        std::uint64_t& offset,
        std::uint64_t& size
    ) noexcept {
        if (!is_valid(view) || view.offset > buffer.size()) {
            return false;
        }
        offset = view.offset;
        size = view.size == 0U ? buffer.size() - view.offset : view.size;
        if (size == 0U || size > buffer.size() - view.offset) {
            return false;
        }
        if (view.structure_stride == 0U) {
            return offset % 4U == 0U && size % 4U == 0U;
        }
        return offset % view.structure_stride == 0U && size % view.structure_stride == 0U;
    }

    void upsert_bound(MetalBoundResource resource) {
        const auto found = std::find_if(
            bound_resources_.begin(),
            bound_resources_.end(),
            [&](const MetalBoundResource& existing) {
                return same_binding(existing.key, resource.key);
            }
        );
        if (found == bound_resources_.end()) {
            bound_resources_.push_back(std::move(resource));
        } else {
            *found = std::move(resource);
        }
    }

    [[nodiscard]] const MetalBoundResource* bound_resource(
        const ShaderBindingKey& key
    ) const noexcept {
        const auto found = std::find_if(
            bound_resources_.begin(),
            bound_resources_.end(),
            [&](const MetalBoundResource& resource) {
                return same_binding(resource.key, key);
            }
        );
        return found == bound_resources_.end() ? nullptr : &*found;
    }

    [[nodiscard]] const PipelineResourceBinding* logical_binding(
        const MetalShaderResourceLocationDesc& location
    ) const noexcept {
        if (active_pipeline_ == nullptr) {
            return nullptr;
        }
        const auto& resources = active_pipeline_->layout().resources;
        const auto found = std::find_if(
            resources.begin(),
            resources.end(),
            [&](const PipelineResourceBinding& resource) {
                return resource.register_class == location.register_class &&
                    resource.binding == location.binding && resource.space == location.space;
            }
        );
        return found == resources.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool all_required_resources_bound() const noexcept {
        if (active_pipeline_ == nullptr) {
            return false;
        }
        for (const auto& resource : active_pipeline_->layout().resources) {
            if (resource.unbounded) {
                return false;
            }
            for (std::uint32_t element = 0; element < resource.count; ++element) {
                const ShaderBindingKey key{
                    .register_class = resource.register_class,
                    .binding = resource.binding,
                    .space = resource.space,
                    .array_element = element,
                };
                if (bound_resource(key) == nullptr) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] bool prepare_draw_resources() {
        if (!all_required_resources_bound()) {
            if (active_pipeline_ != nullptr && !active_pipeline_->layout().resources.empty()) {
                core::log(core::LogLevel::error, "Metal draw is missing a reflected shader resource.");
                return false;
            }
        }
        return bind_stage_resources(ShaderStage::vertex) &&
            bind_stage_resources(ShaderStage::fragment);
    }

    [[nodiscard]] bool bind_stage_resources(const ShaderStage stage) {
        if (active_pipeline_ == nullptr || render_encoder_ == nil) {
            return false;
        }
        if (!active_pipeline_->locations(stage).empty()) {
            return encode_stage_argument_buffer(stage);
        }
        return bind_stage_direct(stage);
    }

    [[nodiscard]] bool bind_stage_direct(const ShaderStage stage) {
        if (active_pipeline_ == nullptr || render_encoder_ == nil) {
            return false;
        }
        const ShaderStageMask mask = stage_mask(stage);
        for (const auto& logical : active_pipeline_->layout().resources) {
            if (!has_stage(logical.stages, mask)) {
                continue;
            }
            if (logical.unbounded || logical.space != 0U) {
                core::log(
                    core::LogLevel::error,
                    "Metal development source shaders require bounded resources in register space 0."
                );
                return false;
            }
            for (std::uint32_t element = 0; element < logical.count; ++element) {
                const std::uint64_t native_index =
                    static_cast<std::uint64_t>(logical.binding) + element;
                if (native_index >= 31U) {
                    return false;
                }
                const ShaderBindingKey key{
                    .register_class = logical.register_class,
                    .binding = logical.binding,
                    .space = logical.space,
                    .array_element = element,
                };
                const MetalBoundResource* bound = bound_resource(key);
                if (bound == nullptr || !bind_direct_resource(stage, static_cast<NSUInteger>(native_index), *bound)) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] bool bind_direct_resource(
        const ShaderStage stage,
        const NSUInteger index,
        const MetalBoundResource& bound
    ) {
        const MTLRenderStages render_stage = stage == ShaderStage::vertex
            ? MTLRenderStageVertex
            : MTLRenderStageFragment;

        switch (bound.kind) {
        case MetalBoundKind::constant_buffer:
        case MetalBoundKind::read_buffer:
        case MetalBoundKind::write_buffer: {
            if (bound.buffer == nullptr || bound.buffer->native() == nil ||
                bound.offset > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())) {
                return false;
            }
            if (stage == ShaderStage::vertex) {
                if (index >= metal_shader_converter_vertex_buffer_base) {
                    core::log(
                        core::LogLevel::error,
                        "Metal source shader buffer binding collides with the VICE vertex-buffer namespace."
                    );
                    return false;
                }
                [render_encoder_ setVertexBuffer:bound.buffer->native()
                                          offset:static_cast<NSUInteger>(bound.offset)
                                         atIndex:index];
            } else {
                [render_encoder_ setFragmentBuffer:bound.buffer->native()
                                            offset:static_cast<NSUInteger>(bound.offset)
                                           atIndex:index];
            }
            const MTLResourceUsage usage = bound.kind == MetalBoundKind::write_buffer
                ? static_cast<MTLResourceUsage>(MTLResourceUsageRead | MTLResourceUsageWrite)
                : MTLResourceUsageRead;
            [render_encoder_ useResource:bound.buffer->native() usage:usage stages:render_stage];
            return true;
        }
        case MetalBoundKind::texture:
        case MetalBoundKind::storage_texture: {
            if (bound.texture == nullptr || bound.texture->native() == nil) {
                return false;
            }
            if (stage == ShaderStage::vertex) {
                [render_encoder_ setVertexTexture:bound.texture->native() atIndex:index];
            } else {
                [render_encoder_ setFragmentTexture:bound.texture->native() atIndex:index];
            }
            const MTLResourceUsage usage = bound.kind == MetalBoundKind::storage_texture
                ? static_cast<MTLResourceUsage>(MTLResourceUsageRead | MTLResourceUsageWrite)
                : MTLResourceUsageRead;
            [render_encoder_ useResource:bound.texture->native() usage:usage stages:render_stage];
            return true;
        }
        case MetalBoundKind::sampler:
            if (bound.sampler == nullptr || bound.sampler->native() == nil) {
                return false;
            }
            if (stage == ShaderStage::vertex) {
                [render_encoder_ setVertexSamplerState:bound.sampler->native() atIndex:index];
            } else {
                [render_encoder_ setFragmentSamplerState:bound.sampler->native() atIndex:index];
            }
            [retained_samplers_ addObject:bound.sampler->native()];
            return true;
        }
        return false;
    }

    [[nodiscard]] bool encode_stage_argument_buffer(const ShaderStage stage) {
        if (active_pipeline_ == nullptr || device_ == nil || render_encoder_ == nil) {
            return false;
        }
        const auto locations = active_pipeline_->locations(stage);
        if (locations.empty()) {
            return true;
        }

        std::uint64_t argument_buffer_size = 0;
        for (const auto& location : locations) {
            const std::uint64_t end =
                static_cast<std::uint64_t>(location.argument_buffer_offset) + location.size;
            argument_buffer_size = std::max(argument_buffer_size, end);
        }
        if (argument_buffer_size == 0U ||
            argument_buffer_size > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())) {
            return false;
        }

        id<MTLBuffer> argument_buffer = [device_
            newBufferWithLength:static_cast<NSUInteger>(argument_buffer_size)
            options:MTLResourceStorageModeShared];
        if (argument_buffer == nil || argument_buffer.contents == nullptr) {
            core::log(core::LogLevel::error, "Metal could not allocate the converted shader argument buffer.");
            return false;
        }
        std::memset(argument_buffer.contents, 0, static_cast<std::size_t>(argument_buffer_size));
        auto* bytes = static_cast<std::byte*>(argument_buffer.contents);

        const MTLRenderStages render_stage = stage == ShaderStage::vertex
            ? MTLRenderStageVertex
            : MTLRenderStageFragment;

        for (const auto& location : locations) {
            const PipelineResourceBinding* logical = logical_binding(location);
            if (logical == nullptr || logical->unbounded) {
                return false;
            }
            const std::uint64_t required =
                static_cast<std::uint64_t>(logical->count) * metal_descriptor_entry_size;
            if (required > location.size) {
                return false;
            }
            for (std::uint32_t element = 0; element < logical->count; ++element) {
                const ShaderBindingKey key{
                    .register_class = logical->register_class,
                    .binding = logical->binding,
                    .space = logical->space,
                    .array_element = element,
                };
                const MetalBoundResource* bound = bound_resource(key);
                if (bound == nullptr) {
                    return false;
                }
                const std::uint64_t descriptor_offset =
                    static_cast<std::uint64_t>(location.argument_buffer_offset) +
                    static_cast<std::uint64_t>(element) * metal_descriptor_entry_size;
                if (descriptor_offset + metal_descriptor_entry_size > argument_buffer_size) {
                    return false;
                }
                auto* entry = reinterpret_cast<MetalDescriptorTableEntry*>(
                    bytes + static_cast<std::size_t>(descriptor_offset)
                );
                if (!encode_descriptor(*entry, *bound, render_stage)) {
                    return false;
                }
            }
        }

        if (stage == ShaderStage::vertex) {
            [render_encoder_ setVertexBuffer:argument_buffer
                                      offset:0
                                     atIndex:metal_shader_converter_argument_buffer_bind_point];
        } else {
            [render_encoder_ setFragmentBuffer:argument_buffer
                                        offset:0
                                       atIndex:metal_shader_converter_argument_buffer_bind_point];
        }
        [retained_argument_buffers_ addObject:argument_buffer];
        return true;
    }

    [[nodiscard]] bool encode_descriptor(
        MetalDescriptorTableEntry& entry,
        const MetalBoundResource& bound,
        const MTLRenderStages stage
    ) {
        entry = {};
        switch (bound.kind) {
        case MetalBoundKind::constant_buffer:
        case MetalBoundKind::read_buffer:
        case MetalBoundKind::write_buffer: {
            if (bound.buffer == nullptr || bound.buffer->native() == nil ||
                bound.size == 0U || bound.size > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            const std::uint64_t gpu_address = bound.buffer->native().gpuAddress;
            if (bound.offset > std::numeric_limits<std::uint64_t>::max() - gpu_address) {
                return false;
            }
            entry.gpu_address = gpu_address + bound.offset;
            entry.metadata = static_cast<std::uint32_t>(bound.size);
            const MTLResourceUsage usage = bound.kind == MetalBoundKind::write_buffer
                ? static_cast<MTLResourceUsage>(MTLResourceUsageRead | MTLResourceUsageWrite)
                : MTLResourceUsageRead;
            [render_encoder_ useResource:bound.buffer->native() usage:usage stages:stage];
            return true;
        }
        case MetalBoundKind::texture:
        case MetalBoundKind::storage_texture: {
            if (bound.texture == nullptr || bound.texture->native() == nil) {
                return false;
            }
            entry.texture_view_id = bound.texture->native().gpuResourceID._impl;
            const MTLResourceUsage usage = bound.kind == MetalBoundKind::storage_texture
                ? static_cast<MTLResourceUsage>(MTLResourceUsageRead | MTLResourceUsageWrite)
                : MTLResourceUsageRead;
            [render_encoder_ useResource:bound.texture->native() usage:usage stages:stage];
            return true;
        }
        case MetalBoundKind::sampler:
            if (bound.sampler == nullptr || bound.sampler->native() == nil) {
                return false;
            }
            entry.gpu_address = bound.sampler->native().gpuResourceID._impl;
            [retained_samplers_ addObject:bound.sampler->native()];
            return true;
        }
        return false;
    }

    void reset_mesh_state() noexcept {
        vertex_buffers_bound_.fill(false);
        vertex_capacities_.fill(0U);
        index_buffer_ = nullptr;
        index_format_ = IndexFormat::uint16;
        index_offset_ = 0U;
        index_capacity_ = 0U;
        index_buffer_bound_ = false;
    }

    id<MTLDevice> device_ = nil;
    id<MTLCommandBuffer> command_buffer_ = nil;
    id<MTLRenderCommandEncoder> render_encoder_ = nil;
    id<CAMetalDrawable> drawable_ = nil;
    MetalGraphicsPipeline* active_pipeline_ = nullptr;
    MetalSwapchain* active_swapchain_ = nullptr;
    MetalTexture* active_depth_texture_ = nullptr;
    std::vector<MetalBoundResource> bound_resources_;
    std::array<bool, max_vertex_buffers> vertex_buffers_bound_{};
    std::array<std::uint64_t, max_vertex_buffers> vertex_capacities_{};
    MetalBuffer* index_buffer_ = nullptr;
    IndexFormat index_format_ = IndexFormat::uint16;
    std::uint64_t index_offset_ = 0U;
    std::uint64_t index_capacity_ = 0U;
    NSMutableArray<id<MTLBuffer>>* retained_argument_buffers_ = nil;
    NSMutableArray<id<MTLSamplerState>>* retained_samplers_ = nil;
    bool index_buffer_bound_ = false;
    bool pipeline_bound_ = false;
    bool present_scheduled_ = false;
};

class MetalDevice final : public Device {
public:
    MetalDevice() : device_{MTLCreateSystemDefaultDevice()} {
        if (device_ == nil) {
            return;
        }
        command_queue_ = [device_ newCommandQueue];
        if (command_queue_ == nil) {
            return;
        }
        capabilities_.max_texture_dimension_1d = 16'384;
        capabilities_.max_texture_dimension_2d = 16'384;
        capabilities_.max_texture_dimension_3d = 2'048;
        capabilities_.max_texture_array_layers = 2'048;
        if (@available(macOS 10.15, *)) {
            capabilities_.unified_memory = [device_ hasUnifiedMemory];
        }
    }

    [[nodiscard]] bool valid() const noexcept { return device_ != nil && command_queue_ != nil; }
    [[nodiscard]] Backend backend() const noexcept override { return Backend::metal; }
    [[nodiscard]] DeviceCapabilities capabilities() const noexcept override { return capabilities_; }

    [[nodiscard]] std::unique_ptr<Buffer> create_buffer(const BufferDesc& desc) override {
        if (!is_valid(desc)) {
            core::log(core::LogLevel::error, "VICE rejected an invalid buffer description.");
            return nullptr;
        }
        id<MTLBuffer> buffer = [device_ newBufferWithLength:static_cast<NSUInteger>(desc.size)
                                                   options:resource_options(desc.memory)];
        if (buffer == nil) {
            core::log(core::LogLevel::error, "Metal could not create a VICE buffer.");
            return nullptr;
        }
        if (NSString* label = make_label(desc.debug_name)) {
            [buffer setLabel:label];
        }
        return std::make_unique<MetalBuffer>(desc, buffer);
    }

    [[nodiscard]] std::unique_ptr<Texture> create_texture(const TextureDesc& desc) override {
        if (!is_valid(desc)) {
            core::log(core::LogLevel::error, "VICE rejected an invalid texture description.");
            return nullptr;
        }
        if (has_usage(desc.usage, TextureUsage::depth_attachment) &&
            (desc.dimension != TextureDimension::d2 || desc.array_layers != 1U || desc.sample_count != 1U)) {
            core::log(core::LogLevel::error, "Metal depth attachments currently require one non-MSAA 2D layer.");
            return nullptr;
        }
        const bool size_supported =
            (desc.dimension == TextureDimension::d1 &&
                desc.width <= capabilities_.max_texture_dimension_1d) ||
            (desc.dimension == TextureDimension::d2 &&
                desc.width <= capabilities_.max_texture_dimension_2d &&
                desc.height <= capabilities_.max_texture_dimension_2d &&
                desc.array_layers <= capabilities_.max_texture_array_layers) ||
            (desc.dimension == TextureDimension::d3 &&
                desc.width <= capabilities_.max_texture_dimension_3d &&
                desc.height <= capabilities_.max_texture_dimension_3d &&
                desc.depth <= capabilities_.max_texture_dimension_3d);
        if (!size_supported) {
            core::log(core::LogLevel::error, "VICE texture dimensions exceed the Metal backend limits.");
            return nullptr;
        }
        MTLTextureDescriptor* native_desc = [[MTLTextureDescriptor alloc] init];
        native_desc.textureType = texture_type(desc);
        native_desc.pixelFormat = pixel_format(desc.format);
        native_desc.width = desc.width;
        native_desc.height = desc.height;
        native_desc.depth = desc.depth;
        native_desc.mipmapLevelCount = desc.mip_levels;
        native_desc.arrayLength = desc.array_layers;
        native_desc.sampleCount = desc.sample_count;
        native_desc.storageMode = MTLStorageModePrivate;
        native_desc.usage = texture_usage(desc.usage);
        id<MTLTexture> texture = [device_ newTextureWithDescriptor:native_desc];
        if (texture == nil) {
            core::log(core::LogLevel::error, "Metal could not create a VICE texture.");
            return nullptr;
        }
        if (NSString* label = make_label(desc.debug_name)) {
            [texture setLabel:label];
        }
        return std::make_unique<MetalTexture>(desc, texture);
    }

    [[nodiscard]] std::unique_ptr<Sampler> create_sampler(const SamplerDesc& desc) override {
        if (!is_valid(desc)) {
            core::log(core::LogLevel::error, "VICE rejected an invalid sampler description.");
            return nullptr;
        }
        MTLSamplerDescriptor* native_desc = [[MTLSamplerDescriptor alloc] init];
        native_desc.minFilter = min_mag_filter(desc.min_filter);
        native_desc.magFilter = min_mag_filter(desc.mag_filter);
        native_desc.mipFilter = mip_filter(desc.mip_filter);
        native_desc.sAddressMode = address_mode(desc.address_u);
        native_desc.tAddressMode = address_mode(desc.address_v);
        native_desc.rAddressMode = address_mode(desc.address_w);
        native_desc.maxAnisotropy = desc.max_anisotropy;
        native_desc.supportArgumentBuffers = YES;
        if (NSString* label = make_label(desc.debug_name)) {
            native_desc.label = label;
        }
        id<MTLSamplerState> sampler = [device_ newSamplerStateWithDescriptor:native_desc];
        if (sampler == nil) {
            core::log(core::LogLevel::error, "Metal could not create a VICE sampler.");
            return nullptr;
        }
        return std::make_unique<MetalSampler>(sampler);
    }

    [[nodiscard]] std::unique_ptr<ShaderModule> create_shader_module(
        const ShaderModuleDesc& desc
    ) override {
        if (!is_valid(desc) || desc.language != ShaderSourceLanguage::metal) {
            core::log(core::LogLevel::error, "Metal requires a valid MSL source shader in development mode.");
            return nullptr;
        }
        NSString* source = make_label(desc.source);
        NSString* entry = make_label(desc.entry_point);
        if (source == nil || entry == nil) {
            return nullptr;
        }
        NSError* error = nil;
        MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
        id<MTLLibrary> library = [device_ newLibraryWithSource:source options:options error:&error];
        if (library == nil) {
            log_error("Metal shader compilation failed.", error);
            return nullptr;
        }
        id<MTLFunction> function = [library newFunctionWithName:entry];
        if (function == nil) {
            core::log(core::LogLevel::error, "Metal could not find the requested shader entry point.");
            return nullptr;
        }
        if (NSString* label = make_label(desc.debug_name)) {
            function.label = label;
        }
        return std::make_unique<MetalShaderModule>(
            desc.stage,
            library,
            function,
            std::vector<ShaderResourceBindingDesc>{desc.resources.begin(), desc.resources.end()}
        );
    }

    [[nodiscard]] std::unique_ptr<ShaderModule> create_shader_module(
        const ShaderBinaryDesc& desc
    ) override {
        if (!is_valid(desc) || desc.format != ShaderBinaryFormat::metallib) {
            core::log(core::LogLevel::error, "Metal requires a valid metallib shader binary.");
            return nullptr;
        }
        std::string layout_diagnostics;
        if (!validate_metal_shader_layout(
                desc.resources,
                desc.metal_resource_locations,
                layout_diagnostics
            )) {
            core::log(
                core::LogLevel::error,
                "Metal rejected cooked resource locations. " + layout_diagnostics
            );
            return nullptr;
        }

        NSString* entry = make_label(desc.entry_point);
        if (entry == nil) {
            return nullptr;
        }

        dispatch_data_t data = dispatch_data_create(
            desc.binary.data(),
            desc.binary.size(),
            nullptr,
            DISPATCH_DATA_DESTRUCTOR_DEFAULT
        );
        if (data == nullptr) {
            core::log(core::LogLevel::error, "Metal could not copy the cooked shader binary.");
            return nullptr;
        }

        NSError* error = nil;
        id<MTLLibrary> library = [device_ newLibraryWithData:data error:&error];
        if (library == nil) {
            log_error("Metal could not load the cooked metallib.", error);
            return nullptr;
        }

        id<MTLFunction> function = [library newFunctionWithName:entry];
        if (function == nil) {
            core::log(core::LogLevel::error, "Metal could not find the requested cooked shader entry point.");
            return nullptr;
        }
        if (NSString* label = make_label(desc.debug_name)) {
            function.label = label;
        }
        return std::make_unique<MetalShaderModule>(
            desc.stage,
            library,
            function,
            std::vector<ShaderResourceBindingDesc>{desc.resources.begin(), desc.resources.end()},
            std::vector<MetalShaderResourceLocationDesc>{
                desc.metal_resource_locations.begin(),
                desc.metal_resource_locations.end()
            }
        );
    }

    [[nodiscard]] std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDesc& desc
    ) override {
        if (!is_valid(desc) || desc.vertex_shader->stage() != ShaderStage::vertex ||
            desc.fragment_shader->stage() != ShaderStage::fragment) {
            core::log(core::LogLevel::error, "VICE rejected an invalid graphics pipeline description.");
            return nullptr;
        }
        auto* vertex = dynamic_cast<MetalShaderModule*>(desc.vertex_shader);
        auto* fragment = dynamic_cast<MetalShaderModule*>(desc.fragment_shader);
        if (vertex == nullptr || fragment == nullptr) {
            return nullptr;
        }

        PipelineLayout layout = compile_graphics_pipeline_layout(
            vertex->resources(),
            fragment->resources()
        );
        if (!layout.valid) {
            core::log(
                core::LogLevel::error,
                "VICE rejected the graphics pipeline resource layout. " + layout.diagnostics
            );
            return nullptr;
        }
        if (std::any_of(
                layout.resources.begin(),
                layout.resources.end(),
                [](const PipelineResourceBinding& resource) { return resource.unbounded; }
            )) {
            core::log(
                core::LogLevel::error,
                "Metal automatic resource layout cannot create a pipeline with unbounded bindings."
            );
            return nullptr;
        }

        MTLVertexDescriptor* vertex_descriptor = nil;
        std::vector<MetalVertexStreamInfo> vertex_streams;
        vertex_streams.reserve(desc.vertex_buffers.size());
        if (!desc.vertex_buffers.empty()) {
            vertex_descriptor = [[MTLVertexDescriptor alloc] init];
            for (std::size_t slot = 0; slot < desc.vertex_buffers.size(); ++slot) {
                const auto& stream = desc.vertex_buffers[slot];
                const NSUInteger buffer_index = metal_shader_converter_vertex_buffer_base + slot;
                if (buffer_index >= 31U) {
                    return nullptr;
                }
                auto* native_layout = vertex_descriptor.layouts[buffer_index];
                native_layout.stride = stream.stride;
                native_layout.stepFunction = stream.step_mode == VertexStepMode::per_instance
                    ? MTLVertexStepFunctionPerInstance
                    : MTLVertexStepFunctionPerVertex;
                native_layout.stepRate = stream.step_rate;
                vertex_streams.push_back(MetalVertexStreamInfo{
                    .stride = stream.stride,
                    .step_mode = stream.step_mode,
                    .step_rate = stream.step_rate,
                });

                for (const auto& attribute : stream.attributes) {
                    const NSUInteger attribute_index =
                        metal_shader_converter_vertex_attribute_base + attribute.shader_location;
                    const MTLVertexFormat format = vertex_format(attribute.format);
                    if (attribute_index >= 31U || format == MTLVertexFormatInvalid) {
                        return nullptr;
                    }
                    auto* native_attribute = vertex_descriptor.attributes[attribute_index];
                    native_attribute.format = format;
                    native_attribute.offset = attribute.offset;
                    native_attribute.bufferIndex = buffer_index;
                }
            }
        }

        MTLRenderPipelineDescriptor* native_desc = [[MTLRenderPipelineDescriptor alloc] init];
        native_desc.vertexFunction = vertex->function();
        native_desc.fragmentFunction = fragment->function();
        native_desc.vertexDescriptor = vertex_descriptor;
        auto* color_attachment = native_desc.colorAttachments[0];
        color_attachment.pixelFormat = pixel_format(desc.color_format);
        if (desc.blend == BlendMode::alpha) {
            color_attachment.blendingEnabled = YES;
            color_attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            color_attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            color_attachment.rgbBlendOperation = MTLBlendOperationAdd;
            color_attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
            color_attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            color_attachment.alphaBlendOperation = MTLBlendOperationAdd;
        }
        native_desc.depthAttachmentPixelFormat = pixel_format(desc.depth.format);
        if (NSString* label = make_label(desc.debug_name)) {
            native_desc.label = label;
        }
        NSError* error = nil;
        id<MTLRenderPipelineState> state = [device_ newRenderPipelineStateWithDescriptor:native_desc error:&error];
        if (state == nil) {
            log_error("Metal graphics pipeline creation failed.", error);
            return nullptr;
        }

        id<MTLDepthStencilState> depth_state = nil;
        if (desc.depth.format != TextureFormat::undefined) {
            MTLDepthStencilDescriptor* depth_desc = [[MTLDepthStencilDescriptor alloc] init];
            depth_desc.depthCompareFunction = desc.depth.test_enabled
                ? comparison_func(desc.depth.compare)
                : MTLCompareFunctionAlways;
            depth_desc.depthWriteEnabled = desc.depth.write_enabled;
            depth_state = [device_ newDepthStencilStateWithDescriptor:depth_desc];
            if (depth_state == nil) {
                core::log(core::LogLevel::error, "Metal could not create the VICE depth-stencil state.");
                return nullptr;
            }
        }

        return std::make_unique<MetalGraphicsPipeline>(
            state,
            depth_state,
            desc.topology,
            desc.cull_mode,
            desc.front_face,
            desc.depth,
            std::move(layout),
            std::vector<MetalShaderResourceLocationDesc>{
                vertex->locations().begin(),
                vertex->locations().end()
            },
            std::vector<MetalShaderResourceLocationDesc>{
                fragment->locations().begin(),
                fragment->locations().end()
            },
            std::move(vertex_streams)
        );
    }

    [[nodiscard]] std::unique_ptr<CommandList> create_command_list() override {
        id<MTLCommandBuffer> command_buffer = [command_queue_ commandBuffer];
        if (command_buffer == nil) {
            core::log(core::LogLevel::error, "Metal could not create a VICE command buffer.");
            return nullptr;
        }
        return std::make_unique<MetalCommandList>(device_, command_buffer);
    }

    SubmissionId submit(std::unique_ptr<CommandList> command_list) override {
        auto* metal_commands = dynamic_cast<MetalCommandList*>(command_list.get());
        if (metal_commands == nullptr || !metal_commands->valid()) {
            return invalid_submission;
        }
        id<MTLCommandBuffer> command_buffer = metal_commands->native();
        [command_buffer commit];
        [command_buffer waitUntilCompleted];
        if ([command_buffer status] == MTLCommandBufferStatusError) {
            core::log(core::LogLevel::error, "Metal command submission failed.");
            return invalid_submission;
        }
        completed_submission_ = ++submitted_submission_;
        return completed_submission_;
    }

    [[nodiscard]] SubmissionId completed_submission() const noexcept override {
        return completed_submission_;
    }

    bool wait_for_submission(const SubmissionId submission) override {
        return submission != invalid_submission && submission <= completed_submission_;
    }

    bool wait_idle() override {
        id<MTLCommandBuffer> command_buffer = [command_queue_ commandBuffer];
        if (command_buffer == nil) {
            return false;
        }
        [command_buffer commit];
        [command_buffer waitUntilCompleted];
        return [command_buffer status] == MTLCommandBufferStatusCompleted;
    }

    [[nodiscard]] std::unique_ptr<Swapchain> create_swapchain(platform::Window& window) override {
        auto swapchain = std::make_unique<MetalSwapchain>(window, device_, command_queue_);
        if (!swapchain->valid()) {
            core::log(core::LogLevel::error, "Metal could not create a VICE swapchain.");
            return nullptr;
        }
        return swapchain;
    }

private:
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> command_queue_ = nil;
    DeviceCapabilities capabilities_{};
    SubmissionId submitted_submission_ = invalid_submission;
    SubmissionId completed_submission_ = invalid_submission;
};

} // namespace

std::unique_ptr<Device> Device::create() {
    @autoreleasepool {
        auto device = std::make_unique<MetalDevice>();
        if (!device->valid()) {
            core::log(core::LogLevel::error, "VICE could not create a Metal device.");
            return nullptr;
        }
        return device;
    }
}

} // namespace vice::render
