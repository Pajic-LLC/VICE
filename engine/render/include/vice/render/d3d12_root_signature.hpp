#pragma once

#include <vice/render/pipeline_layout.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vice::render {

enum class D3D12DescriptorHeapClass : std::uint8_t {
    resources,
    samplers,
};

struct D3D12DescriptorTableLayout {
    std::uint32_t root_parameter_index = 0;
    D3D12DescriptorHeapClass heap_class = D3D12DescriptorHeapClass::resources;
    ShaderStageMask stages = ShaderStageMask::none;
    std::uint32_t bounded_descriptor_count = 0;
    bool unbounded = false;
};

struct D3D12BindingLocation {
    ShaderRegisterClass register_class = ShaderRegisterClass::shader_resource;
    std::uint32_t binding = 0;
    std::uint32_t space = 0;
    std::uint32_t count = 1;
    bool unbounded = false;
    std::uint32_t root_parameter_index = 0;
    std::uint32_t table_offset = 0;
};

struct D3D12RootSignatureResult {
    bool success = false;
    std::vector<std::byte> blob;
    std::vector<D3D12DescriptorTableLayout> tables;
    std::vector<D3D12BindingLocation> bindings;
    std::size_t descriptor_table_count = 0;
    std::size_t descriptor_range_count = 0;
    std::string diagnostics;
};

[[nodiscard]] D3D12RootSignatureResult serialize_d3d12_root_signature(
    const PipelineLayout& layout
);

[[nodiscard]] const D3D12BindingLocation* find_d3d12_binding_location(
    const D3D12RootSignatureResult& root,
    const ShaderBindingKey& binding
) noexcept;

} // namespace vice::render
