#pragma once

#import <Metal/Metal.h>

#include <vice/render/pipeline_layout.hpp>
#include <vice/render/rhi.hpp>

#include <span>
#include <string>
#include <vector>

namespace vice::render::metal_detail {

struct ComputePipelineBuildResult {
    bool success = false;
    id<MTLComputePipelineState> pipeline_state = nil;
    PipelineLayout layout;
    std::vector<MetalShaderResourceLocationDesc> locations;
    std::string diagnostics;
};

[[nodiscard]] ComputePipelineBuildResult build_compute_pipeline(
    id<MTLDevice> device,
    id<MTLFunction> function,
    std::span<const ShaderResourceBindingDesc> resources,
    std::span<const MetalShaderResourceLocationDesc> locations
);

} // namespace vice::render::metal_detail
