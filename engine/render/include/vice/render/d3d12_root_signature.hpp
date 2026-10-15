#pragma once

#include <vice/render/pipeline_layout.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace vice::render {

struct D3D12RootSignatureResult {
    bool success = false;
    std::vector<std::byte> blob;
    std::size_t descriptor_table_count = 0;
    std::size_t descriptor_range_count = 0;
    std::string diagnostics;
};

[[nodiscard]] D3D12RootSignatureResult serialize_d3d12_root_signature(
    const PipelineLayout& layout
);

} // namespace vice::render
