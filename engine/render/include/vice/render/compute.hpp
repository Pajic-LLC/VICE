#pragma once

#include <cstdint>
#include <string_view>

namespace vice::render {

class ShaderModule;

class ComputePipeline {
public:
    virtual ~ComputePipeline() = default;
};

struct ComputePipelineDesc {
    ShaderModule* compute_shader = nullptr;
    std::string_view debug_name;
};

[[nodiscard]] bool is_valid(const ComputePipelineDesc& desc) noexcept;

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
