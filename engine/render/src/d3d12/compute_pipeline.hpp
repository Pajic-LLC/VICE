#pragma once

#include <vice/render/d3d12_root_signature.hpp>

#include <d3d12.h>
#include <wrl/client.h>

#include <span>
#include <string>

namespace vice::render::d3d12_detail {

struct ComputePipelineBuildResult {
    bool success = false;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state;
    D3D12RootSignatureResult root_layout;
    std::string diagnostics;
};

[[nodiscard]] ComputePipelineBuildResult build_compute_pipeline(
    ID3D12Device& device,
    D3D12_SHADER_BYTECODE shader,
    std::span<const ShaderResourceBindingDesc> resources
);

} // namespace vice::render::d3d12_detail
