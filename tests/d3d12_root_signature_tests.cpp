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
    assert(serialized.tables.size() == 4U);
    assert(serialized.bindings.size() == 4U);

    const ShaderBindingKey cbv_key{
        .register_class = ShaderRegisterClass::constant_buffer,
        .binding = 2,
        .space = 1,
        .array_element = 0,
    };
    const auto* cbv_location = find_d3d12_binding_location(serialized, cbv_key);
    assert(cbv_location != nullptr);
    assert(cbv_location->count == 1U);
    assert(!cbv_location->unbounded);
    assert(cbv_location->table_offset == 0U);

    const ShaderBindingKey texture_element{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 4,
        .space = 2,
        .array_element = 5,
    };
    const auto* texture_location = find_d3d12_binding_location(serialized, texture_element);
    assert(texture_location != nullptr);
    assert(texture_location->count == 6U);
    assert(texture_location->table_offset == 0U);

    const ShaderBindingKey texture_out_of_range{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 4,
        .space = 2,
        .array_element = 6,
    };
    assert(find_d3d12_binding_location(serialized, texture_out_of_range) == nullptr);

    const ShaderBindingKey bindless_element{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 32,
        .space = 5,
        .array_element = 4096,
    };
    const auto* bindless_location = find_d3d12_binding_location(serialized, bindless_element);
    assert(bindless_location != nullptr);
    assert(bindless_location->unbounded);
    assert(bindless_location->table_offset == 0U);

    const ShaderBindingKey wrong_class{
        .register_class = ShaderRegisterClass::unordered_access,
        .binding = 32,
        .space = 5,
        .array_element = 0,
    };
    assert(find_d3d12_binding_location(serialized, wrong_class) == nullptr);

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

    for (const auto& table : serialized.tables) {
        if (table.root_parameter_index == cbv_location->root_parameter_index) {
            assert(table.heap_class == D3D12DescriptorHeapClass::resources);
            assert(table.bounded_descriptor_count == 1U);
            assert(!table.unbounded);
        }
        if (table.root_parameter_index == bindless_location->root_parameter_index) {
            assert(table.heap_class == D3D12DescriptorHeapClass::resources);
            assert(table.bounded_descriptor_count == 0U);
            assert(table.unbounded);
        }
    }

    constexpr ShaderResourceBindingDesc compute_constants{
        .register_class = ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    };
    constexpr ShaderResourceBindingDesc compute_input{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    };
    constexpr ShaderResourceBindingDesc compute_output{
        .register_class = ShaderRegisterClass::unordered_access,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    };
    constexpr ShaderResourceBindingDesc compute_tlas{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 5,
        .space = 2,
        .count = 1,
        .unbounded = false,
        .kind = ShaderResourceKind::acceleration_structure,
    };
    constexpr std::array compute_resources{
        compute_constants,
        compute_input,
        compute_output,
        compute_tlas,
    };
    const PipelineLayout compute_layout = compile_compute_pipeline_layout(compute_resources);
    assert(compute_layout.valid);

    const D3D12RootSignatureResult compute_root = serialize_d3d12_root_signature(compute_layout);
    assert(compute_root.success);
    assert(compute_root.bindings.size() == 4U);
    assert(compute_root.tables.size() == 3U);

    ComPtr<ID3D12RootSignatureDeserializer> compute_deserializer;
    const HRESULT compute_result = D3D12CreateRootSignatureDeserializer(
        compute_root.blob.data(),
        compute_root.blob.size(),
        IID_PPV_ARGS(&compute_deserializer)
    );
    assert(SUCCEEDED(compute_result));
    assert(compute_deserializer != nullptr);
    const D3D12_ROOT_SIGNATURE_DESC* compute_desc = compute_deserializer->GetRootSignatureDesc();
    assert(compute_desc != nullptr);
    assert(compute_desc->NumParameters == 3U);
    for (UINT parameter_index = 0; parameter_index < compute_desc->NumParameters; ++parameter_index) {
        const auto& parameter = compute_desc->pParameters[parameter_index];
        assert(parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
        assert(parameter.ShaderVisibility == D3D12_SHADER_VISIBILITY_ALL);
    }

    const ShaderBindingKey compute_uav_key{
        .register_class = ShaderRegisterClass::unordered_access,
        .binding = 0,
        .space = 0,
        .array_element = 0,
    };
    assert(find_d3d12_binding_location(compute_root, compute_uav_key) != nullptr);

    const ShaderBindingKey compute_tlas_key{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 5,
        .space = 2,
        .array_element = 0,
    };
    const auto* compute_tlas_location = find_d3d12_binding_location(compute_root, compute_tlas_key);
    assert(compute_tlas_location != nullptr);
    assert(compute_tlas_location->kind == ShaderResourceKind::acceleration_structure);
    assert(compute_tlas_location->register_class == ShaderRegisterClass::shader_resource);

    PipelineLayout invalid_layout;
    invalid_layout.valid = false;
    invalid_layout.diagnostics = "expected failure";
    const D3D12RootSignatureResult invalid = serialize_d3d12_root_signature(invalid_layout);
    assert(!invalid.success);
    assert(invalid.blob.empty());
    assert(invalid.tables.empty());
    assert(invalid.bindings.empty());
    assert(invalid.diagnostics == "expected failure");

    return 0;
}
