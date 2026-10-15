#include <vice/render/d3d12_root_signature.hpp>

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cassert>

int main() {
    using namespace vice::render;
    using Microsoft::WRL::ComPtr;

    constexpr ShaderResourceBindingDesc shared_frame_data{
        .register_class = ShaderRegisterClass::constant_buffer,
        .binding = 2,
        .space = 1,
        .count = 1,
        .unbounded = false,
    };
    constexpr ShaderResourceBindingDesc vertex_textures{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 4,
        .space = 2,
        .count = 6,
        .unbounded = false,
    };
    constexpr ShaderResourceBindingDesc fragment_sampler{
        .register_class = ShaderRegisterClass::sampler,
        .binding = 3,
        .space = 1,
        .count = 1,
        .unbounded = false,
    };
    constexpr ShaderResourceBindingDesc fragment_bindless{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 32,
        .space = 5,
        .count = 0,
        .unbounded = true,
    };

    constexpr std::array vertex_resources{
        shared_frame_data,
        vertex_textures,
    };
    constexpr std::array fragment_resources{
        shared_frame_data,
        fragment_sampler,
        fragment_bindless,
    };

    const PipelineLayout layout = compile_graphics_pipeline_layout(
        vertex_resources,
        fragment_resources
    );
    assert(layout.valid);

    const D3D12RootSignatureResult serialized = serialize_d3d12_root_signature(layout);
    assert(serialized.success);
    assert(serialized.diagnostics.empty());
    assert(!serialized.blob.empty());
    assert(serialized.descriptor_table_count == 4U);
    assert(serialized.descriptor_range_count == 4U);

    ComPtr<ID3D12RootSignatureDeserializer> deserializer;
    const HRESULT result = D3D12CreateRootSignatureDeserializer(
        serialized.blob.data(),
        serialized.blob.size(),
        IID_PPV_ARGS(&deserializer)
    );
    assert(SUCCEEDED(result));
    assert(deserializer != nullptr);

    const D3D12_ROOT_SIGNATURE_DESC* root = deserializer->GetRootSignatureDesc();
    assert(root != nullptr);
    assert(root->NumParameters == 4U);
    assert(root->NumStaticSamplers == 0U);

    bool saw_shared_cbv = false;
    bool saw_vertex_srv = false;
    bool saw_fragment_sampler = false;
    bool saw_fragment_unbounded = false;

    for (UINT parameter_index = 0; parameter_index < root->NumParameters; ++parameter_index) {
        const auto& parameter = root->pParameters[parameter_index];
        assert(parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
        assert(parameter.DescriptorTable.NumDescriptorRanges == 1U);
        const auto& range = parameter.DescriptorTable.pDescriptorRanges[0];

        if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV &&
            range.BaseShaderRegister == 2U && range.RegisterSpace == 1U) {
            assert(range.NumDescriptors == 1U);
            assert(parameter.ShaderVisibility == D3D12_SHADER_VISIBILITY_ALL);
            saw_shared_cbv = true;
        } else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV &&
            range.BaseShaderRegister == 4U && range.RegisterSpace == 2U) {
            assert(range.NumDescriptors == 6U);
            assert(parameter.ShaderVisibility == D3D12_SHADER_VISIBILITY_VERTEX);
            saw_vertex_srv = true;
        } else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER &&
            range.BaseShaderRegister == 3U && range.RegisterSpace == 1U) {
            assert(range.NumDescriptors == 1U);
            assert(parameter.ShaderVisibility == D3D12_SHADER_VISIBILITY_PIXEL);
            saw_fragment_sampler = true;
        } else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV &&
            range.BaseShaderRegister == 32U && range.RegisterSpace == 5U) {
            assert(range.NumDescriptors == UINT_MAX);
            assert(parameter.ShaderVisibility == D3D12_SHADER_VISIBILITY_PIXEL);
            saw_fragment_unbounded = true;
        }
    }

    assert(saw_shared_cbv);
    assert(saw_vertex_srv);
    assert(saw_fragment_sampler);
    assert(saw_fragment_unbounded);

    PipelineLayout invalid_layout;
    invalid_layout.valid = false;
    invalid_layout.diagnostics = "expected failure";
    const D3D12RootSignatureResult invalid = serialize_d3d12_root_signature(invalid_layout);
    assert(!invalid.success);
    assert(invalid.blob.empty());
    assert(invalid.diagnostics == "expected failure");

    return 0;
}
