#pragma once

#include <vice/render/rhi.hpp>

#include <cstdint>
#include <string_view>

namespace vice::render {

struct ComputePipelineDesc {
    ShaderModule* compute_shader = nullptr;
    std::string_view debug_name;
};

[[nodiscard]] inline bool is_valid(const ComputePipelineDesc& desc) noexcept {
    return desc.compute_shader != nullptr &&
        desc.compute_shader->stage() == ShaderStage::compute;
}

struct ComputeDispatchDesc {
    std::uint32_t group_count_x = 1U;
    std::uint32_t group_count_y = 1U;
    std::uint32_t group_count_z = 1U;
};

[[nodiscard]] constexpr bool is_valid(const ComputeDispatchDesc& desc) noexcept {
    return desc.group_count_x > 0U &&
        desc.group_count_y > 0U &&
        desc.group_count_z > 0U;
}

} // namespace vice::render
