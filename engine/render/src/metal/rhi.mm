#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <vice/core/log.hpp>
#include <vice/platform/native_window.hpp>
#include <vice/render/rhi.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string_view>

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
    case TextureFormat::rgba8_unorm:
        return MTLPixelFormatRGBA8Unorm;
    case TextureFormat::bgra8_unorm:
        return MTLPixelFormatBGRA8Unorm;
    case TextureFormat::rgba16_float:
        return MTLPixelFormatRGBA16Float;
    case TextureFormat::r32_float:
        return MTLPixelFormatR32Float;
    case TextureFormat::depth32_float:
        return MTLPixelFormatDepth32Float;
    case TextureFormat::undefined:
        return MTLPixelFormatInvalid;
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
    case AddressMode::clamp_to_edge:
        return MTLSamplerAddressModeClampToEdge;
    case AddressMode::repeat:
        return MTLSamplerAddressModeRepeat;
    case AddressMode::mirrored_repeat:
        return MTLSamplerAddressModeMirrorRepeat;
    }
    return MTLSamplerAddressModeClampToEdge;
}

class MetalBuffer final : public Buffer {
public:
    MetalBuffer(const BufferDesc& desc, id<MTLBuffer> buffer)
        : size_{desc.size}, usage_{desc.usage}, memory_{desc.memory}, buffer_{buffer} {}

    [[nodiscard]] bool valid() const noexcept {
        return buffer_ != nil;
    }

    [[nodiscard]] std::uint64_t size() const noexcept override {
        return size_;
    }

    [[nodiscard]] BufferUsage usage() const noexcept override {
        return usage_;
    }

    [[nodiscard]] MemoryLocation memory_location() const noexcept override {
        return memory_;
    }

    bool write(const std::uint64_t offset, const std::span<const std::byte> data) override {
        if (memory_ != MemoryLocation::cpu_to_gpu || buffer_ == nil) {
            return false;
        }
        if (offset > size_ || data.size() > size_ - offset) {
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
        if (memory_ != MemoryLocation::gpu_to_cpu || buffer_ == nil) {
            return false;
        }
        if (offset > size_ || data.size() > size_ - offset) {
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

    [[nodiscard]] id<MTLBuffer> native() const noexcept {
        return buffer_;
    }

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

    [[nodiscard]] bool valid() const noexcept {
        return texture_ != nil;
    }

    [[nodiscard]] TextureDimension dimension() const noexcept override {
        return desc_.dimension;
    }

    [[nodiscard]] TextureFormat format() const noexcept override {
        return desc_.format;
    }

    [[nodiscard]] TextureUsage usage() const noexcept override {
        return desc_.usage;
    }

    [[nodiscard]] std::uint32_t width() const noexcept override {
        return desc_.width;
    }

    [[nodiscard]] std::uint32_t height() const noexcept override {
        return desc_.height;
    }

    [[nodiscard]] std::uint32_t depth() const noexcept override {
        return desc_.depth;
    }

private:
    TextureDesc desc_{};
    id<MTLTexture> texture_ = nil;
};

class MetalSampler final : public Sampler {
public:
    explicit MetalSampler(id<MTLSamplerState> sampler) : sampler_{sampler} {}

    [[nodiscard]] bool valid() const noexcept {
        return sampler_ != nil;
    }

private:
    id<MTLSamplerState> sampler_ = nil;
};

class MetalCommandList final : public CommandList {
public:
    explicit MetalCommandList(id<MTLCommandBuffer> command_buffer)
        : command_buffer_{command_buffer} {}

    [[nodiscard]] bool valid() const noexcept {
        return command_buffer_ != nil;
    }

    bool copy_buffer(
        Buffer& source,
        const std::uint64_t source_offset,
        Buffer& destination,
        const std::uint64_t destination_offset,
        const std::uint64_t size
    ) override {
        auto* source_buffer = dynamic_cast<MetalBuffer*>(&source);
        auto* destination_buffer = dynamic_cast<MetalBuffer*>(&destination);
        if (source_buffer == nullptr || destination_buffer == nullptr || command_buffer_ == nil) {
            return false;
        }
        if (!has_usage(source.usage(), BufferUsage::copy_source) ||
            !has_usage(destination.usage(), BufferUsage::copy_destination)) {
            return false;
        }
        if (source_offset > source.size() || size > source.size() - source_offset ||
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

    [[nodiscard]] id<MTLCommandBuffer> native() const noexcept {
        return command_buffer_;
    }

private:
    id<MTLCommandBuffer> command_buffer_ = nil;
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

    [[nodiscard]] bool valid() const noexcept {
        return valid_;
    }

    [[nodiscard]] platform::WindowId window_id() const noexcept override {
        return window_ != nullptr ? window_->id() : platform::invalid_window_id;
    }

    bool resize(const platform::WindowMetrics metrics) override {
        if (layer_ == nil) {
            return false;
        }

        const auto drawable_width = std::max<std::uint32_t>(1U, metrics.drawable_width);
        const auto drawable_height = std::max<std::uint32_t>(1U, metrics.drawable_height);
        const CGFloat scale = metrics.scale_factor > 0.0F
            ? static_cast<CGFloat>(metrics.scale_factor)
            : 1.0;

        [layer_ setContentsScale:scale];
        [layer_ setDrawableSize:CGSizeMake(
            static_cast<CGFloat>(drawable_width),
            static_cast<CGFloat>(drawable_height)
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
                static_cast<double>(color.red),
                static_cast<double>(color.green),
                static_cast<double>(color.blue),
                static_cast<double>(color.alpha)
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

private:
    platform::Window* window_ = nullptr;
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> command_queue_ = nil;
    NSView* view_ = nil;
    CAMetalLayer* layer_ = nil;
    bool valid_ = false;
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

    [[nodiscard]] bool valid() const noexcept {
        return device_ != nil && command_queue_ != nil;
    }

    [[nodiscard]] Backend backend() const noexcept override {
        return Backend::metal;
    }

    [[nodiscard]] DeviceCapabilities capabilities() const noexcept override {
        return capabilities_;
    }

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
