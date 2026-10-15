#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <dispatch/dispatch.h>

#include <vice/core/log.hpp>
#include <vice/platform/native_window.hpp>
#include <vice/render/pipeline_layout.hpp>
#include <vice/render/rhi.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vice::render {
namespace {

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

private:
    TextureDesc desc_{};
    id<MTLTexture> texture_ = nil;
};

class MetalSampler final : public Sampler {
public:
    explicit MetalSampler(id<MTLSamplerState> sampler) : sampler_{sampler} {}

private:
    id<MTLSamplerState> sampler_ = nil;
};

class MetalShaderModule final : public ShaderModule {
public:
    MetalShaderModule(
        const ShaderStage stage,
        id<MTLLibrary> library,
        id<MTLFunction> function,
        std::vector<ShaderResourceBindingDesc> resources = {}
    ) : stage_{stage}, library_{library}, function_{function}, resources_{std::move(resources)} {}

    [[nodiscard]] ShaderStage stage() const noexcept override { return stage_; }
    [[nodiscard]] std::span<const ShaderResourceBindingDesc> resources() const noexcept override {
        return resources_;
    }
    [[nodiscard]] id<MTLFunction> function() const noexcept { return function_; }

private:
    ShaderStage stage_ = ShaderStage::vertex;
    id<MTLLibrary> library_ = nil;
    id<MTLFunction> function_ = nil;
    std::vector<ShaderResourceBindingDesc> resources_;
};

class MetalGraphicsPipeline final : public GraphicsPipeline {
public:
    MetalGraphicsPipeline(
        id<MTLRenderPipelineState> state,
        const PrimitiveTopology topology,
        const CullMode cull,
        const FrontFace front,
        PipelineLayout layout
    ) : state_{state}, topology_{topology}, cull_{cull}, front_{front}, layout_{std::move(layout)} {}

    [[nodiscard]] PrimitiveTopology topology() const noexcept override { return topology_; }
    [[nodiscard]] id<MTLRenderPipelineState> native() const noexcept { return state_; }
    [[nodiscard]] CullMode cull() const noexcept { return cull_; }
    [[nodiscard]] FrontFace front() const noexcept { return front_; }
    [[nodiscard]] bool has_resources() const noexcept { return !layout_.resources.empty(); }

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
                return resource.unbounded || key.array_element < resource.count;
            }
        );
        return found != layout_.resources.end();
    }

private:
    id<MTLRenderPipelineState> state_ = nil;
    PrimitiveTopology topology_ = PrimitiveTopology::triangle_list;
    CullMode cull_ = CullMode::back;
    FrontFace front_ = FrontFace::counter_clockwise;
    PipelineLayout layout_;
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

class MetalCommandList final : public CommandList {
public:
    explicit MetalCommandList(id<MTLCommandBuffer> command_buffer)
        : command_buffer_{command_buffer} {}

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
        render_encoder_ = [command_buffer_ renderCommandEncoderWithDescriptor:pass];
        active_swapchain_ = metal_swapchain;
        active_pipeline_ = nullptr;
        pipeline_bound_ = false;
        return render_encoder_ != nil;
    }

    bool set_graphics_pipeline(GraphicsPipeline& pipeline) override {
        if (render_encoder_ == nil) {
            return false;
        }
        auto* metal_pipeline = dynamic_cast<MetalGraphicsPipeline*>(&pipeline);
        if (metal_pipeline == nullptr || metal_pipeline->native() == nil) {
            return false;
        }
        [render_encoder_ setRenderPipelineState:metal_pipeline->native()];
        [render_encoder_ setCullMode:cull_mode(metal_pipeline->cull())];
        [render_encoder_ setFrontFacingWinding:front_winding(metal_pipeline->front())];
        active_pipeline_ = metal_pipeline;
        pipeline_bound_ = true;
        return true;
    }

    bool set_graphics_constant_buffer(
        const ShaderBindingKey& binding,
        Buffer& buffer,
        const std::uint64_t offset,
        const std::uint64_t size
    ) override {
        if (!binding_allowed(binding, ShaderRegisterClass::constant_buffer) ||
            dynamic_cast<MetalBuffer*>(&buffer) == nullptr ||
            !has_usage(buffer.usage(), BufferUsage::uniform) || offset > buffer.size() ||
            (size != 0U && size > buffer.size() - offset)) {
            return false;
        }
        has_validated_binding_ = true;
        return true;
    }

    bool set_graphics_buffer(
        const ShaderBindingKey& binding,
        Buffer& buffer,
        const BufferViewDesc& view
    ) override {
        if (!binding_allowed(binding, ShaderRegisterClass::shader_resource) ||
            dynamic_cast<MetalBuffer*>(&buffer) == nullptr ||
            !has_usage(buffer.usage(), BufferUsage::storage) || !valid_buffer_view(buffer, view)) {
            return false;
        }
        has_validated_binding_ = true;
        return true;
    }

    bool set_graphics_storage_buffer(
        const ShaderBindingKey& binding,
        Buffer& buffer,
        const BufferViewDesc& view
    ) override {
        if (!binding_allowed(binding, ShaderRegisterClass::unordered_access) ||
            dynamic_cast<MetalBuffer*>(&buffer) == nullptr ||
            !has_usage(buffer.usage(), BufferUsage::storage) || !valid_buffer_view(buffer, view)) {
            return false;
        }
        has_validated_binding_ = true;
        return true;
    }

    bool set_graphics_texture(const ShaderBindingKey& binding, Texture& texture) override {
        if (!binding_allowed(binding, ShaderRegisterClass::shader_resource) ||
            dynamic_cast<MetalTexture*>(&texture) == nullptr ||
            !has_usage(texture.usage(), TextureUsage::sampled)) {
            return false;
        }
        has_validated_binding_ = true;
        return true;
    }

    bool set_graphics_storage_texture(
        const ShaderBindingKey& binding,
        Texture& texture
    ) override {
        if (!binding_allowed(binding, ShaderRegisterClass::unordered_access) ||
            dynamic_cast<MetalTexture*>(&texture) == nullptr ||
            !has_usage(texture.usage(), TextureUsage::storage)) {
            return false;
        }
        has_validated_binding_ = true;
        return true;
    }

    bool set_graphics_sampler(const ShaderBindingKey& binding, Sampler& sampler) override {
        if (!binding_allowed(binding, ShaderRegisterClass::sampler) ||
            dynamic_cast<MetalSampler*>(&sampler) == nullptr) {
            return false;
        }
        has_validated_binding_ = true;
        return true;
    }

    bool draw(
        const std::uint32_t vertex_count,
        const std::uint32_t instance_count,
        const std::uint32_t first_vertex,
        const std::uint32_t first_instance
    ) override {
        if (render_encoder_ == nil || !pipeline_bound_ || active_pipeline_ == nullptr ||
            vertex_count == 0 || instance_count == 0) {
            return false;
        }
        if (active_pipeline_->has_resources()) {
            core::log(
                core::LogLevel::error,
                "Metal resource binding requires Metal Shader Converter argument-buffer location metadata."
            );
            return false;
        }
        [render_encoder_ drawPrimitives:MTLPrimitiveTypeTriangle
                            vertexStart:first_vertex
                            vertexCount:vertex_count
                          instanceCount:instance_count
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
        has_validated_binding_ = false;
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

    [[nodiscard]] static bool valid_buffer_view(
        const Buffer& buffer,
        const BufferViewDesc& view
    ) noexcept {
        if (!is_valid(view) || view.offset > buffer.size()) {
            return false;
        }
        const std::uint64_t size = view.size == 0U ? buffer.size() - view.offset : view.size;
        if (size == 0U || size > buffer.size() - view.offset) {
            return false;
        }
        if (view.structure_stride == 0U) {
            return view.offset % 4U == 0U && size % 4U == 0U;
        }
        return view.offset % view.structure_stride == 0U && size % view.structure_stride == 0U;
    }

    id<MTLCommandBuffer> command_buffer_ = nil;
    id<MTLRenderCommandEncoder> render_encoder_ = nil;
    id<CAMetalDrawable> drawable_ = nil;
    MetalGraphicsPipeline* active_pipeline_ = nullptr;
    MetalSwapchain* active_swapchain_ = nullptr;
    bool has_validated_binding_ = false;
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
        return std::make_unique<MetalShaderModule>(desc.stage, library, function);
    }

    [[nodiscard]] std::unique_ptr<ShaderModule> create_shader_module(
        const ShaderBinaryDesc& desc
    ) override {
        if (!is_valid(desc) || desc.format != ShaderBinaryFormat::metallib) {
            core::log(core::LogLevel::error, "Metal requires a valid metallib shader binary.");
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
            std::vector<ShaderResourceBindingDesc>{desc.resources.begin(), desc.resources.end()}
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

        MTLRenderPipelineDescriptor* native_desc = [[MTLRenderPipelineDescriptor alloc] init];
        native_desc.vertexFunction = vertex->function();
        native_desc.fragmentFunction = fragment->function();
        native_desc.colorAttachments[0].pixelFormat = pixel_format(desc.color_format);
        if (NSString* label = make_label(desc.debug_name)) {
            native_desc.label = label;
        }
        NSError* error = nil;
        id<MTLRenderPipelineState> state = [device_ newRenderPipelineStateWithDescriptor:native_desc error:&error];
        if (state == nil) {
            log_error("Metal graphics pipeline creation failed.", error);
            return nullptr;
        }
        return std::make_unique<MetalGraphicsPipeline>(
            state,
            desc.topology,
            desc.cull_mode,
            desc.front_face,
            std::move(layout)
        );
    }

    [[nodiscard]] std::unique_ptr<CommandList> create_command_list() override {
        id<MTLCommandBuffer> command_buffer = [command_queue_ commandBuffer];
        if (command_buffer == nil) {
            core::log(core::LogLevel::error, "Metal could not create a VICE command buffer.");
            return nullptr;
        }
        return std::make_unique<MetalCommandList>(command_buffer);
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
