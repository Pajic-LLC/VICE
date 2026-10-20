#pragma once

#include <cstdint>
#include <string_view>

namespace vice::render {

class ShaderModule;

struct ComputeThreadGroupSize {
    std::uint32_t x = 1U;
    std::uint32_t y = 1U;
    std::uint32_t z = 1U;
};

[[nodiscard]] constexpr bool is_valid(const ComputeThreadGroupSize& size) noexcept {
    return size.x > 0U && size.y > 0U && size.z > 0U;
}

[[nodiscard]] constexpr std::uint64_t thread_count(
    const ComputeThreadGroupSize& size
) noexcept {
    return static_cast<std::uint64_t>(size.x) *
        static_cast<std::uint64_t>(size.y) *
        static_cast<std::uint64_t>(size.z);
}

class ComputePipeline {
public:
    virtual ~ComputePipeline() = default;
};

struct ComputePipelineDesc {
    ShaderModule* compute_shader = nullptr;
    ComputeThreadGroupSize thread_group_size{};
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
