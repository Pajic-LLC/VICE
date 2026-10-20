#include <vice/render/compute.hpp>
#include <vice/render/rhi.hpp>

namespace vice::render {

bool is_valid(const ComputePipelineDesc& desc) noexcept {
    return desc.compute_shader != nullptr &&
        desc.compute_shader->stage() == ShaderStage::compute;
}

} // namespace vice::render
