#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <vice/core/log.hpp>
#include <vice/platform/native_window.hpp>
#include <vice/render/rhi.hpp>

#include <algorithm>
#include <memory>

namespace vice::render {
namespace {

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
        if (device_ != nil) {
            command_queue_ = [device_ newCommandQueue];
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return device_ != nil && command_queue_ != nil;
    }

    [[nodiscard]] Backend backend() const noexcept override {
        return Backend::metal;
    }

    [[nodiscard]] DeviceCapabilities capabilities() const noexcept override {
        return DeviceCapabilities{};
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
