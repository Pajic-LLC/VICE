#include <vice/render/d3d12_root_signature.hpp>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace vice::render {
namespace {

using Microsoft::WRL::ComPtr;

struct DescriptorTableGroup {
    bool sampler = false;
    ShaderStageMask stages = ShaderStageMask::none;
    bool unbounded = false;
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
};

[[nodiscard]] D3D12_DESCRIPTOR_RANGE_TYPE range_type(
    const ShaderRegisterClass register_class
) noexcept {
    switch (register_class) {
    case ShaderRegisterClass::constant_buffer: return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    case ShaderRegisterClass::shader_resource: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    case ShaderRegisterClass::unordered_access: return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    case ShaderRegisterClass::sampler: return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    }
    return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
}

[[nodiscard]] D3D12_SHADER_VISIBILITY shader_visibility(const ShaderStageMask stages) noexcept {
    if (stages == ShaderStageMask::vertex) {
        return D3D12_SHADER_VISIBILITY_VERTEX;
    }
    if (stages == ShaderStageMask::fragment) {
        return D3D12_SHADER_VISIBILITY_PIXEL;
    }
    return D3D12_SHADER_VISIBILITY_ALL;
}

[[nodiscard]] D3D12_DESCRIPTOR_RANGE make_range(const PipelineResourceBinding& resource) noexcept {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = range_type(resource.register_class);
    range.NumDescriptors = resource.unbounded ? UINT_MAX : resource.count;
    range.BaseShaderRegister = resource.binding;
    range.RegisterSpace = resource.space;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    return range;
}

[[nodiscard]] DescriptorTableGroup* find_bounded_group(
    std::vector<DescriptorTableGroup>& groups,
    const bool sampler,
    const ShaderStageMask stages
) {
    const auto found = std::find_if(
        groups.begin(),
        groups.end(),
        [&](const DescriptorTableGroup& group) {
            return !group.unbounded && group.sampler == sampler && group.stages == stages;
        }
    );
    return found == groups.end() ? nullptr : &*found;
}

[[nodiscard]] std::string blob_message(ID3DBlob* blob) {
    if (blob == nullptr || blob->GetBufferPointer() == nullptr || blob->GetBufferSize() == 0U) {
        return {};
    }
    return std::string{
        static_cast<const char*>(blob->GetBufferPointer()),
        blob->GetBufferSize()
    };
}

} // namespace

D3D12RootSignatureResult serialize_d3d12_root_signature(const PipelineLayout& layout) {
    D3D12RootSignatureResult result;
    if (!layout.valid) {
        result.diagnostics = layout.diagnostics.empty()
            ? "VICE cannot serialize an invalid pipeline layout."
            : layout.diagnostics;
        return result;
    }

    std::vector<DescriptorTableGroup> groups;
    groups.reserve(layout.resources.size());

    for (const auto& resource : layout.resources) {
        const bool sampler = resource.register_class == ShaderRegisterClass::sampler;
        if (resource.unbounded) {
            DescriptorTableGroup group;
            group.sampler = sampler;
            group.stages = resource.stages;
            group.unbounded = true;
            group.ranges.push_back(make_range(resource));
            groups.push_back(std::move(group));
            continue;
        }

        DescriptorTableGroup* group = find_bounded_group(groups, sampler, resource.stages);
        if (group == nullptr) {
            DescriptorTableGroup new_group;
            new_group.sampler = sampler;
            new_group.stages = resource.stages;
            groups.push_back(std::move(new_group));
            group = &groups.back();
        }
        group->ranges.push_back(make_range(resource));
    }

    std::vector<D3D12_ROOT_PARAMETER> parameters;
    parameters.reserve(groups.size());
    for (const auto& group : groups) {
        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.ShaderVisibility = shader_visibility(group.stages);
        parameter.DescriptorTable.NumDescriptorRanges =
            static_cast<UINT>(group.ranges.size());
        parameter.DescriptorTable.pDescriptorRanges = group.ranges.data();
        parameters.push_back(parameter);
        result.descriptor_range_count += group.ranges.size();
    }

    D3D12_ROOT_SIGNATURE_DESC root{};
    root.NumParameters = static_cast<UINT>(parameters.size());
    root.pParameters = parameters.empty() ? nullptr : parameters.data();
    root.NumStaticSamplers = 0;
    root.pStaticSamplers = nullptr;
    root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    const HRESULT status = D3D12SerializeRootSignature(
        &root,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized,
        &errors
    );
    if (FAILED(status) || serialized == nullptr) {
        result.diagnostics = blob_message(errors.Get());
        if (result.diagnostics.empty()) {
            result.diagnostics = "Direct3D 12 could not serialize the VICE root signature.";
        }
        return result;
    }

    const auto* begin = static_cast<const std::byte*>(serialized->GetBufferPointer());
    result.blob.assign(begin, begin + serialized->GetBufferSize());
    result.descriptor_table_count = groups.size();
    result.success = true;
    return result;
}

} // namespace vice::render
