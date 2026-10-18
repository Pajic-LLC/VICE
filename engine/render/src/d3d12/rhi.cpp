#include <vice/core/log.hpp>
#include <vice/platform/native_window.hpp>
#include <vice/render/d3d12_root_signature.hpp>
#include <vice/render/pipeline_layout.hpp>
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
#include <vector>

namespace vice::render {
namespace {

using Microsoft::WRL::ComPtr;
constexpr UINT back_buffer_count = 2;
constexpr std::uint64_t constant_buffer_alignment = 256U;
constexpr std::uint64_t maximum_constant_buffer_size = 64U * 1024U;

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

[[nodiscard]] bool aligned_size(
    const std::uint64_t value,
    const std::uint64_t alignment,
    std::uint64_t& result
) noexcept {
    if (alignment == 0U || value > std::numeric_limits<std::uint64_t>::max() - (alignment - 1U)) {
        return false;
    }
    result = (value + alignment - 1U) / alignment * alignment;
    return true;
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
    case TextureFormat::rgba8_unorm_srgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case TextureFormat::bgra8_unorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::rgba16_float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case TextureFormat::r32_float: return DXGI_FORMAT_R32_FLOAT;
    case TextureFormat::depth32_float: return DXGI_FORMAT_D32_FLOAT;
    case TextureFormat::undefined: return DXGI_FORMAT_UNKNOWN;
    }
    return DXGI_FORMAT_UNKNOWN;
}

[[nodiscard]] DXGI_FORMAT vertex_format(const VertexFormat format) noexcept {
    switch (format) {
    case VertexFormat::float32: return DXGI_FORMAT_R32_FLOAT;
    case VertexFormat::float32x2: return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::float32x3: return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::float32x4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case VertexFormat::float16x2: return DXGI_FORMAT_R16G16_FLOAT;
    case VertexFormat::float16x4: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case VertexFormat::uint32: return DXGI_FORMAT_R32_UINT;
    case VertexFormat::uint32x2: return DXGI_FORMAT_R32G32_UINT;
    case VertexFormat::uint32x3: return DXGI_FORMAT_R32G32B32_UINT;
    case VertexFormat::uint32x4: return DXGI_FORMAT_R32G32B32A32_UINT;
    case VertexFormat::sint32: return DXGI_FORMAT_R32_SINT;
    case VertexFormat::sint32x2: return DXGI_FORMAT_R32G32_SINT;
    case VertexFormat::sint32x3: return DXGI_FORMAT_R32G32B32_SINT;
    case VertexFormat::sint32x4: return DXGI_FORMAT_R32G32B32A32_SINT;
    case VertexFormat::uint8x2: return DXGI_FORMAT_R8G8_UINT;
    case VertexFormat::uint8x4: return DXGI_FORMAT_R8G8B8A8_UINT;
    case VertexFormat::sint8x2: return DXGI_FORMAT_R8G8_SINT;
    case VertexFormat::sint8x4: return DXGI_FORMAT_R8G8B8A8_SINT;
    case VertexFormat::unorm8x2: return DXGI_FORMAT_R8G8_UNORM;
    case VertexFormat::unorm8x4: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VertexFormat::snorm8x2: return DXGI_FORMAT_R8G8_SNORM;
    case VertexFormat::snorm8x4: return DXGI_FORMAT_R8G8B8A8_SNORM;
    case VertexFormat::uint16x2: return DXGI_FORMAT_R16G16_UINT;
    case VertexFormat::uint16x4: return DXGI_FORMAT_R16G16B16A16_UINT;
    case VertexFormat::sint16x2: return DXGI_FORMAT_R16G16_SINT;
    case VertexFormat::sint16x4: return DXGI_FORMAT_R16G16B16A16_SINT;
    case VertexFormat::unorm16x2: return DXGI_FORMAT_R16G16_UNORM;
    case VertexFormat::unorm16x4: return DXGI_FORMAT_R16G16B16A16_UNORM;
    case VertexFormat::snorm16x2: return DXGI_FORMAT_R16G16_SNORM;
    case VertexFormat::snorm16x4: return DXGI_FORMAT_R16G16B16A16_SNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}

[[nodiscard]] D3D12_COMPARISON_FUNC comparison_func(const CompareOp compare) noexcept {
    switch (compare) {
    case CompareOp::never: return D3D12_COMPARISON_FUNC_NEVER;
    case CompareOp::less: return D3D12_COMPARISON_FUNC_LESS;
    case CompareOp::less_equal: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case CompareOp::greater: return D3D12_COMPARISON_FUNC_GREATER;
    case CompareOp::greater_equal: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case CompareOp::equal: return D3D12_COMPARISON_FUNC_EQUAL;
    case CompareOp::not_equal: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case CompareOp::always: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_ALWAYS;
}

[[nodiscard]] DXGI_FORMAT texture_resource_format(const TextureDesc& desc) noexcept {
    if (desc.format == TextureFormat::depth32_float && has_usage(desc.usage, TextureUsage::sampled)) {
        return DXGI_FORMAT_R32_TYPELESS;
    }
    return pixel_format(desc.format);
}

[[nodiscard]] DXGI_FORMAT shader_view_format(const TextureFormat format) noexcept {
    return format == TextureFormat::depth32_float
        ? DXGI_FORMAT_R32_FLOAT
        : pixel_format(format);
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

[[nodiscard]] bool same_binding_key(
    const ShaderBindingKey& left,
    const ShaderBindingKey& right
) noexcept {
    return same_binding_namespace(left, right) && left.array_element == right.array_element;
}

struct D3D12VertexStreamInfo {
    std::uint32_t stride = 0;
    VertexStepMode step_mode = VertexStepMode::per_vertex;
    std::uint32_t step_rate = 1;
};

class D3D12Buffer final : public Buffer {
public:
    D3D12Buffer(
        const BufferDesc& desc,
        ComPtr<ID3D12Resource> resource,
        const std::uint64_t allocation_size,
        const D3D12_RESOURCE_STATES state
    ) : size_{desc.size}, allocation_size_{allocation_size}, usage_{desc.usage}, memory_{desc.memory},
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
    [[nodiscard]] std::uint64_t allocation_size() const noexcept { return allocation_size_; }
    [[nodiscard]] D3D12_RESOURCE_STATES state() const noexcept { return state_; }
    void set_state(const D3D12_RESOURCE_STATES state) noexcept { state_ = state; }

private:
    std::uint64_t size_ = 0;
    std::uint64_t allocation_size_ = 0;
    BufferUsage usage_ = BufferUsage::none;
    MemoryLocation memory_ = MemoryLocation::gpu_only;
    ComPtr<ID3D12Resource> resource_;
    D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_COMMON;
};

class D3D12Texture final : public Texture {
public:
    D3D12Texture(
        const TextureDesc& desc,
        ComPtr<ID3D12Device> device,
        ComPtr<ID3D12CommandQueue> command_queue,
        ComPtr<ID3D12Resource> resource,
        const D3D12_RESOURCE_STATES state,
        ComPtr<ID3D12DescriptorHeap> dsv_heap = {}
    ) : desc_{desc}, device_{std::move(device)}, command_queue_{std::move(command_queue)},
        resource_{std::move(resource)}, state_{state}, dsv_heap_{std::move(dsv_heap)} {
        desc_.debug_name = {};
    }

    [[nodiscard]] TextureDimension dimension() const noexcept override { return desc_.dimension; }
    [[nodiscard]] TextureFormat format() const noexcept override { return desc_.format; }
    [[nodiscard]] TextureUsage usage() const noexcept override { return desc_.usage; }
    [[nodiscard]] std::uint32_t width() const noexcept override { return desc_.width; }
    [[nodiscard]] std::uint32_t height() const noexcept override { return desc_.height; }
    [[nodiscard]] std::uint32_t depth() const noexcept override { return desc_.depth; }

    bool write_2d(
        const std::span<const std::byte> data,
        const std::uint64_t bytes_per_row
    ) override {
        if (device_ == nullptr || command_queue_ == nullptr || resource_ == nullptr ||
            desc_.dimension != TextureDimension::d2 || desc_.depth != 1U ||
            desc_.array_layers != 1U || desc_.mip_levels != 1U || desc_.sample_count != 1U ||
            !has_usage(desc_.usage, TextureUsage::copy_destination) ||
            desc_.format == TextureFormat::depth32_float) {
            return false;
        }

        const std::uint64_t bytes_per_pixel = texture_format_bytes_per_pixel(desc_.format);
        if (bytes_per_pixel == 0U ||
            desc_.width > std::numeric_limits<std::uint64_t>::max() / bytes_per_pixel) {
            return false;
        }
        const std::uint64_t minimum_row = static_cast<std::uint64_t>(desc_.width) * bytes_per_pixel;
        if (bytes_per_row < minimum_row ||
            desc_.height > 0U && bytes_per_row >
                std::numeric_limits<std::uint64_t>::max() / desc_.height) {
            return false;
        }
        const std::uint64_t required_source = bytes_per_row * desc_.height;
        if (required_source > data.size()) {
            return false;
        }

        const D3D12_RESOURCE_DESC texture_desc = resource_->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT row_count = 0U;
        UINT64 row_size = 0U;
        UINT64 upload_size = 0U;
        device_->GetCopyableFootprints(
            &texture_desc,
            0U,
            1U,
            0U,
            &footprint,
            &row_count,
            &row_size,
            &upload_size
        );
        if (row_count != desc_.height || row_size < minimum_row || upload_size == 0U ||
            upload_size > std::numeric_limits<std::size_t>::max()) {
            return false;
        }

        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        upload_heap.CreationNodeMask = 1U;
        upload_heap.VisibleNodeMask = 1U;
        D3D12_RESOURCE_DESC upload_desc{};
        upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_desc.Width = upload_size;
        upload_desc.Height = 1U;
        upload_desc.DepthOrArraySize = 1U;
        upload_desc.MipLevels = 1U;
        upload_desc.SampleDesc.Count = 1U;
        upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> upload;
        if (FAILED(device_->CreateCommittedResource(
                &upload_heap,
                D3D12_HEAP_FLAG_NONE,
                &upload_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&upload)
            ))) {
            return false;
        }

        void* mapped = nullptr;
        const D3D12_RANGE read_range{0U, 0U};
        if (FAILED(upload->Map(0U, &read_range, &mapped)) || mapped == nullptr) {
            return false;
        }
        auto* destination = static_cast<std::byte*>(mapped) +
            static_cast<std::size_t>(footprint.Offset);
        for (std::uint32_t row = 0U; row < desc_.height; ++row) {
            std::memcpy(
                destination + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                data.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(bytes_per_row),
                static_cast<std::size_t>(minimum_row)
            );
        }
        upload->Unmap(0U, nullptr);

        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        if (FAILED(device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&allocator)
            )) || FAILED(device_->CreateCommandList(
                0U,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&commands)
            ))) {
            return false;
        }

        if (state_ != D3D12_RESOURCE_STATE_COPY_DEST) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource_.Get();
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = state_;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            commands->ResourceBarrier(1U, &barrier);
        }

        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION destination_location{};
        destination_location.pResource = resource_.Get();
        destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination_location.SubresourceIndex = 0U;
        commands->CopyTextureRegion(&destination_location, 0U, 0U, 0U, &source, nullptr);

        D3D12_RESOURCE_BARRIER final_barrier{};
        final_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        final_barrier.Transition.pResource = resource_.Get();
        final_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        final_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        final_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        commands->ResourceBarrier(1U, &final_barrier);

        if (FAILED(commands->Close())) {
            return false;
        }
        ID3D12CommandList* submitted[] = {commands.Get()};
        command_queue_->ExecuteCommandLists(1U, submitted);

        ComPtr<ID3D12Fence> fence;
        if (FAILED(device_->CreateFence(0U, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
            return false;
        }
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event == nullptr) {
            return false;
        }
        const HRESULT signal = command_queue_->Signal(fence.Get(), 1U);
        bool completed = SUCCEEDED(signal);
        if (completed && fence->GetCompletedValue() < 1U) {
            completed = SUCCEEDED(fence->SetEventOnCompletion(1U, event)) &&
                WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0;
        }
        CloseHandle(event);
        if (!completed) {
            return false;
        }
        state_ = D3D12_RESOURCE_STATE_COMMON;
        return true;
    }

    [[nodiscard]] const TextureDesc& desc() const noexcept { return desc_; }
    [[nodiscard]] ID3D12Resource* native() const noexcept { return resource_.Get(); }
    [[nodiscard]] D3D12_RESOURCE_STATES state() const noexcept { return state_; }
    void set_state(const D3D12_RESOURCE_STATES state) noexcept { state_ = state; }
    [[nodiscard]] bool has_dsv() const noexcept { return dsv_heap_ != nullptr; }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE dsv() const noexcept {
        return dsv_heap_ != nullptr
            ? dsv_heap_->GetCPUDescriptorHandleForHeapStart()
            : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

private:
    TextureDesc desc_{};
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> command_queue_;
    ComPtr<ID3D12Resource> resource_;
    D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_COMMON;
    ComPtr<ID3D12DescriptorHeap> dsv_heap_;
};

class D3D12Sampler final : public Sampler {
public:
    explicit D3D12Sampler(const D3D12_SAMPLER_DESC& desc) : desc_{desc} {}
    [[nodiscard]] const D3D12_SAMPLER_DESC& native_desc() const noexcept { return desc_; }
private:
    D3D12_SAMPLER_DESC desc_{};
};

class D3D12ShaderModule final : public ShaderModule {
public:
    D3D12ShaderModule(
        const ShaderStage stage,
        std::vector<std::byte> bytecode,
        std::vector<ShaderResourceBindingDesc> resources = {}
    ) : stage_{stage}, bytecode_{std::move(bytecode)}, resources_{std::move(resources)} {}

    [[nodiscard]] ShaderStage stage() const noexcept override { return stage_; }
    [[nodiscard]] std::span<const ShaderResourceBindingDesc> resources() const noexcept override {
        return resources_;
    }
    [[nodiscard]] D3D12_SHADER_BYTECODE native_bytecode() const noexcept {
        D3D12_SHADER_BYTECODE result{};
        result.pShaderBytecode = bytecode_.empty() ? nullptr : bytecode_.data();
        result.BytecodeLength = bytecode_.size();
        return result;
    }

private:
    ShaderStage stage_ = ShaderStage::vertex;
    std::vector<std::byte> bytecode_;
    std::vector<ShaderResourceBindingDesc> resources_;
};

class D3D12GraphicsPipeline final : public GraphicsPipeline {
public:
    D3D12GraphicsPipeline(
        ComPtr<ID3D12RootSignature> root_signature,
        ComPtr<ID3D12PipelineState> state,
        const PrimitiveTopology topology,
        D3D12RootSignatureResult root_layout,
        std::vector<D3D12VertexStreamInfo> vertex_streams,
        const DepthStateDesc depth
    ) : root_signature_{std::move(root_signature)}, state_{std::move(state)}, topology_{topology},
        root_layout_{std::move(root_layout)}, vertex_streams_{std::move(vertex_streams)}, depth_{depth} {}

    [[nodiscard]] PrimitiveTopology topology() const noexcept override { return topology_; }
    [[nodiscard]] ID3D12RootSignature* root_signature() const noexcept { return root_signature_.Get(); }
    [[nodiscard]] ID3D12PipelineState* state() const noexcept { return state_.Get(); }
    [[nodiscard]] const D3D12RootSignatureResult& root_layout() const noexcept { return root_layout_; }
    [[nodiscard]] const DepthStateDesc& depth() const noexcept { return depth_; }
    [[nodiscard]] std::size_t vertex_stream_count() const noexcept { return vertex_streams_.size(); }
    [[nodiscard]] const D3D12VertexStreamInfo* vertex_stream(const std::uint32_t slot) const noexcept {
        return slot < vertex_streams_.size() ? &vertex_streams_[slot] : nullptr;
    }
    [[nodiscard]] const D3D12BindingLocation* binding_location(
        const ShaderBindingKey& binding
    ) const noexcept {
        return find_d3d12_binding_location(root_layout_, binding);
    }

private:
    ComPtr<ID3D12RootSignature> root_signature_;
    ComPtr<ID3D12PipelineState> state_;
    PrimitiveTopology topology_ = PrimitiveTopology::triangle_list;
    D3D12RootSignatureResult root_layout_;
    std::vector<D3D12VertexStreamInfo> vertex_streams_;
    DepthStateDesc depth_{};
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

enum class PendingGraphicsBindingKind : std::uint8_t {
    constant_buffer,
    shader_buffer,
    storage_buffer,
    texture,
    storage_texture,
    sampler,
};

struct PendingGraphicsBinding {
    ShaderBindingKey key{};
    PendingGraphicsBindingKind kind = PendingGraphicsBindingKind::texture;
    D3D12Buffer* buffer = nullptr;
    D3D12Texture* texture = nullptr;
    D3D12Sampler* sampler = nullptr;
    BufferViewDesc view{};
    std::uint64_t constant_offset = 0;
    std::uint64_t constant_size = 0;
};

struct DescriptorTableAllocation {
    std::uint32_t root_parameter_index = 0;
    D3D12DescriptorHeapClass heap_class = D3D12DescriptorHeapClass::resources;
    std::uint32_t heap_offset = 0;
    std::uint32_t descriptor_count = 0;
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
        resource_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
        sampler_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
        );
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

        D3D12Texture* depth_texture = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE depth_handle{};
        if (desc.depth_attachment != nullptr) {
            depth_texture = dynamic_cast<D3D12Texture*>(desc.depth_attachment);
            if (depth_texture == nullptr || depth_texture->format() != TextureFormat::depth32_float ||
                !has_usage(depth_texture->usage(), TextureUsage::depth_attachment) ||
                !depth_texture->has_dsv() || depth_texture->width() != d3d_swapchain->width() ||
                depth_texture->height() != d3d_swapchain->height()) {
                return false;
            }
            transition(*depth_texture, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            depth_handle = depth_texture->dsv();
        }

        transition_back_buffer(
            d3d_swapchain->current_back_buffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        const auto rtv = d3d_swapchain->current_rtv();
        command_list_->OMSetRenderTargets(
            1,
            &rtv,
            FALSE,
            depth_texture != nullptr ? &depth_handle : nullptr
        );
        if (desc.clear_color_attachment) {
            const FLOAT clear_color[4] = {
                desc.clear_color.red,
                desc.clear_color.green,
                desc.clear_color.blue,
                desc.clear_color.alpha
            };
            command_list_->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
        }
        if (depth_texture != nullptr && desc.clear_depth_attachment) {
            command_list_->ClearDepthStencilView(
                depth_handle,
                D3D12_CLEAR_FLAG_DEPTH,
                desc.clear_depth,
                0,
                0,
                nullptr
            );
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
        active_depth_texture_ = depth_texture;
        render_pass_active_ = true;
        reset_graphics_bindings();
        return true;
    }

    bool set_viewport(const Viewport& viewport) override {
        if (!render_pass_active_ || !is_valid(viewport)) {
            return false;
        }
        const D3D12_VIEWPORT native{
            viewport.x,
            viewport.y,
            viewport.width,
            viewport.height,
            viewport.min_depth,
            viewport.max_depth,
        };
        command_list_->RSSetViewports(1, &native);
        return true;
    }

    bool set_scissor_rect(const ScissorRect& scissor) override {
        if (!render_pass_active_ || !is_valid(scissor)) {
            return false;
        }
        const std::int64_t right =
            static_cast<std::int64_t>(scissor.x) + static_cast<std::int64_t>(scissor.width);
        const std::int64_t bottom =
            static_cast<std::int64_t>(scissor.y) + static_cast<std::int64_t>(scissor.height);
        if (right > std::numeric_limits<LONG>::max() ||
            right < std::numeric_limits<LONG>::min() ||
            bottom > std::numeric_limits<LONG>::max() ||
            bottom < std::numeric_limits<LONG>::min()) {
            return false;
        }
        const D3D12_RECT native{
            static_cast<LONG>(scissor.x),
            static_cast<LONG>(scissor.y),
            static_cast<LONG>(right),
            static_cast<LONG>(bottom),
        };
        command_list_->RSSetScissorRects(1, &native);
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
        const auto& depth = d3d_pipeline->depth();
        if (depth.format != TextureFormat::undefined &&
            (active_depth_texture_ == nullptr || active_depth_texture_->format() != depth.format)) {
            return false;
        }
        command_list_->SetGraphicsRootSignature(d3d_pipeline->root_signature());
        command_list_->SetPipelineState(d3d_pipeline->state());
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        active_pipeline_ = d3d_pipeline;
        pending_bindings_.clear();
        active_resource_heap_.Reset();
        active_sampler_heap_.Reset();
        vertex_buffers_bound_.fill(false);
        vertex_capacities_.fill(0U);
        index_buffer_bound_ = false;
        index_capacity_ = 0U;
        bindings_dirty_ = true;
        pipeline_bound_ = true;
        return true;
    }

    bool set_vertex_buffer(
        const std::uint32_t slot,
        Buffer& buffer,
        const std::uint64_t offset
    ) override {
        auto* native_buffer = dynamic_cast<D3D12Buffer*>(&buffer);
        if (!render_pass_active_ || !pipeline_bound_ || active_pipeline_ == nullptr ||
            native_buffer == nullptr || !has_usage(buffer.usage(), BufferUsage::vertex) ||
            offset > buffer.size()) {
            return false;
        }
        const D3D12VertexStreamInfo* stream = active_pipeline_->vertex_stream(slot);
        if (stream == nullptr || stream->stride == 0U) {
            return false;
        }
        const std::uint64_t remaining = buffer.size() - offset;
        if (remaining < stream->stride || remaining > std::numeric_limits<UINT>::max()) {
            return false;
        }
        transition(*native_buffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = native_buffer->native()->GetGPUVirtualAddress() + offset;
        view.SizeInBytes = static_cast<UINT>(remaining);
        view.StrideInBytes = stream->stride;
        command_list_->IASetVertexBuffers(slot, 1, &view);
        vertex_buffers_bound_[slot] = true;
        vertex_capacities_[slot] = remaining / stream->stride;
        return true;
    }

    bool set_index_buffer(
        Buffer& buffer,
        const IndexFormat format,
        const std::uint64_t offset
    ) override {
        auto* native_buffer = dynamic_cast<D3D12Buffer*>(&buffer);
        const std::uint32_t element_size = index_format_size(format);
        if (!render_pass_active_ || !pipeline_bound_ || native_buffer == nullptr ||
            !has_usage(buffer.usage(), BufferUsage::index) || element_size == 0U ||
            offset > buffer.size() || offset % element_size != 0U) {
            return false;
        }
        const std::uint64_t remaining = buffer.size() - offset;
        if (remaining < element_size || remaining > std::numeric_limits<UINT>::max()) {
            return false;
        }
        transition(*native_buffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
        D3D12_INDEX_BUFFER_VIEW view{};
        view.BufferLocation = native_buffer->native()->GetGPUVirtualAddress() + offset;
        view.SizeInBytes = static_cast<UINT>(remaining);
        view.Format = format == IndexFormat::uint16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        command_list_->IASetIndexBuffer(&view);
        index_buffer_bound_ = true;
        index_capacity_ = remaining / element_size;
        return true;
    }

    bool set_graphics_constant_buffer(
        const ShaderBindingKey& binding,
        Buffer& buffer,
        const std::uint64_t offset,
        const std::uint64_t size
    ) override {
        auto* native_buffer = dynamic_cast<D3D12Buffer*>(&buffer);
        if (!binding_allowed(binding, ShaderRegisterClass::constant_buffer) ||
            native_buffer == nullptr || !has_usage(buffer.usage(), BufferUsage::uniform) ||
            offset > buffer.size() || (size != 0U && size > buffer.size() - offset)) {
            return false;
        }
        PendingGraphicsBinding pending;
        pending.key = binding;
        pending.kind = PendingGraphicsBindingKind::constant_buffer;
        pending.buffer = native_buffer;
        pending.constant_offset = offset;
        pending.constant_size = size;
        upsert_binding(std::move(pending));
        return true;
    }

    bool set_graphics_buffer(
        const ShaderBindingKey& binding,
        Buffer& buffer,
        const BufferViewDesc& view
    ) override {
        auto* native_buffer = dynamic_cast<D3D12Buffer*>(&buffer);
        if (!binding_allowed(binding, ShaderRegisterClass::shader_resource) ||
            native_buffer == nullptr || !has_usage(buffer.usage(), BufferUsage::storage) ||
            !valid_buffer_view(buffer, view)) {
            return false;
        }
        PendingGraphicsBinding pending;
        pending.key = binding;
        pending.kind = PendingGraphicsBindingKind::shader_buffer;
        pending.buffer = native_buffer;
        pending.view = view;
        upsert_binding(std::move(pending));
        return true;
    }

    bool set_graphics_storage_buffer(
        const ShaderBindingKey& binding,
        Buffer& buffer,
        const BufferViewDesc& view
    ) override {
        auto* native_buffer = dynamic_cast<D3D12Buffer*>(&buffer);
        if (!binding_allowed(binding, ShaderRegisterClass::unordered_access) ||
            native_buffer == nullptr || !has_usage(buffer.usage(), BufferUsage::storage) ||
            !valid_buffer_view(buffer, view)) {
            return false;
        }
        PendingGraphicsBinding pending;
        pending.key = binding;
        pending.kind = PendingGraphicsBindingKind::storage_buffer;
        pending.buffer = native_buffer;
        pending.view = view;
        upsert_binding(std::move(pending));
        return true;
    }

    bool set_graphics_texture(const ShaderBindingKey& binding, Texture& texture) override {
        auto* native_texture = dynamic_cast<D3D12Texture*>(&texture);
        if (!binding_allowed(binding, ShaderRegisterClass::shader_resource) ||
            native_texture == nullptr || !has_usage(texture.usage(), TextureUsage::sampled)) {
            return false;
        }
        PendingGraphicsBinding pending;
        pending.key = binding;
        pending.kind = PendingGraphicsBindingKind::texture;
        pending.texture = native_texture;
        upsert_binding(std::move(pending));
        return true;
    }

    bool set_graphics_storage_texture(
        const ShaderBindingKey& binding,
        Texture& texture
    ) override {
        auto* native_texture = dynamic_cast<D3D12Texture*>(&texture);
        if (!binding_allowed(binding, ShaderRegisterClass::unordered_access) ||
            native_texture == nullptr || !has_usage(texture.usage(), TextureUsage::storage)) {
            return false;
        }
        PendingGraphicsBinding pending;
        pending.key = binding;
        pending.kind = PendingGraphicsBindingKind::storage_texture;
        pending.texture = native_texture;
        upsert_binding(std::move(pending));
        return true;
    }

    bool set_graphics_sampler(const ShaderBindingKey& binding, Sampler& sampler) override {
        auto* native_sampler = dynamic_cast<D3D12Sampler*>(&sampler);
        if (!binding_allowed(binding, ShaderRegisterClass::sampler) || native_sampler == nullptr) {
            return false;
        }
        PendingGraphicsBinding pending;
        pending.key = binding;
        pending.kind = PendingGraphicsBindingKind::sampler;
        pending.sampler = native_sampler;
        upsert_binding(std::move(pending));
        return true;
    }

    bool draw(
        const std::uint32_t vertex_count,
        const std::uint32_t instance_count,
        const std::uint32_t first_vertex,
        const std::uint32_t first_instance
    ) override {
        if (!render_pass_active_ || !pipeline_bound_ || active_pipeline_ == nullptr ||
            vertex_count == 0 || instance_count == 0 ||
            !vertex_streams_cover_draw(vertex_count, instance_count, first_vertex, first_instance)) {
            return false;
        }
        if (!materialize_graphics_bindings()) {
            return false;
        }
        command_list_->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
        return true;
    }

    bool draw_indexed(
        const std::uint32_t index_count,
        const std::uint32_t instance_count,
        const std::uint32_t first_index,
        const std::int32_t vertex_offset,
        const std::uint32_t first_instance
    ) override {
        if (!render_pass_active_ || !pipeline_bound_ || active_pipeline_ == nullptr ||
            !index_buffer_bound_ || index_count == 0U || instance_count == 0U ||
            first_index > index_capacity_ || index_count > index_capacity_ - first_index ||
            !vertex_streams_cover_instances(instance_count, first_instance)) {
            return false;
        }
        if (!materialize_graphics_bindings()) {
            return false;
        }
        command_list_->DrawIndexedInstanced(
            index_count,
            instance_count,
            first_index,
            vertex_offset,
            first_instance
        );
        return true;
    }

    bool end_render_pass() override {
        if (!render_pass_active_) {
            return false;
        }
        render_pass_active_ = false;
        active_depth_texture_ = nullptr;
        reset_graphics_bindings();
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
    [[nodiscard]] bool binding_allowed(
        const ShaderBindingKey& binding,
        const ShaderRegisterClass expected_class
    ) const noexcept {
        return render_pass_active_ && pipeline_bound_ && active_pipeline_ != nullptr &&
            binding.register_class == expected_class &&
            active_pipeline_->binding_location(binding) != nullptr;
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
        if (!all_vertex_buffers_bound() || active_pipeline_ == nullptr) {
            return false;
        }
        if (instance_count == 0U) {
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

    void upsert_binding(PendingGraphicsBinding pending) {
        const auto found = std::find_if(
            pending_bindings_.begin(),
            pending_bindings_.end(),
            [&](const PendingGraphicsBinding& current) {
                return same_binding_key(current.key, pending.key);
            }
        );
        if (found == pending_bindings_.end()) {
            pending_bindings_.push_back(std::move(pending));
        } else {
            *found = std::move(pending);
        }
        bindings_dirty_ = true;
    }

    [[nodiscard]] const PendingGraphicsBinding* pending_binding(
        const D3D12BindingLocation& location,
        const std::uint32_t array_element
    ) const noexcept {
        const auto found = std::find_if(
            pending_bindings_.begin(),
            pending_bindings_.end(),
            [&](const PendingGraphicsBinding& pending) {
                return pending.key.register_class == location.register_class &&
                    pending.key.binding == location.binding &&
                    pending.key.space == location.space &&
                    pending.key.array_element == array_element;
            }
        );
        return found == pending_bindings_.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool binding_set_is_complete(
        const D3D12BindingLocation& location,
        std::uint32_t& required_count
    ) const noexcept {
        std::uint64_t match_count = 0;
        std::uint32_t maximum_element = 0;
        bool found_any = false;
        for (const auto& pending : pending_bindings_) {
            if (pending.key.register_class != location.register_class ||
                pending.key.binding != location.binding || pending.key.space != location.space) {
                continue;
            }
            ++match_count;
            maximum_element = std::max(maximum_element, pending.key.array_element);
            found_any = true;
        }

        if (location.unbounded) {
            if (!found_any || maximum_element == std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            const std::uint64_t count = static_cast<std::uint64_t>(maximum_element) + 1U;
            if (match_count != count || count > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            required_count = static_cast<std::uint32_t>(count);
            return true;
        }

        if (match_count != location.count) {
            return false;
        }
        required_count = location.count;
        return true;
    }

    [[nodiscard]] bool table_descriptor_count(
        const D3D12DescriptorTableLayout& table,
        std::uint32_t& count
    ) const noexcept {
        if (!table.unbounded) {
            count = table.bounded_descriptor_count;
            return count > 0U;
        }
        if (active_pipeline_ == nullptr) {
            return false;
        }
        for (const auto& location : active_pipeline_->root_layout().bindings) {
            if (location.root_parameter_index != table.root_parameter_index || !location.unbounded) {
                continue;
            }
            return binding_set_is_complete(location, count);
        }
        return false;
    }

    [[nodiscard]] bool create_shader_visible_heap(
        const D3D12_DESCRIPTOR_HEAP_TYPE type,
        const std::uint32_t descriptor_count,
        ComPtr<ID3D12DescriptorHeap>& heap
    ) {
        if (descriptor_count == 0U) {
            heap.Reset();
            return true;
        }
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = type;
        desc.NumDescriptors = descriptor_count;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        desc.NodeMask = 0;
        if (!succeeded(
            device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)),
            "Direct3D 12 could not create a VICE shader-visible descriptor heap."
        )) {
            return false;
        }
        retained_heaps_.push_back(heap);
        return true;
    }

    [[nodiscard]] const DescriptorTableAllocation* table_allocation(
        const std::vector<DescriptorTableAllocation>& allocations,
        const std::uint32_t root_parameter_index
    ) const noexcept {
        const auto found = std::find_if(
            allocations.begin(),
            allocations.end(),
            [&](const DescriptorTableAllocation& allocation) {
                return allocation.root_parameter_index == root_parameter_index;
            }
        );
        return found == allocations.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool create_constant_buffer_view(
        const PendingGraphicsBinding& pending,
        const D3D12_CPU_DESCRIPTOR_HANDLE handle
    ) {
        if (pending.buffer == nullptr || pending.constant_offset % constant_buffer_alignment != 0U) {
            return false;
        }
        const std::uint64_t logical_size = pending.constant_size == 0U
            ? pending.buffer->size() - pending.constant_offset
            : pending.constant_size;
        std::uint64_t view_size = 0;
        if (logical_size == 0U || logical_size > maximum_constant_buffer_size ||
            !aligned_size(logical_size, constant_buffer_alignment, view_size) ||
            view_size > maximum_constant_buffer_size ||
            pending.constant_offset + view_size > pending.buffer->allocation_size()) {
            return false;
        }
        D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
        desc.BufferLocation = pending.buffer->native()->GetGPUVirtualAddress() + pending.constant_offset;
        desc.SizeInBytes = static_cast<UINT>(view_size);
        device_->CreateConstantBufferView(&desc, handle);
        transition(*pending.buffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        return true;
    }

    [[nodiscard]] bool create_buffer_srv(
        const PendingGraphicsBinding& pending,
        const D3D12_CPU_DESCRIPTOR_HANDLE handle
    ) {
        if (pending.buffer == nullptr) {
            return false;
        }
        const std::uint64_t size = pending.view.size == 0U
            ? pending.buffer->size() - pending.view.offset
            : pending.view.size;
        const std::uint64_t stride = pending.view.structure_stride == 0U
            ? 4U
            : pending.view.structure_stride;
        const std::uint64_t first_element = pending.view.offset / stride;
        const std::uint64_t element_count = size / stride;
        if (first_element > std::numeric_limits<UINT>::max() ||
            element_count == 0U || element_count > std::numeric_limits<UINT>::max()) {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = pending.view.structure_stride == 0U ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.FirstElement = first_element;
        desc.Buffer.NumElements = static_cast<UINT>(element_count);
        desc.Buffer.StructureByteStride = pending.view.structure_stride;
        desc.Buffer.Flags = pending.view.structure_stride == 0U
            ? D3D12_BUFFER_SRV_FLAG_RAW
            : D3D12_BUFFER_SRV_FLAG_NONE;
        device_->CreateShaderResourceView(pending.buffer->native(), &desc, handle);
        transition(
            *pending.buffer,
            static_cast<D3D12_RESOURCE_STATES>(
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
            )
        );
        return true;
    }

    [[nodiscard]] bool create_buffer_uav(
        const PendingGraphicsBinding& pending,
        const D3D12_CPU_DESCRIPTOR_HANDLE handle
    ) {
        if (pending.buffer == nullptr) {
            return false;
        }
        const std::uint64_t size = pending.view.size == 0U
            ? pending.buffer->size() - pending.view.offset
            : pending.view.size;
        const std::uint64_t stride = pending.view.structure_stride == 0U
            ? 4U
            : pending.view.structure_stride;
        const std::uint64_t first_element = pending.view.offset / stride;
        const std::uint64_t element_count = size / stride;
        if (first_element > std::numeric_limits<UINT>::max() ||
            element_count == 0U || element_count > std::numeric_limits<UINT>::max()) {
            return false;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = pending.view.structure_stride == 0U ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = first_element;
        desc.Buffer.NumElements = static_cast<UINT>(element_count);
        desc.Buffer.StructureByteStride = pending.view.structure_stride;
        desc.Buffer.CounterOffsetInBytes = 0;
        desc.Buffer.Flags = pending.view.structure_stride == 0U
            ? D3D12_BUFFER_UAV_FLAG_RAW
            : D3D12_BUFFER_UAV_FLAG_NONE;
        device_->CreateUnorderedAccessView(pending.buffer->native(), nullptr, &desc, handle);
        transition(*pending.buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return true;
    }

    [[nodiscard]] static D3D12_SHADER_RESOURCE_VIEW_DESC texture_srv_desc(
        const TextureDesc& texture
    ) noexcept {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = shader_view_format(texture.format);
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        switch (texture.dimension) {
        case TextureDimension::d1:
            if (texture.array_layers > 1U) {
                desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
                desc.Texture1DArray.MostDetailedMip = 0;
                desc.Texture1DArray.MipLevels = texture.mip_levels;
                desc.Texture1DArray.FirstArraySlice = 0;
                desc.Texture1DArray.ArraySize = texture.array_layers;
                desc.Texture1DArray.ResourceMinLODClamp = 0.0F;
            } else {
                desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                desc.Texture1D.MostDetailedMip = 0;
                desc.Texture1D.MipLevels = texture.mip_levels;
                desc.Texture1D.ResourceMinLODClamp = 0.0F;
            }
            break;
        case TextureDimension::d2:
            if (texture.sample_count > 1U) {
                desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
            } else if (texture.array_layers > 1U) {
                desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.MostDetailedMip = 0;
                desc.Texture2DArray.MipLevels = texture.mip_levels;
                desc.Texture2DArray.FirstArraySlice = 0;
                desc.Texture2DArray.ArraySize = texture.array_layers;
                desc.Texture2DArray.PlaneSlice = 0;
                desc.Texture2DArray.ResourceMinLODClamp = 0.0F;
            } else {
                desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MostDetailedMip = 0;
                desc.Texture2D.MipLevels = texture.mip_levels;
                desc.Texture2D.PlaneSlice = 0;
                desc.Texture2D.ResourceMinLODClamp = 0.0F;
            }
            break;
        case TextureDimension::d3:
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            desc.Texture3D.MostDetailedMip = 0;
            desc.Texture3D.MipLevels = texture.mip_levels;
            desc.Texture3D.ResourceMinLODClamp = 0.0F;
            break;
        }
        return desc;
    }

    [[nodiscard]] static D3D12_UNORDERED_ACCESS_VIEW_DESC texture_uav_desc(
        const TextureDesc& texture
    ) noexcept {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = pixel_format(texture.format);
        switch (texture.dimension) {
        case TextureDimension::d1:
            if (texture.array_layers > 1U) {
                desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
                desc.Texture1DArray.MipSlice = 0;
                desc.Texture1DArray.FirstArraySlice = 0;
                desc.Texture1DArray.ArraySize = texture.array_layers;
            } else {
                desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
                desc.Texture1D.MipSlice = 0;
            }
            break;
        case TextureDimension::d2:
            if (texture.array_layers > 1U) {
                desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.MipSlice = 0;
                desc.Texture2DArray.FirstArraySlice = 0;
                desc.Texture2DArray.ArraySize = texture.array_layers;
                desc.Texture2DArray.PlaneSlice = 0;
            } else {
                desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = 0;
                desc.Texture2D.PlaneSlice = 0;
            }
            break;
        case TextureDimension::d3:
            desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            desc.Texture3D.MipSlice = 0;
            desc.Texture3D.FirstWSlice = 0;
            desc.Texture3D.WSize = texture.depth;
            break;
        }
        return desc;
    }

    [[nodiscard]] bool create_texture_srv(
        const PendingGraphicsBinding& pending,
        const D3D12_CPU_DESCRIPTOR_HANDLE handle
    ) {
        if (pending.texture == nullptr) {
            return false;
        }
        const auto desc = texture_srv_desc(pending.texture->desc());
        device_->CreateShaderResourceView(pending.texture->native(), &desc, handle);
        transition(
            *pending.texture,
            static_cast<D3D12_RESOURCE_STATES>(
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
            )
        );
        return true;
    }

    [[nodiscard]] bool create_texture_uav(
        const PendingGraphicsBinding& pending,
        const D3D12_CPU_DESCRIPTOR_HANDLE handle
    ) {
        if (pending.texture == nullptr || pending.texture->format() == TextureFormat::depth32_float) {
            return false;
        }
        const auto desc = texture_uav_desc(pending.texture->desc());
        device_->CreateUnorderedAccessView(pending.texture->native(), nullptr, &desc, handle);
        transition(*pending.texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return true;
    }

    [[nodiscard]] bool write_descriptor(
        const PendingGraphicsBinding& pending,
        const D3D12_CPU_DESCRIPTOR_HANDLE handle
    ) {
        switch (pending.kind) {
        case PendingGraphicsBindingKind::constant_buffer:
            return create_constant_buffer_view(pending, handle);
        case PendingGraphicsBindingKind::shader_buffer:
            return create_buffer_srv(pending, handle);
        case PendingGraphicsBindingKind::storage_buffer:
            return create_buffer_uav(pending, handle);
        case PendingGraphicsBindingKind::texture:
            return create_texture_srv(pending, handle);
        case PendingGraphicsBindingKind::storage_texture:
            return create_texture_uav(pending, handle);
        case PendingGraphicsBindingKind::sampler:
            if (pending.sampler == nullptr) {
                return false;
            }
            device_->CreateSampler(&pending.sampler->native_desc(), handle);
            return true;
        }
        return false;
    }

    [[nodiscard]] bool materialize_graphics_bindings() {
        if (active_pipeline_ == nullptr) {
            return false;
        }
        const auto& root = active_pipeline_->root_layout();
        if (root.bindings.empty()) {
            return true;
        }
        if (!bindings_dirty_) {
            return active_resource_heap_ != nullptr || active_sampler_heap_ != nullptr;
        }

        for (const auto& location : root.bindings) {
            std::uint32_t required_count = 0;
            if (!binding_set_is_complete(location, required_count)) {
                core::log(core::LogLevel::error, "VICE draw is missing one or more required shader resource bindings.");
                return false;
            }
        }

        std::vector<DescriptorTableAllocation> allocations;
        allocations.reserve(root.tables.size());
        std::uint64_t resource_count = 0;
        std::uint64_t sampler_count = 0;
        for (const auto& table : root.tables) {
            std::uint32_t descriptor_count = 0;
            if (!table_descriptor_count(table, descriptor_count)) {
                return false;
            }
            std::uint64_t& total = table.heap_class == D3D12DescriptorHeapClass::samplers
                ? sampler_count
                : resource_count;
            if (total > std::numeric_limits<std::uint32_t>::max() - descriptor_count) {
                return false;
            }
            allocations.push_back(DescriptorTableAllocation{
                .root_parameter_index = table.root_parameter_index,
                .heap_class = table.heap_class,
                .heap_offset = static_cast<std::uint32_t>(total),
                .descriptor_count = descriptor_count,
            });
            total += descriptor_count;
        }

        ComPtr<ID3D12DescriptorHeap> resource_heap;
        ComPtr<ID3D12DescriptorHeap> sampler_heap;
        if (!create_shader_visible_heap(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            static_cast<std::uint32_t>(resource_count),
            resource_heap
        ) || !create_shader_visible_heap(
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
            static_cast<std::uint32_t>(sampler_count),
            sampler_heap
        )) {
            return false;
        }

        for (const auto& pending : pending_bindings_) {
            const auto* location = active_pipeline_->binding_location(pending.key);
            if (location == nullptr) {
                return false;
            }
            const auto* allocation = table_allocation(allocations, location->root_parameter_index);
            if (allocation == nullptr) {
                return false;
            }
            const std::uint64_t descriptor_index =
                static_cast<std::uint64_t>(allocation->heap_offset) +
                static_cast<std::uint64_t>(location->table_offset) +
                static_cast<std::uint64_t>(pending.key.array_element);
            if (descriptor_index >=
                static_cast<std::uint64_t>(allocation->heap_offset) + allocation->descriptor_count) {
                return false;
            }

            ID3D12DescriptorHeap* heap = allocation->heap_class == D3D12DescriptorHeapClass::samplers
                ? sampler_heap.Get()
                : resource_heap.Get();
            if (heap == nullptr) {
                return false;
            }
            D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
            const UINT increment = allocation->heap_class == D3D12DescriptorHeapClass::samplers
                ? sampler_descriptor_size_
                : resource_descriptor_size_;
            handle.ptr += static_cast<SIZE_T>(descriptor_index) * static_cast<SIZE_T>(increment);
            if (!write_descriptor(pending, handle)) {
                return false;
            }
        }

        std::array<ID3D12DescriptorHeap*, 2> heaps{};
        UINT heap_count = 0;
        if (resource_heap != nullptr) {
            heaps[heap_count++] = resource_heap.Get();
        }
        if (sampler_heap != nullptr) {
            heaps[heap_count++] = sampler_heap.Get();
        }
        if (heap_count > 0U) {
            command_list_->SetDescriptorHeaps(heap_count, heaps.data());
        }

        for (const auto& allocation : allocations) {
            ID3D12DescriptorHeap* heap = allocation.heap_class == D3D12DescriptorHeapClass::samplers
                ? sampler_heap.Get()
                : resource_heap.Get();
            if (heap == nullptr) {
                return false;
            }
            D3D12_GPU_DESCRIPTOR_HANDLE handle = heap->GetGPUDescriptorHandleForHeapStart();
            const UINT increment = allocation.heap_class == D3D12DescriptorHeapClass::samplers
                ? sampler_descriptor_size_
                : resource_descriptor_size_;
            handle.ptr += static_cast<UINT64>(allocation.heap_offset) * static_cast<UINT64>(increment);
            command_list_->SetGraphicsRootDescriptorTable(allocation.root_parameter_index, handle);
        }

        active_resource_heap_ = std::move(resource_heap);
        active_sampler_heap_ = std::move(sampler_heap);
        bindings_dirty_ = false;
        return true;
    }

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

    void transition(D3D12Texture& texture, const D3D12_RESOURCE_STATES desired) {
        const auto current = texture.state();
        if (current == desired || (current & desired) == desired) {
            return;
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.native();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = current;
        barrier.Transition.StateAfter = desired;
        command_list_->ResourceBarrier(1, &barrier);
        texture.set_state(desired);
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

    void reset_graphics_bindings() {
        active_pipeline_ = nullptr;
        pending_bindings_.clear();
        active_resource_heap_.Reset();
        active_sampler_heap_.Reset();
        vertex_buffers_bound_.fill(false);
        vertex_capacities_.fill(0U);
        index_buffer_bound_ = false;
        index_capacity_ = 0U;
        bindings_dirty_ = true;
        pipeline_bound_ = false;
    }

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> command_list_;
    std::vector<PendingGraphicsBinding> pending_bindings_;
    std::vector<ComPtr<ID3D12DescriptorHeap>> retained_heaps_;
    ComPtr<ID3D12DescriptorHeap> active_resource_heap_;
    ComPtr<ID3D12DescriptorHeap> active_sampler_heap_;
    std::array<bool, max_vertex_buffers> vertex_buffers_bound_{};
    std::array<std::uint64_t, max_vertex_buffers> vertex_capacities_{};
    D3D12GraphicsPipeline* active_pipeline_ = nullptr;
    D3D12Swapchain* active_swapchain_ = nullptr;
    D3D12Swapchain* present_swapchain_ = nullptr;
    D3D12Texture* active_depth_texture_ = nullptr;
    std::uint64_t index_capacity_ = 0U;
    UINT resource_descriptor_size_ = 0;
    UINT sampler_descriptor_size_ = 0;
    bool index_buffer_bound_ = false;
    bool bindings_dirty_ = true;
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
        std::uint64_t allocation_size = desc.size;
        if (has_usage(desc.usage, BufferUsage::uniform) &&
            !aligned_size(desc.size, constant_buffer_alignment, allocation_size)) {
            return nullptr;
        }
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = heap_type(desc.memory);
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC native_desc{};
        native_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        native_desc.Width = allocation_size;
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
        return std::make_unique<D3D12Buffer>(
            desc,
            std::move(resource),
            allocation_size,
            initial_state
        );
    }

    [[nodiscard]] std::unique_ptr<Texture> create_texture(const TextureDesc& desc) override {
        if (!is_valid(desc)) {
            core::log(core::LogLevel::error, "VICE rejected an invalid texture description.");
            return nullptr;
        }
        if (has_usage(desc.usage, TextureUsage::depth_attachment) &&
            (desc.dimension != TextureDimension::d2 || desc.array_layers != 1U || desc.sample_count != 1U)) {
            core::log(core::LogLevel::error, "Direct3D 12 depth attachments currently require one non-MSAA 2D layer.");
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
        native_desc.Format = texture_resource_format(desc);
        native_desc.SampleDesc.Count = desc.sample_count;
        native_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        native_desc.Flags = texture_flags(desc.usage);

        D3D12_CLEAR_VALUE clear_value{};
        const D3D12_CLEAR_VALUE* optimized_clear = nullptr;
        if (has_usage(desc.usage, TextureUsage::depth_attachment)) {
            clear_value.Format = DXGI_FORMAT_D32_FLOAT;
            clear_value.DepthStencil.Depth = 1.0F;
            clear_value.DepthStencil.Stencil = 0;
            optimized_clear = &clear_value;
        }

        ComPtr<ID3D12Resource> resource;
        if (!succeeded(
            device_->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &native_desc,
                D3D12_RESOURCE_STATE_COMMON, optimized_clear, IID_PPV_ARGS(&resource)
            ),
            "Direct3D 12 could not create a VICE texture."
        )) {
            return nullptr;
        }
        if (const auto name = wide_name(desc.debug_name); !name.empty()) {
            (void)resource->SetName(name.c_str());
        }

        ComPtr<ID3D12DescriptorHeap> dsv_heap;
        if (has_usage(desc.usage, TextureUsage::depth_attachment)) {
            D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
            heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            heap_desc.NumDescriptors = 1;
            heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (!succeeded(
                device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&dsv_heap)),
                "Direct3D 12 could not create a VICE depth descriptor heap."
            )) {
                return nullptr;
            }
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
            dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
            dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
            dsv_desc.Texture2D.MipSlice = 0;
            device_->CreateDepthStencilView(
                resource.Get(),
                &dsv_desc,
                dsv_heap->GetCPUDescriptorHandleForHeapStart()
            );
        }

        return std::make_unique<D3D12Texture>(
            desc,
            device_,
            command_queue_,
            std::move(resource),
            D3D12_RESOURCE_STATE_COMMON,
            std::move(dsv_heap)
        );
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

        std::vector<std::byte> compiled(bytecode->GetBufferSize());
        std::memcpy(compiled.data(), bytecode->GetBufferPointer(), compiled.size());
        return std::make_unique<D3D12ShaderModule>(
            desc.stage,
            std::move(compiled),
            std::vector<ShaderResourceBindingDesc>{desc.resources.begin(), desc.resources.end()}
        );
    }

    [[nodiscard]] std::unique_ptr<ShaderModule> create_shader_module(
        const ShaderBinaryDesc& desc
    ) override {
        if (!is_valid(desc) || desc.format != ShaderBinaryFormat::dxil) {
            core::log(core::LogLevel::error, "Direct3D 12 requires a valid DXIL shader binary.");
            return nullptr;
        }
        return std::make_unique<D3D12ShaderModule>(
            desc.stage,
            std::vector<std::byte>{desc.binary.begin(), desc.binary.end()},
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
        auto* vertex = dynamic_cast<D3D12ShaderModule*>(desc.vertex_shader);
        auto* fragment = dynamic_cast<D3D12ShaderModule*>(desc.fragment_shader);
        if (vertex == nullptr || fragment == nullptr) {
            return nullptr;
        }

        const PipelineLayout layout = compile_graphics_pipeline_layout(
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
        D3D12RootSignatureResult serialized_root = serialize_d3d12_root_signature(layout);
        if (!serialized_root.success) {
            core::log(
                core::LogLevel::error,
                "Direct3D 12 root signature generation failed. " + serialized_root.diagnostics
            );
            return nullptr;
        }

        ComPtr<ID3D12RootSignature> root_signature;
        if (!succeeded(
            device_->CreateRootSignature(
                0,
                serialized_root.blob.data(),
                serialized_root.blob.size(),
                IID_PPV_ARGS(&root_signature)
            ),
            "Direct3D 12 could not create the VICE root signature."
        )) {
            return nullptr;
        }

        std::size_t attribute_count = 0;
        for (const auto& stream : desc.vertex_buffers) {
            attribute_count += stream.attributes.size();
        }
        if (attribute_count > std::numeric_limits<UINT>::max()) {
            return nullptr;
        }

        std::vector<std::string> semantic_names;
        semantic_names.reserve(attribute_count);
        for (const auto& stream : desc.vertex_buffers) {
            for (const auto& attribute : stream.attributes) {
                semantic_names.emplace_back(attribute.semantic);
            }
        }

        std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;
        input_elements.reserve(attribute_count);
        std::vector<D3D12VertexStreamInfo> vertex_streams;
        vertex_streams.reserve(desc.vertex_buffers.size());
        std::size_t semantic_index = 0;
        for (std::size_t slot = 0; slot < desc.vertex_buffers.size(); ++slot) {
            const auto& stream = desc.vertex_buffers[slot];
            vertex_streams.push_back(D3D12VertexStreamInfo{
                .stride = stream.stride,
                .step_mode = stream.step_mode,
                .step_rate = stream.step_rate,
            });
            for (const auto& attribute : stream.attributes) {
                const DXGI_FORMAT format = vertex_format(attribute.format);
                if (format == DXGI_FORMAT_UNKNOWN) {
                    return nullptr;
                }
                D3D12_INPUT_ELEMENT_DESC input{};
                input.SemanticName = semantic_names[semantic_index++].c_str();
                input.SemanticIndex = attribute.semantic_index;
                input.Format = format;
                input.InputSlot = static_cast<UINT>(slot);
                input.AlignedByteOffset = attribute.offset;
                input.InputSlotClass = stream.step_mode == VertexStepMode::per_instance
                    ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                    : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                input.InstanceDataStepRate = stream.step_mode == VertexStepMode::per_instance
                    ? stream.step_rate
                    : 0U;
                input_elements.push_back(input);
            }
        }

        const bool alpha_blend = desc.blend == BlendMode::alpha;
        D3D12_RENDER_TARGET_BLEND_DESC blend{};
        blend.BlendEnable = alpha_blend ? TRUE : FALSE;
        blend.LogicOpEnable = FALSE;
        blend.SrcBlend = alpha_blend ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_ONE;
        blend.DestBlend = alpha_blend ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_ZERO;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = alpha_blend ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_ZERO;
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
        pipeline_desc.DepthStencilState.DepthEnable = desc.depth.test_enabled ? TRUE : FALSE;
        pipeline_desc.DepthStencilState.DepthWriteMask = desc.depth.write_enabled
            ? D3D12_DEPTH_WRITE_MASK_ALL
            : D3D12_DEPTH_WRITE_MASK_ZERO;
        pipeline_desc.DepthStencilState.DepthFunc = comparison_func(desc.depth.compare);
        pipeline_desc.DepthStencilState.StencilEnable = FALSE;
        pipeline_desc.InputLayout = {
            input_elements.empty() ? nullptr : input_elements.data(),
            static_cast<UINT>(input_elements.size())
        };
        pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline_desc.NumRenderTargets = 1;
        pipeline_desc.RTVFormats[0] = pixel_format(desc.color_format);
        pipeline_desc.DSVFormat = pixel_format(desc.depth.format);
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
            desc.topology,
            std::move(serialized_root),
            std::move(vertex_streams),
            desc.depth
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
