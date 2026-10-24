#pragma once

#include <vice/render/rhi.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vice::render {

enum class ShaderStageMask : std::uint8_t {
    none = 0,
    vertex = 1U << 0U,
    fragment = 1U << 1U,
    compute = 1U << 2U,
};

[[nodiscard]] constexpr ShaderStageMask operator|(
    const ShaderStageMask left,
    const ShaderStageMask right
) noexcept {
    return static_cast<ShaderStageMask>(
        static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right)
    );
}

[[nodiscard]] constexpr ShaderStageMask operator&(
    const ShaderStageMask left,
    const ShaderStageMask right
) noexcept {
    return static_cast<ShaderStageMask>(
        static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right)
    );
}

constexpr ShaderStageMask& operator|=(ShaderStageMask& left, const ShaderStageMask right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool has_stage(
    const ShaderStageMask value,
    const ShaderStageMask stage
) noexcept {
    return (value & stage) == stage;
}

struct PipelineResourceBinding {
    ShaderRegisterClass register_class = ShaderRegisterClass::shader_resource;
    std::uint32_t binding = 0;
    std::uint32_t space = 0;
    std::uint32_t count = 1;
    bool unbounded = false;
    ShaderResourceKind kind = ShaderResourceKind::generic;
    ShaderStageMask stages = ShaderStageMask::none;
};

struct PipelineLayout {
    bool valid = false;
    std::vector<PipelineResourceBinding> resources;
    std::string diagnostics;
};

[[nodiscard]] PipelineLayout compile_graphics_pipeline_layout(
    std::span<const ShaderResourceBindingDesc> vertex_resources,
    std::span<const ShaderResourceBindingDesc> fragment_resources
);

[[nodiscard]] PipelineLayout compile_compute_pipeline_layout(
    std::span<const ShaderResourceBindingDesc> compute_resources
);

} // namespace vice::render
