#include <vice/core/log.hpp>
#include <vice/platform/native_window.hpp>
#include <vice/render/rhi.hpp>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <windows.h>

#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif

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

[[nodiscard]] std::wstring wide_name(const std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        required
    );
    if (converted != required) {
        return {};
    }
    return result;
}

[[nodiscard]] D3D12_HEAP_TYPE heap_type(const MemoryLocation memory) noexcept {
    switch (memory) {
    case MemoryLocation::gpu_only: return D3D12_HEAP_TYPE_DEFAULT;
    case MemoryLocation::cpu_to_gpu: return D3D12_HEAP_TYPE_UPLOAD;
    case MemoryLocation::gpu_to_cpu: return D3D12_HEAP_TYPE_READBACK;
    }
    return D3D12_HEAP_TYPE_DEFAULT;
}

[[nodiscard]] D3D12_RESOURCE_STATES initial_buffer_state(const MemoryLocation memory) noexcept {
    switch (memory) {
    case MemoryLocation::gpu_only: return D3D12_RESOURCE_STATE_COMMON;
    case MemoryLocation::cpu_to_gpu: return D3D12_RESOURCE_STATE_GENERIC_READ;
    case MemoryLocation::gpu_to_cpu: return D3D12_RESOURCE_STATE_COPY_DEST;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

[[nodiscard]] D3D12_RESOURCE_FLAGS buffer_flags(const BufferUsage usage) noexcept {
    return has_usage(usage, BufferUsage::storage)
        ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
        : D3D12_RESOURCE_FLAG_NONE;
}

[[nodiscard]] DXGI_FORMAT pixel_format(const TextureFormat format) noexcept {
    switch (format) {
    case TextureFormat::rgba8_unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::bgra8_unorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::rgba16_float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case TextureFormat::r32_float: return DXGI_FORMAT_R32_FLOAT;
    case TextureFormat::depth32_float: return DXGI_FORMAT_D32_FLOAT;
    case TextureFormat::undefined: return DXGI_FORMAT_UNKNOWN;
    }
    return DXGI_FORMAT_UNKNOWN;
}

[[nodiscard]] D3D12_RESOURCE_DIMENSION resource_dimension(
    const TextureDimension dimension
) noexcept {
    switch (dimension) {
    case TextureDimension::d1: return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    case TextureDimension::d2: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    case TextureDimension::d3: return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    }
    return D3D12_RESOURCE_DIMENSION_UNKNOWN;
}

[[nodiscard]] D3D12_RESOURCE_FLAGS texture_flags(const TextureUsage usage) noexcept {
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (has_usage(usage, TextureUsage::storage)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    if (has_usage(usage, TextureUsage::color_attachment)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (has_usage(usage, TextureUsage::depth_attachment)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    return flags;
}

[[nodiscard]] D3D12_TEXTURE_ADDRESS_MODE address_mode(const AddressMode mode) noexcept {
    switch (mode) {
    case AddressMode::clamp_to_edge: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case AddressMode::repeat: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case AddressMode::mirrored_repeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    }
    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
}

[[nodiscard]] D3D12_FILTER sampler_filter(const SamplerDesc& desc) noexcept {
    if (desc.max_anisotropy > 1) {
        return D3D12_FILTER_ANISOTROPIC;
    }
    const bool min_linear = desc.min_filter == Filter::linear;
    const bool mag_linear = desc.mag_filter == Filter::linear;
    const bool mip_linear = desc.mip_filter == Filter::linear;
    if (min_linear && mag_linear && mip_linear) return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    if (min_linear && mag_linear && !mip_linear) return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    if (min_linear && !mag_linear && mip_linear) return D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    if (min_linear && !mag_linear && !mip_linear) return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    if (!min_linear && mag_linear && mip_linear) return D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
    if (!min_linear && mag_linear && !mip_linear) return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
    if (!min_linear && !mag_linear && mip_linear) return D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    return D3D12_FILTER_MIN_MAG_MIP_POINT;
}

[[nodiscard]] D3D12_CULL_MODE cull_mode(const CullMode mode) noexcept {
    switch (mode) {
    case CullMode::none: return D3D12_CULL_MODE_NONE;
    case CullMode::front: return D3D12_CULL_MODE_FRONT;
    case CullMode::back: return D3D12_CULL_MODE_BACK;
    }
    return D3D12_CULL_MODE_NONE;
}

[[nodiscard]] const char* shader_target(const ShaderStage stage) noexcept {
    switch (stage) {
    case ShaderStage::vertex: return "vs_5_1";
    case ShaderStage::fragment: return "ps_5_1";
    case ShaderStage::compute: return "cs_5_1";
    }
    return "";
}

class D3D12Buffer final : public Buffer {
public:
    D3D12Buffer(
        const BufferDesc& desc,
        ComPtr<ID3D12Resource> resource,
        const D3D12_RESOURCE_STATES state
    ) : size_{desc.size}, usage_{desc.usage}, memory_{desc.memory},
        resource_{std::move(resource)}, state_{state} {}

    [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }
    [[nodiscard]] BufferUsage usage() const noexcept override { return usage_; }
    [[nodiscard]] MemoryLocation memory_location() const noexcept override { return memory_; }

    bool write(const std::uint64_t offset, const std::span<const std::byte> data) override {
        if (memory_ != MemoryLocation::cpu_to_gpu || resource_ == nullptr ||
            offset > size_ || data.size() > size_ - offset) {
            return false;
        }
        if (data.empty()) {
            return true;
        }
        void* mapped = nullptr;
        const D3D12_RANGE read_range{0, 0};
        if (FAILED(resource_->Map(0, &read_range, &mapped)) || mapped == nullptr) {
            return false;
        }
        auto* destination = static_cast<std::byte*>(mapped);
        std::memcpy(destination + static_cast<std::size_t>(offset), data.data(), data.size());
        const D3D12_RANGE written_range{
            static_cast<SIZE_T>(offset),
            static_cast<SIZE_T>(offset + data.size())
        };
        resource_->Unmap(0, &written_range);
        return true;
    }

    bool read(const std::uint64_t offset, const std::span<std::byte> data) override {
        if (memory_ != MemoryLocation::gpu_to_cpu || resource_ == nullptr ||
            offset > size_ || data.size() > size_ - offset) {
            return false;
        }
        if (data.empty()) {
            return true;
        }
        void* mapped = nullptr;
        const D3D12_RANGE read_range{
            static_cast<SIZE_T>(offset),
            static_cast<SIZE_T>(offset + data.size())
        };
        if (FAILED(resource_->Map(0, &read_range, &mapped)) || mapped == nullptr) {
            return false;
        }
        const auto* source = static_cast<const std::byte*>(mapped);
        std::memcpy(data.data(), source + static_cast<std::size_t>(offset), data.size());
        const D3D12_RANGE written_range{0, 0};
        resource_->Unmap(0, &written_range);
        return true;
    }

    [[nodiscard]] ID3D12Resource* native() const noexcept { return resource_.Get(); }
    [[nodiscard]] D3D12_RESOURCE_STATES state() const noexcept { return state_; }
    void set_state(const D3D12_RESOURCE_STATES state) noexcept { state_ = state; }

private:
    std::uint64_t size_ = 0;
    BufferUsage usage_ = BufferUsage::none;
    MemoryLocation memory_ = MemoryLocation::gpu_only;
    ComPtr<ID3D12Resource> resource_;
    D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_COMMON;
};

class D3D12Texture final : public Texture {
public:
    D3D12Texture(const TextureDesc& desc, ComPtr<ID3D12Resource> resource)
        : desc_{desc}, resource_{std::move(resource)} {
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
    ComPtr<ID3D12Resource> resource_;
};

class D3D12Sampler final : public Sampler {
public:
    explicit D3D12Sampler(const D3D12_SAMPLER_DESC& desc) : desc_{desc} {}
private:
    D3D12_SAMPLER_DESC desc_{};
};

class D3D12ShaderModule final : public ShaderModule {
public:
    D3D12ShaderModule(const ShaderStage stage, ComPtr<ID3DBlob> bytecode)
        : stage_{stage}, bytecode_{std::move(bytecode)} {}

    [[nodiscard]] ShaderStage stage() const noexcept override { return stage_; }
    [[nodiscard]] D3D12_SHADER_BYTECODE native_bytecode() const noexcept {
        D3D12_SHADER_BYTECODE result{};
        result.pShaderBytecode = bytecode_ != nullptr ? bytecode_->GetBufferPointer() : nullptr;
        result.BytecodeLength = bytecode_ != nullptr ? bytecode_->GetBufferSize() : 0;
        return result;
    }

private:
    ShaderStage stage_ = ShaderStage::vertex;
    ComPtr<ID3DBlob> bytecode_;
};

class D3D12GraphicsPipeline final : public GraphicsPipeline {
public:
    D3D12GraphicsPipeline(
        ComPtr<ID3D12RootSignature> root_signature,
        ComPtr<ID3D12PipelineState> state,
        const PrimitiveTopology topology
    ) : root_signature_{std::move(root_signature)}, state_{std::move(state)}, topology_{topology} {}

    [[nodiscard]] PrimitiveTopology topology() const noexcept override { return topology_; }
    [[nodiscard]] ID3D12RootSignature* root_signature() const noexcept { return root_signature_.Get(); }
    [[nodiscard]] ID3D12PipelineState* state() const noexcept { return state_.Get(); }

private:
    ComPtr<ID3D12RootSignature> root_signature_;
    ComPtr<ID3D12PipelineState> state_;
    PrimitiveTopology topology_ = PrimitiveTopology::triangle_list;
};

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
        (void)wait_for_gpu();
        if (fence_event_ != nullptr) {
            CloseHandle(fence_event_);
            fence_event_ = nullptr;
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
        width_ = metrics.drawable_width;
        height_ = metrics.drawable_height;
        return create_render_targets();
    }

    bool clear_and_present(const Color color) override {
        if (!valid_ || swapchain_ == nullptr) {
            return false;
        }
        const UINT index = frame_index();
        if (index >= back_buffer_count) {
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
        transition_resource(
            command_list_.Get(),
            back_buffers_[index].Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        const auto rtv = current_rtv();
        const FLOAT clear_color[4] = {color.red, color.green, color.blue, color.alpha};
        command_list_->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
        transition_resource(
            command_list_.Get(),
            back_buffers_[index].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT
        );
        if (!succeeded(command_list_->Close(), "Direct3D 12 could not close the command list.")) {
            return false;
        }
        ID3D12CommandList* command_lists[] = {command_list_.Get()};
        command_queue_->ExecuteCommandLists(1, command_lists);
        if (!present_now()) {
            return false;
        }
        return wait_for_gpu();
    }

    [[nodiscard]] UINT frame_index() const noexcept {
        return swapchain_ != nullptr ? swapchain_->GetCurrentBackBufferIndex() : 0U;
    }

    [[nodiscard]] ID3D12Resource* current_back_buffer() const noexcept {
        const UINT index = frame_index();
        return index < back_buffer_count ? back_buffers_[index].Get() : nullptr;
    }

    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE current_rtv() const noexcept {
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if (rtv_heap_ == nullptr) {
            return handle;
        }
        handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(frame_index()) * static_cast<SIZE_T>(rtv_descriptor_size_);
        return handle;
    }

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

    bool present_now() {
        return swapchain_ != nullptr && succeeded(
            swapchain_->Present(1, 0),
            "Direct3D 12 could not present the VICE swapchain."
        );
    }

private:
    static void transition_resource(
        ID3D12GraphicsCommandList* command_list,
        ID3D12Resource* resource,
        const D3D12_RESOURCE_STATES before,
        const D3D12_RESOURCE_STATES after
    ) {
        if (command_list == nullptr || resource == nullptr || before == after) {
            return;
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        command_list->ResourceBarrier(1, &barrier);
    }

    bool initialize(const platform::WindowMetrics metrics) {
        ComPtr<IDXGIFactory4> factory;
        if (!succeeded(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Direct3D 12 could not create a DXGI factory.")) {
            return false;
        }
        DXGI_SWAP_CHAIN_DESC1 swapchain_desc{};
        width_ = std::max<UINT>(1U, metrics.drawable_width);
        height_ = std::max<UINT>(1U, metrics.drawable_height);
        swapchain_desc.Width = width_;
        swapchain_desc.Height = height_;
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
                command_queue_.Get(), window_handle_, &swapchain_desc, nullptr, nullptr, &swapchain
            ),
            "Direct3D 12 could not create the VICE swapchain."
        )) {
            return false;
        }
        (void)factory->MakeWindowAssociation(window_handle_, DXGI_MWA_NO_ALT_ENTER);
        if (!succeeded(swapchain.As(&swapchain_), "Direct3D 12 could not access IDXGISwapChain3.")) {
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
            device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr, IID_PPV_ARGS(&command_list_)),
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
    std::uint32_t width_ = 1;
    std::uint32_t height_ = 1;
    bool valid_ = false;
};

class D3D12CommandList final : public CommandList {
public:
    explicit D3D12CommandList(ComPtr<ID3D12Device> device) : device_{std::move(device)} {
        if (device_ == nullptr) {
            return;
        }
        if (!succeeded(
            device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)),
            "Direct3D 12 could not create a VICE command allocator."
        )) {
            return;
        }
        if (!succeeded(
            device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr, IID_PPV_ARGS(&command_list_)),
            "Direct3D 12 could not create a VICE command list."
        )) {
            return;
        }
        valid_ = true;
    }

    [[nodiscard]] bool valid() const noexcept { return valid_ && command_list_ != nullptr; }

    bool copy_buffer(
        Buffer& source,
        const std::uint64_t source_offset,
        Buffer& destination,
        const std::uint64_t destination_offset,
        const std::uint64_t size
    ) override {
        if (render_pass_active_) {
            return false;
        }
        auto* source_buffer = dynamic_cast<D3D12Buffer*>(&source);
        auto* destination_buffer = dynamic_cast<D3D12Buffer*>(&destination);
        if (!valid() || source_buffer == nullptr || destination_buffer == nullptr ||
            !has_usage(source.usage(), BufferUsage::copy_source) ||
            !has_usage(destination.usage(), BufferUsage::copy_destination) ||
            source_offset > source.size() || size > source.size() - source_offset ||
            destination_offset > destination.size() || size > destination.size() - destination_offset) {
            return false;
        }
        if (size == 0) {
            return true;
        }
        transition(*source_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(*destination_buffer, D3D12_RESOURCE_STATE_COPY_DEST);
        command_list_->CopyBufferRegion(
            destination_buffer->native(), destination_offset, source_buffer->native(), source_offset, size
        );
        return true;
    }

    bool begin_render_pass(Swapchain& swapchain, const RenderPassDesc& desc) override {
        if (!valid() || render_pass_active_) {
            return false;
        }
        auto* d3d_swapchain = dynamic_cast<D3D12Swapchain*>(&swapchain);
        if (d3d_swapchain == nullptr || d3d_swapchain->current_back_buffer() == nullptr) {
            return false;
        }
        transition_back_buffer(
            d3d_swapchain->current_back_buffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        const auto rtv = d3d_swapchain->current_rtv();
        command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        if (desc.clear_color_attachment) {
            const FLOAT clear_color[4] = {
                desc.clear_color.red,
                desc.clear_color.green,
                desc.clear_color.blue,
                desc.clear_color.alpha
            };
            command_list_->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
        }
        const D3D12_VIEWPORT viewport{
            0.0F,
            0.0F,
            static_cast<float>(d3d_swapchain->width()),
            static_cast<float>(d3d_swapchain->height()),
            0.0F,
            1.0F
        };
        const D3D12_RECT scissor{
            0,
            0,
            static_cast<LONG>(d3d_swapchain->width()),
            static_cast<LONG>(d3d_swapchain->height())
        };
        command_list_->RSSetViewports(1, &viewport);
        command_list_->RSSetScissorRects(1, &scissor);
        active_swapchain_ = d3d_swapchain;
        render_pass_active_ = true;
        pipeline_bound_ = false;
        return true;
    }

    bool set_graphics_pipeline(GraphicsPipeline& pipeline) override {
        if (!render_pass_active_) {
            return false;
        }
        auto* d3d_pipeline = dynamic_cast<D3D12GraphicsPipeline*>(&pipeline);
        if (d3d_pipeline == nullptr || d3d_pipeline->state() == nullptr ||
            d3d_pipeline->root_signature() == nullptr) {
            return false;
        }
        command_list_->SetGraphicsRootSignature(d3d_pipeline->root_signature());
        command_list_->SetPipelineState(d3d_pipeline->state());
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pipeline_bound_ = true;
        return true;
    }

    bool draw(
        const std::uint32_t vertex_count,
        const std::uint32_t instance_count,
        const std::uint32_t first_vertex,
        const std::uint32_t first_instance
    ) override {
        if (!render_pass_active_ || !pipeline_bound_ || vertex_count == 0 || instance_count == 0) {
            return false;
        }
        command_list_->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
        return true;
    }

    bool end_render_pass() override {
        if (!render_pass_active_) {
            return false;
        }
        render_pass_active_ = false;
        pipeline_bound_ = false;
        return true;
    }

    bool present(Swapchain& swapchain) override {
        auto* d3d_swapchain = dynamic_cast<D3D12Swapchain*>(&swapchain);
        if (!valid() || render_pass_active_ || d3d_swapchain == nullptr ||
            d3d_swapchain != active_swapchain_ || present_swapchain_ != nullptr) {
            return false;
        }
        transition_back_buffer(
            d3d_swapchain->current_back_buffer(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT
        );
        present_swapchain_ = d3d_swapchain;
        return true;
    }

    [[nodiscard]] ID3D12GraphicsCommandList* native() const noexcept { return command_list_.Get(); }
    [[nodiscard]] D3D12Swapchain* present_swapchain() const noexcept { return present_swapchain_; }

    bool close() {
        if (!valid() || render_pass_active_) {
            return false;
        }
        if (!succeeded(command_list_->Close(), "Direct3D 12 could not close a VICE command list.")) {
            return false;
        }
        valid_ = false;
        return true;
    }

private:
    void transition(D3D12Buffer& buffer, const D3D12_RESOURCE_STATES desired) {
        const auto current = buffer.state();
        if (current == desired || (current & desired) == desired) {
            return;
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffer.native();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = current;
        barrier.Transition.StateAfter = desired;
        command_list_->ResourceBarrier(1, &barrier);
        buffer.set_state(desired);
    }

    void transition_back_buffer(
        ID3D12Resource* resource,
        const D3D12_RESOURCE_STATES before,
        const D3D12_RESOURCE_STATES after
    ) {
        if (resource == nullptr || before == after) {
            return;
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        command_list_->ResourceBarrier(1, &barrier);
    }

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> command_list_;
    D3D12Swapchain* active_swapchain_ = nullptr;
    D3D12Swapchain* present_swapchain_ = nullptr;
    bool render_pass_active_ = false;
    bool pipeline_bound_ = false;
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
        if (!succeeded(
            device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)),
            "VICE could not create a Direct3D 12 command queue."
        )) {
            return;
        }
        if (!succeeded(
            device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&submission_fence_)),
            "VICE could not create a Direct3D 12 submission fence."
        )) {
            return;
        }
        submission_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (submission_event_ == nullptr) {
            core::log(core::LogLevel::error, "VICE could not create a Direct3D 12 submission event.");
            return;
        }
        capabilities_.max_texture_dimension_1d = D3D12_REQ_TEXTURE1D_U_DIMENSION;
        capabilities_.max_texture_dimension_2d = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        capabilities_.max_texture_dimension_3d = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        capabilities_.max_texture_array_layers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
        D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{};
        architecture.NodeIndex = 0;
        if (SUCCEEDED(device_->CheckFeatureSupport(
            D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture)
        ))) {
            capabilities_.unified_memory = architecture.UMA != FALSE;
        }
    }

    ~D3D12Device() override {
        (void)wait_idle();
        if (submission_event_ != nullptr) {
            CloseHandle(submission_event_);
            submission_event_ = nullptr;
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return device_ != nullptr && command_queue_ != nullptr &&
            submission_fence_ != nullptr && submission_event_ != nullptr;
    }
    [[nodiscard]] Backend backend() const noexcept override { return Backend::direct3d12; }
    [[nodiscard]] DeviceCapabilities capabilities() const noexcept override { return capabilities_; }

    [[nodiscard]] std::unique_ptr<Buffer> create_buffer(const BufferDesc& desc) override {
        if (!is_valid(desc)) {
            core::log(core::LogLevel::error, "VICE rejected an invalid buffer description.");
            return nullptr;
        }
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = heap_type(desc.memory);
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC native_desc{};
        native_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        native_desc.Width = desc.size;
        native_desc.Height = 1;
        native_desc.DepthOrArraySize = 1;
        native_desc.MipLevels = 1;
        native_desc.SampleDesc.Count = 1;
        native_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        native_desc.Flags = buffer_flags(desc.usage);
        const auto initial_state = initial_buffer_state(desc.memory);
        ComPtr<ID3D12Resource> resource;
        if (!succeeded(
            device_->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &native_desc, initial_state, nullptr, IID_PPV_ARGS(&resource)
            ),
            "Direct3D 12 could not create a VICE buffer."
        )) {
            return nullptr;
        }
        if (const auto name = wide_name(desc.debug_name); !name.empty()) {
            (void)resource->SetName(name.c_str());
        }
        return std::make_unique<D3D12Buffer>(desc, std::move(resource), initial_state);
    }

    [[nodiscard]] std::unique_ptr<Texture> create_texture(const TextureDesc& desc) override {
        if (!is_valid(desc)) {
            core::log(core::LogLevel::error, "VICE rejected an invalid texture description.");
            return nullptr;
        }
        const bool size_supported =
            (desc.dimension == TextureDimension::d1 &&
                desc.width <= capabilities_.max_texture_dimension_1d &&
                desc.array_layers <= capabilities_.max_texture_array_layers) ||
            (desc.dimension == TextureDimension::d2 &&
                desc.width <= capabilities_.max_texture_dimension_2d &&
                desc.height <= capabilities_.max_texture_dimension_2d &&
                desc.array_layers <= capabilities_.max_texture_array_layers) ||
            (desc.dimension == TextureDimension::d3 &&
                desc.width <= capabilities_.max_texture_dimension_3d &&
                desc.height <= capabilities_.max_texture_dimension_3d &&
                desc.depth <= capabilities_.max_texture_dimension_3d);
        if (!size_supported || desc.array_layers > std::numeric_limits<UINT16>::max() ||
            desc.mip_levels > std::numeric_limits<UINT16>::max() ||
            (desc.dimension == TextureDimension::d3 && desc.depth > std::numeric_limits<UINT16>::max())) {
            core::log(core::LogLevel::error, "VICE texture dimensions exceed the Direct3D 12 backend limits.");
            return nullptr;
        }
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC native_desc{};
        native_desc.Dimension = resource_dimension(desc.dimension);
        native_desc.Width = desc.width;
        native_desc.Height = desc.dimension == TextureDimension::d1 ? 1U : desc.height;
        native_desc.DepthOrArraySize = static_cast<UINT16>(
            desc.dimension == TextureDimension::d3 ? desc.depth : desc.array_layers
        );
        native_desc.MipLevels = static_cast<UINT16>(desc.mip_levels);
        native_desc.Format = pixel_format(desc.format);
        native_desc.SampleDesc.Count = desc.sample_count;
        native_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        native_desc.Flags = texture_flags(desc.usage);
        ComPtr<ID3D12Resource> resource;
        if (!succeeded(
            device_->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &native_desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)
            ),
            "Direct3D 12 could not create a VICE texture."
        )) {
            return nullptr;
        }
        if (const auto name = wide_name(desc.debug_name); !name.empty()) {
            (void)resource->SetName(name.c_str());
        }
        return std::make_unique<D3D12Texture>(desc, std::move(resource));
    }

    [[nodiscard]] std::unique_ptr<Sampler> create_sampler(const SamplerDesc& desc) override {
        if (!is_valid(desc)) {
            core::log(core::LogLevel::error, "VICE rejected an invalid sampler description.");
            return nullptr;
        }
        D3D12_SAMPLER_DESC native_desc{};
        native_desc.Filter = sampler_filter(desc);
        native_desc.AddressU = address_mode(desc.address_u);
        native_desc.AddressV = address_mode(desc.address_v);
        native_desc.AddressW = address_mode(desc.address_w);
        native_desc.MaxAnisotropy = desc.max_anisotropy;
        native_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        native_desc.MaxLOD = D3D12_FLOAT32_MAX;
        return std::make_unique<D3D12Sampler>(native_desc);
    }

    [[nodiscard]] std::unique_ptr<ShaderModule> create_shader_module(
        const ShaderModuleDesc& desc
    ) override {
        if (!is_valid(desc) || desc.language != ShaderSourceLanguage::hlsl) {
            core::log(core::LogLevel::error, "Direct3D 12 requires a valid HLSL source shader in development mode.");
            return nullptr;
        }
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        const std::string entry{desc.entry_point};
        const std::string source_name = desc.debug_name.empty()
            ? std::string{"VICE shader"}
            : std::string{desc.debug_name};
        const HRESULT result = D3DCompile(
            desc.source.data(),
            desc.source.size(),
            source_name.c_str(),
            nullptr,
            nullptr,
            entry.c_str(),
            shader_target(desc.stage),
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            &bytecode,
            &errors
        );
        if (FAILED(result) || bytecode == nullptr) {
            if (errors != nullptr && errors->GetBufferPointer() != nullptr) {
                core::log(
                    core::LogLevel::error,
                    std::string{static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()}
                );
            } else {
                core::log(core::LogLevel::error, "Direct3D 12 shader compilation failed.");
            }
            return nullptr;
        }
        return std::make_unique<D3D12ShaderModule>(desc.stage, std::move(bytecode));
    }

    [[nodiscard]] std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDesc& desc
    ) override {
        if (!is_valid(desc) || desc.vertex_shader->stage() != ShaderStage::vertex ||
            desc.fragment_shader->stage() != ShaderStage::fragment) {
            core::log(core::LogLevel::error, "VICE rejected an invalid graphics pipeline description.");
            return nullptr;
        }
        auto* vertex = dynamic_cast<D3D12ShaderModule*>(desc.vertex_shader);
        auto* fragment = dynamic_cast<D3D12ShaderModule*>(desc.fragment_shader);
        if (vertex == nullptr || fragment == nullptr) {
            return nullptr;
        }

        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> serialized_root;
        ComPtr<ID3DBlob> root_errors;
        if (!succeeded(
            D3D12SerializeRootSignature(
                &root_desc,
                D3D_ROOT_SIGNATURE_VERSION_1,
                &serialized_root,
                &root_errors
            ),
            "Direct3D 12 could not serialize the VICE root signature."
        )) {
            return nullptr;
        }
        ComPtr<ID3D12RootSignature> root_signature;
        if (!succeeded(
            device_->CreateRootSignature(
                0,
                serialized_root->GetBufferPointer(),
                serialized_root->GetBufferSize(),
                IID_PPV_ARGS(&root_signature)
            ),
            "Direct3D 12 could not create the VICE root signature."
        )) {
            return nullptr;
        }

        D3D12_RENDER_TARGET_BLEND_DESC blend{};
        blend.BlendEnable = FALSE;
        blend.LogicOpEnable = FALSE;
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_ZERO;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc{};
        pipeline_desc.pRootSignature = root_signature.Get();
        pipeline_desc.VS = vertex->native_bytecode();
        pipeline_desc.PS = fragment->native_bytecode();
        pipeline_desc.BlendState.AlphaToCoverageEnable = FALSE;
        pipeline_desc.BlendState.IndependentBlendEnable = FALSE;
        pipeline_desc.BlendState.RenderTarget[0] = blend;
        pipeline_desc.SampleMask = UINT_MAX;
        pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline_desc.RasterizerState.CullMode = cull_mode(desc.cull_mode);
        pipeline_desc.RasterizerState.FrontCounterClockwise =
            desc.front_face == FrontFace::counter_clockwise ? TRUE : FALSE;
        pipeline_desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        pipeline_desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        pipeline_desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        pipeline_desc.RasterizerState.DepthClipEnable = TRUE;
        pipeline_desc.RasterizerState.MultisampleEnable = FALSE;
        pipeline_desc.RasterizerState.AntialiasedLineEnable = FALSE;
        pipeline_desc.RasterizerState.ForcedSampleCount = 0;
        pipeline_desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        pipeline_desc.DepthStencilState.DepthEnable = FALSE;
        pipeline_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        pipeline_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipeline_desc.DepthStencilState.StencilEnable = FALSE;
        pipeline_desc.InputLayout = {nullptr, 0};
        pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline_desc.NumRenderTargets = 1;
        pipeline_desc.RTVFormats[0] = pixel_format(desc.color_format);
        pipeline_desc.SampleDesc.Count = 1;

        ComPtr<ID3D12PipelineState> state;
        if (!succeeded(
            device_->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&state)),
            "Direct3D 12 could not create the VICE graphics pipeline."
        )) {
            return nullptr;
        }
        if (const auto name = wide_name(desc.debug_name); !name.empty()) {
            (void)state->SetName(name.c_str());
        }
        return std::make_unique<D3D12GraphicsPipeline>(
            std::move(root_signature),
            std::move(state),
            desc.topology
        );
    }

    [[nodiscard]] std::unique_ptr<CommandList> create_command_list() override {
        auto command_list = std::make_unique<D3D12CommandList>(device_);
        if (!command_list->valid()) {
            return nullptr;
        }
        return command_list;
    }

    SubmissionId submit(std::unique_ptr<CommandList> command_list) override {
        auto* d3d_commands = dynamic_cast<D3D12CommandList*>(command_list.get());
        if (d3d_commands == nullptr || !d3d_commands->close()) {
            return invalid_submission;
        }
        ID3D12CommandList* command_lists[] = {d3d_commands->native()};
        command_queue_->ExecuteCommandLists(1, command_lists);
        if (auto* swapchain = d3d_commands->present_swapchain(); swapchain != nullptr) {
            if (!swapchain->present_now()) {
                return invalid_submission;
            }
        }
        const SubmissionId submission = ++submitted_submission_;
        if (!succeeded(
            command_queue_->Signal(submission_fence_.Get(), submission),
            "Direct3D 12 could not signal the VICE submission fence."
        )) {
            return invalid_submission;
        }
        if (!wait_for_submission(submission)) {
            return invalid_submission;
        }
        return submission;
    }

    [[nodiscard]] SubmissionId completed_submission() const noexcept override {
        return submission_fence_ != nullptr
            ? submission_fence_->GetCompletedValue()
            : invalid_submission;
    }

    bool wait_for_submission(const SubmissionId submission) override {
        if (submission == invalid_submission || submission_fence_ == nullptr || submission_event_ == nullptr) {
            return false;
        }
        if (submission_fence_->GetCompletedValue() >= submission) {
            return true;
        }
        if (FAILED(submission_fence_->SetEventOnCompletion(submission, submission_event_))) {
            return false;
        }
        return WaitForSingleObject(submission_event_, INFINITE) == WAIT_OBJECT_0;
    }

    bool wait_idle() override {
        if (command_queue_ == nullptr || submission_fence_ == nullptr || submission_event_ == nullptr) {
            return false;
        }
        const SubmissionId submission = ++submitted_submission_;
        if (FAILED(command_queue_->Signal(submission_fence_.Get(), submission))) {
            return false;
        }
        return wait_for_submission(submission);
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
    ComPtr<ID3D12Fence> submission_fence_;
    HANDLE submission_event_ = nullptr;
    DeviceCapabilities capabilities_{};
    SubmissionId submitted_submission_ = invalid_submission;
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
