#include <vice/core/log.hpp>
#include <vice/platform/native_window.hpp>
#include <vice/render/rhi.hpp>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <windows.h>

#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <utility>

namespace vice::render {
namespace {

using Microsoft::WRL::ComPtr;
constexpr UINT back_buffer_count = 2;

[[nodiscard]] bool succeeded(const HRESULT result, const char* message) {
    if (SUCCEEDED(result)) {
        return true;
    }
    core::log(core::LogLevel::error, message);
    return false;
}

class D3D12Swapchain final : public Swapchain {
public:
    D3D12Swapchain(
        platform::Window& window,
        ComPtr<ID3D12Device> device,
        ComPtr<ID3D12CommandQueue> command_queue
    ) : window_{&window}, device_{std::move(device)}, command_queue_{std::move(command_queue)} {
        const auto native = platform::NativeWindowAccess::get(window);
        if (native.system != platform::NativeWindowSystem::win32 || native.window == nullptr) {
            return;
        }

        window_handle_ = static_cast<HWND>(native.window);
        valid_ = initialize(window.metrics());
    }

    ~D3D12Swapchain() override {
        wait_for_gpu();
        if (fence_event_ != nullptr) {
            CloseHandle(fence_event_);
            fence_event_ = nullptr;
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return valid_;
    }

    [[nodiscard]] platform::WindowId window_id() const noexcept override {
        return window_ != nullptr ? window_->id() : platform::invalid_window_id;
    }

    bool resize(const platform::WindowMetrics metrics) override {
        if (!valid_ || swapchain_ == nullptr) {
            return false;
        }
        if (metrics.drawable_width == 0U || metrics.drawable_height == 0U) {
            return true;
        }

        if (!wait_for_gpu()) {
            return false;
        }

        for (auto& back_buffer : back_buffers_) {
            back_buffer.Reset();
        }

        if (!succeeded(
            swapchain_->ResizeBuffers(
                back_buffer_count,
                metrics.drawable_width,
                metrics.drawable_height,
                DXGI_FORMAT_B8G8R8A8_UNORM,
                0
            ),
            "Direct3D 12 could not resize the VICE swapchain."
        )) {
            return false;
        }

        return create_render_targets();
    }

    bool clear_and_present(const Color color) override {
        if (!valid_ || swapchain_ == nullptr) {
            return false;
        }

        const UINT frame_index = swapchain_->GetCurrentBackBufferIndex();
        if (frame_index >= back_buffer_count) {
            return false;
        }

        if (!succeeded(allocator_->Reset(), "Direct3D 12 could not reset the command allocator.")) {
            return false;
        }
        if (!succeeded(
            command_list_->Reset(allocator_.Get(), nullptr),
            "Direct3D 12 could not reset the command list."
        )) {
            return false;
        }

        D3D12_RESOURCE_BARRIER to_render_target{};
        to_render_target.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_render_target.Transition.pResource = back_buffers_[frame_index].Get();
        to_render_target.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        to_render_target.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        to_render_target.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        command_list_->ResourceBarrier(1, &to_render_target);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(frame_index) * static_cast<SIZE_T>(rtv_descriptor_size_);

        const FLOAT clear_color[4] = {color.red, color.green, color.blue, color.alpha};
        command_list_->ClearRenderTargetView(rtv, clear_color, 0, nullptr);

        D3D12_RESOURCE_BARRIER to_present = to_render_target;
        to_present.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        to_present.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        command_list_->ResourceBarrier(1, &to_present);

        if (!succeeded(command_list_->Close(), "Direct3D 12 could not close the command list.")) {
            return false;
        }

        ID3D12CommandList* command_lists[] = {command_list_.Get()};
        command_queue_->ExecuteCommandLists(1, command_lists);

        if (!succeeded(swapchain_->Present(1, 0), "Direct3D 12 could not present the VICE swapchain.")) {
            return false;
        }

        return wait_for_gpu();
    }

private:
    bool initialize(const platform::WindowMetrics metrics) {
        ComPtr<IDXGIFactory4> factory;
        if (!succeeded(
            CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)),
            "Direct3D 12 could not create a DXGI factory."
        )) {
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 swapchain_desc{};
        swapchain_desc.Width = std::max<UINT>(1U, metrics.drawable_width);
        swapchain_desc.Height = std::max<UINT>(1U, metrics.drawable_height);
        swapchain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapchain_desc.SampleDesc.Count = 1;
        swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchain_desc.BufferCount = back_buffer_count;
        swapchain_desc.Scaling = DXGI_SCALING_STRETCH;
        swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        ComPtr<IDXGISwapChain1> swapchain;
        if (!succeeded(
            factory->CreateSwapChainForHwnd(
                command_queue_.Get(),
                window_handle_,
                &swapchain_desc,
                nullptr,
                nullptr,
                &swapchain
            ),
            "Direct3D 12 could not create the VICE swapchain."
        )) {
            return false;
        }
        factory->MakeWindowAssociation(window_handle_, DXGI_MWA_NO_ALT_ENTER);

        if (!succeeded(
            swapchain.As(&swapchain_),
            "Direct3D 12 could not access IDXGISwapChain3."
        )) {
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
        rtv_heap_desc.NumDescriptors = back_buffer_count;
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (!succeeded(
            device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap_)),
            "Direct3D 12 could not create the RTV heap."
        )) {
            return false;
        }
        rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        if (!create_render_targets()) {
            return false;
        }

        if (!succeeded(
            device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)),
            "Direct3D 12 could not create a command allocator."
        )) {
            return false;
        }
        if (!succeeded(
            device_->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator_.Get(),
                nullptr,
                IID_PPV_ARGS(&command_list_)
            ),
            "Direct3D 12 could not create a command list."
        )) {
            return false;
        }
        if (!succeeded(command_list_->Close(), "Direct3D 12 could not close the initial command list.")) {
            return false;
        }

        if (!succeeded(
            device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
            "Direct3D 12 could not create a fence."
        )) {
            return false;
        }

        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (fence_event_ == nullptr) {
            core::log(core::LogLevel::error, "Direct3D 12 could not create a fence event.");
            return false;
        }

        return true;
    }

    bool create_render_targets() {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        for (UINT index = 0; index < back_buffer_count; ++index) {
            if (!succeeded(
                swapchain_->GetBuffer(index, IID_PPV_ARGS(&back_buffers_[index])),
                "Direct3D 12 could not acquire a swapchain buffer."
            )) {
                return false;
            }
            device_->CreateRenderTargetView(back_buffers_[index].Get(), nullptr, handle);
            handle.ptr += static_cast<SIZE_T>(rtv_descriptor_size_);
        }
        return true;
    }

    bool wait_for_gpu() noexcept {
        if (command_queue_ == nullptr || fence_ == nullptr || fence_event_ == nullptr) {
            return true;
        }

        const std::uint64_t value = ++fence_value_;
        if (FAILED(command_queue_->Signal(fence_.Get(), value))) {
            return false;
        }
        if (fence_->GetCompletedValue() >= value) {
            return true;
        }
        if (FAILED(fence_->SetEventOnCompletion(value, fence_event_))) {
            return false;
        }
        return WaitForSingleObject(fence_event_, INFINITE) == WAIT_OBJECT_0;
    }

    platform::Window* window_ = nullptr;
    HWND window_handle_ = nullptr;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> command_queue_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    std::array<ComPtr<ID3D12Resource>, back_buffer_count> back_buffers_{};
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> command_list_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fence_event_ = nullptr;
    UINT rtv_descriptor_size_ = 0;
    std::uint64_t fence_value_ = 0;
    bool valid_ = false;
};

class D3D12Device final : public Device {
public:
    D3D12Device() {
        if (!succeeded(
            D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)),
            "VICE could not create a Direct3D 12 device."
        )) {
            return;
        }

        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        if (!succeeded(
            device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)),
            "VICE could not create a Direct3D 12 command queue."
        )) {
            return;
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return device_ != nullptr && command_queue_ != nullptr;
    }

    [[nodiscard]] Backend backend() const noexcept override {
        return Backend::direct3d12;
    }

    [[nodiscard]] DeviceCapabilities capabilities() const noexcept override {
        return DeviceCapabilities{};
    }

    [[nodiscard]] std::unique_ptr<Swapchain> create_swapchain(platform::Window& window) override {
        auto swapchain = std::make_unique<D3D12Swapchain>(window, device_, command_queue_);
        if (!swapchain->valid()) {
            core::log(core::LogLevel::error, "Direct3D 12 could not create a VICE swapchain.");
            return nullptr;
        }
        return swapchain;
    }

private:
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> command_queue_;
};

} // namespace

std::unique_ptr<Device> Device::create() {
    auto device = std::make_unique<D3D12Device>();
    if (!device->valid()) {
        return nullptr;
    }
    return device;
}

} // namespace vice::render
