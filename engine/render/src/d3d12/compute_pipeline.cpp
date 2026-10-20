#include "compute_pipeline.hpp"

#include <vice/render/pipeline_layout.hpp>

namespace vice::render::d3d12_detail {

ComputePipelineBuildResult build_compute_pipeline(
    ID3D12Device& device,
    const D3D12_SHADER_BYTECODE shader,
    const std::span<const ShaderResourceBindingDesc> resources
) {
    ComputePipelineBuildResult result;
    if (shader.pShaderBytecode == nullptr || shader.BytecodeLength == 0U) {
        result.diagnostics = "Direct3D 12 compute pipeline requires non-empty shader bytecode.";
        return result;
    }

    const PipelineLayout layout = compile_compute_pipeline_layout(resources);
    if (!layout.valid) {
        result.diagnostics = layout.diagnostics.empty()
            ? "VICE rejected the compute pipeline resource layout."
            : "VICE rejected the compute pipeline resource layout. " + layout.diagnostics;
        return result;
    }

    result.root_layout = serialize_d3d12_root_signature(layout);
    if (!result.root_layout.success) {
        result.diagnostics = result.root_layout.diagnostics.empty()
            ? "Direct3D 12 could not serialize the compute root signature."
            : "Direct3D 12 could not serialize the compute root signature. " +
                result.root_layout.diagnostics;
        return result;
    }

    const HRESULT root_status = device.CreateRootSignature(
        0U,
        result.root_layout.blob.data(),
        result.root_layout.blob.size(),
        IID_PPV_ARGS(&result.root_signature)
    );
    if (FAILED(root_status) || result.root_signature == nullptr) {
        result.diagnostics = "Direct3D 12 could not create the compute root signature.";
        result.root_signature.Reset();
        return result;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
    pipeline_desc.pRootSignature = result.root_signature.Get();
    pipeline_desc.CS = shader;

    const HRESULT pipeline_status = device.CreateComputePipelineState(
        &pipeline_desc,
        IID_PPV_ARGS(&result.pipeline_state)
    );
    if (FAILED(pipeline_status) || result.pipeline_state == nullptr) {
        result.diagnostics = "Direct3D 12 could not create the compute pipeline state.";
        result.root_signature.Reset();
        result.pipeline_state.Reset();
        return result;
    }

    result.success = true;
    return result;
}

} // namespace vice::render::d3d12_detail
