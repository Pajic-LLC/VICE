#include "compute_pipeline.hpp"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

int main() {
    using Microsoft::WRL::ComPtr;
    using namespace vice::render;
    using namespace vice::render::d3d12_detail;

    ComPtr<IDXGIFactory4> factory;
    CHECK(SUCCEEDED(CreateDXGIFactory2(0U, IID_PPV_ARGS(&factory))));

    ComPtr<IDXGIAdapter> warp_adapter;
    CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter))));

    ComPtr<ID3D12Device> device;
    CHECK(SUCCEEDED(D3D12CreateDevice(
        warp_adapter.Get(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&device)
    )));
    CHECK(device != nullptr);

    constexpr const char* source = R"HLSL(
RWStructuredBuffer<uint> OutputBuffer : register(u7, space3);

[numthreads(4, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
    OutputBuffer[dispatch_id.x] = 0xCAFE0000u + dispatch_id.x;
}
)HLSL";

    ComPtr<ID3DBlob> shader;
    ComPtr<ID3DBlob> errors;
    const HRESULT compile_status = D3DCompile(
        source,
        sizeof(R"HLSL(
RWStructuredBuffer<uint> OutputBuffer : register(u7, space3);

[numthreads(4, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
    OutputBuffer[dispatch_id.x] = 0xCAFE0000u + dispatch_id.x;
}
)HLSL") - 1U,
        "vice_compute_pipeline_test",
        nullptr,
        nullptr,
        "main",
        "cs_5_1",
        D3DCOMPILE_ENABLE_STRICTNESS,
        0U,
        &shader,
        &errors
    );
    CHECK(SUCCEEDED(compile_status));
    CHECK(shader != nullptr);

    constexpr std::array resources{
        ShaderResourceBindingDesc{
            .register_class = ShaderRegisterClass::unordered_access,
            .binding = 7U,
            .space = 3U,
            .count = 1U,
            .unbounded = false,
        },
    };

    const D3D12_SHADER_BYTECODE bytecode{
        .pShaderBytecode = shader->GetBufferPointer(),
        .BytecodeLength = shader->GetBufferSize(),
    };
    const ComputePipelineBuildResult pipeline = build_compute_pipeline(
        *device,
        bytecode,
        resources
    );
    CHECK(pipeline.success);
    CHECK(pipeline.diagnostics.empty());
    CHECK(pipeline.root_signature != nullptr);
    CHECK(pipeline.pipeline_state != nullptr);
    CHECK(pipeline.root_layout.success);
    CHECK(pipeline.root_layout.bindings.size() == 1U);

    const ShaderBindingKey output_key{
        .register_class = ShaderRegisterClass::unordered_access,
        .binding = 7U,
        .space = 3U,
        .array_element = 0U,
    };
    const D3D12BindingLocation* output_location = find_d3d12_binding_location(
        pipeline.root_layout,
        output_key
    );
    CHECK(output_location != nullptr);

    constexpr UINT element_count = 4U;
    constexpr UINT64 buffer_size = element_count * sizeof(std::uint32_t);

    D3D12_RESOURCE_DESC buffer_desc{};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = buffer_size;
    buffer_desc.Height = 1U;
    buffer_desc.DepthOrArraySize = 1U;
    buffer_desc.MipLevels = 1U;
    buffer_desc.SampleDesc.Count = 1U;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES default_heap{};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    default_heap.CreationNodeMask = 1U;
    default_heap.VisibleNodeMask = 1U;

    ComPtr<ID3D12Resource> output;
    CHECK(SUCCEEDED(device->CreateCommittedResource(
        &default_heap,
        D3D12_HEAP_FLAG_NONE,
        &buffer_desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&output)
    )));

    D3D12_RESOURCE_DESC readback_desc = buffer_desc;
    readback_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    readback_heap.CreationNodeMask = 1U;
    readback_heap.VisibleNodeMask = 1U;

    ComPtr<ID3D12Resource> readback;
    CHECK(SUCCEEDED(device->CreateCommittedResource(
        &readback_heap,
        D3D12_HEAP_FLAG_NONE,
        &readback_desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readback)
    )));

    D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc{};
    descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptor_heap_desc.NumDescriptors = 1U;
    descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ComPtr<ID3D12DescriptorHeap> descriptor_heap;
    CHECK(SUCCEEDED(device->CreateDescriptorHeap(
        &descriptor_heap_desc,
        IID_PPV_ARGS(&descriptor_heap)
    )));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
    uav_desc.Format = DXGI_FORMAT_UNKNOWN;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.FirstElement = 0U;
    uav_desc.Buffer.NumElements = element_count;
    uav_desc.Buffer.StructureByteStride = sizeof(std::uint32_t);
    uav_desc.Buffer.CounterOffsetInBytes = 0U;
    uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(
        output.Get(),
        nullptr,
        &uav_desc,
        descriptor_heap->GetCPUDescriptorHandleForHeapStart()
    );

    ComPtr<ID3D12CommandAllocator> allocator;
    CHECK(SUCCEEDED(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&allocator)
    )));

    ComPtr<ID3D12GraphicsCommandList> commands;
    CHECK(SUCCEEDED(device->CreateCommandList(
        0U,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator.Get(),
        nullptr,
        IID_PPV_ARGS(&commands)
    )));

    ID3D12DescriptorHeap* heaps[] = {descriptor_heap.Get()};
    commands->SetDescriptorHeaps(1U, heaps);
    commands->SetComputeRootSignature(pipeline.root_signature.Get());
    commands->SetPipelineState(pipeline.pipeline_state.Get());
    commands->SetComputeRootDescriptorTable(
        output_location->root_parameter_index,
        descriptor_heap->GetGPUDescriptorHandleForHeapStart()
    );
    commands->Dispatch(1U, 1U, 1U);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = output.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1U, &barrier);
    commands->CopyBufferRegion(readback.Get(), 0U, output.Get(), 0U, buffer_size);
    CHECK(SUCCEEDED(commands->Close()));

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    CHECK(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))));
    ID3D12CommandList* submitted[] = {commands.Get()};
    queue->ExecuteCommandLists(1U, submitted);

    ComPtr<ID3D12Fence> fence;
    CHECK(SUCCEEDED(device->CreateFence(0U, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    CHECK(event != nullptr);
    CHECK(SUCCEEDED(queue->Signal(fence.Get(), 1U)));
    if (fence->GetCompletedValue() < 1U) {
        CHECK(SUCCEEDED(fence->SetEventOnCompletion(1U, event)));
        CHECK(WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0);
    }
    CloseHandle(event);

    void* mapped = nullptr;
    const D3D12_RANGE read_range{0U, static_cast<SIZE_T>(buffer_size)};
    CHECK(SUCCEEDED(readback->Map(0U, &read_range, &mapped)));
    CHECK(mapped != nullptr);
    const auto* values = static_cast<const std::uint32_t*>(mapped);
    for (std::uint32_t index = 0U; index < element_count; ++index) {
        CHECK(values[index] == 0xCAFE0000U + index);
    }
    const D3D12_RANGE written_range{0U, 0U};
    readback->Unmap(0U, &written_range);

    const ComputePipelineBuildResult empty_shader = build_compute_pipeline(
        *device,
        D3D12_SHADER_BYTECODE{},
        resources
    );
    CHECK(!empty_shader.success);
    CHECK(!empty_shader.diagnostics.empty());

    return 0;
}

#undef CHECK
