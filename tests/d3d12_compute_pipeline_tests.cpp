#include "compute_pipeline.hpp"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <string>

int main() {
    using Microsoft::WRL::ComPtr;
    using namespace vice::render;
    using namespace vice::render::d3d12_detail;

    ComPtr<IDXGIFactory4> factory;
    assert(SUCCEEDED(CreateDXGIFactory2(0U, IID_PPV_ARGS(&factory))));

    ComPtr<IDXGIAdapter> warp_adapter;
    assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter))));

    ComPtr<ID3D12Device> device;
    assert(SUCCEEDED(D3D12CreateDevice(
        warp_adapter.Get(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&device)
    )));
    assert(device != nullptr);

    constexpr const char* source = R"HLSL(
cbuffer FrameData : register(b2, space1) {
    uint BaseValue;
};

RWStructuredBuffer<uint> OutputBuffer : register(u7, space3);

[numthreads(1, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
    OutputBuffer[dispatch_id.x] = BaseValue + dispatch_id.x;
}
)HLSL";

    ComPtr<ID3DBlob> shader;
    ComPtr<ID3DBlob> errors;
    const HRESULT compile_status = D3DCompile(
        source,
        std::char_traits<char>::length(source),
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
    assert(SUCCEEDED(compile_status));
    assert(shader != nullptr);

    constexpr std::array resources{
        ShaderResourceBindingDesc{
            .register_class = ShaderRegisterClass::constant_buffer,
            .binding = 2U,
            .space = 1U,
            .count = 1U,
            .unbounded = false,
        },
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
    const ComputePipelineBuildResult result = build_compute_pipeline(
        *device,
        bytecode,
        resources
    );
    assert(result.success);
    assert(result.diagnostics.empty());
    assert(result.root_signature != nullptr);
    assert(result.pipeline_state != nullptr);
    assert(result.root_layout.success);
    assert(result.root_layout.bindings.size() == 2U);

    const ShaderBindingKey frame_key{
        .register_class = ShaderRegisterClass::constant_buffer,
        .binding = 2U,
        .space = 1U,
        .array_element = 0U,
    };
    const ShaderBindingKey output_key{
        .register_class = ShaderRegisterClass::unordered_access,
        .binding = 7U,
        .space = 3U,
        .array_element = 0U,
    };
    assert(find_d3d12_binding_location(result.root_layout, frame_key) != nullptr);
    assert(find_d3d12_binding_location(result.root_layout, output_key) != nullptr);

    const ComputePipelineBuildResult empty_shader = build_compute_pipeline(
        *device,
        D3D12_SHADER_BYTECODE{},
        resources
    );
    assert(!empty_shader.success);
    assert(!empty_shader.diagnostics.empty());

    return 0;
}
