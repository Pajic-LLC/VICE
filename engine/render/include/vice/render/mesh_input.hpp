#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace vice::render {

inline constexpr std::uint32_t max_vertex_buffers = 16;
inline constexpr std::uint32_t max_vertex_attributes = 16;

enum class VertexFormat : std::uint8_t {
    float32,
    float32x2,
    float32x3,
    float32x4,
    float16x2,
    float16x4,
    uint32,
    uint32x2,
    uint32x3,
    uint32x4,
    sint32,
    sint32x2,
    sint32x3,
    sint32x4,
    uint8x2,
    uint8x4,
    sint8x2,
    sint8x4,
    unorm8x2,
    unorm8x4,
    snorm8x2,
    snorm8x4,
    uint16x2,
    uint16x4,
    sint16x2,
    sint16x4,
    unorm16x2,
    unorm16x4,
    snorm16x2,
    snorm16x4,
};

[[nodiscard]] constexpr std::uint32_t vertex_format_size(const VertexFormat format) noexcept {
    switch (format) {
    case VertexFormat::float32:
    case VertexFormat::uint32:
    case VertexFormat::sint32:
    case VertexFormat::float16x2:
    case VertexFormat::uint8x4:
    case VertexFormat::sint8x4:
    case VertexFormat::unorm8x4:
    case VertexFormat::snorm8x4:
    case VertexFormat::uint16x2:
    case VertexFormat::sint16x2:
    case VertexFormat::unorm16x2:
    case VertexFormat::snorm16x2:
        return 4;
    case VertexFormat::float32x2:
    case VertexFormat::uint32x2:
    case VertexFormat::sint32x2:
    case VertexFormat::float16x4:
    case VertexFormat::uint16x4:
    case VertexFormat::sint16x4:
    case VertexFormat::unorm16x4:
    case VertexFormat::snorm16x4:
        return 8;
    case VertexFormat::float32x3:
    case VertexFormat::uint32x3:
    case VertexFormat::sint32x3:
        return 12;
    case VertexFormat::float32x4:
    case VertexFormat::uint32x4:
    case VertexFormat::sint32x4:
        return 16;
    case VertexFormat::uint8x2:
    case VertexFormat::sint8x2:
    case VertexFormat::unorm8x2:
    case VertexFormat::snorm8x2:
        return 2;
    }
    return 0;
}

enum class VertexStepMode : std::uint8_t {
    per_vertex,
    per_instance,
};

struct VertexAttributeDesc {
    std::string_view semantic;
    std::uint32_t semantic_index = 0;
    std::uint32_t shader_location = 0;
    VertexFormat format = VertexFormat::float32;
    std::uint32_t offset = 0;
};

struct VertexBufferLayoutDesc {
    std::uint32_t stride = 0;
    VertexStepMode step_mode = VertexStepMode::per_vertex;
    std::uint32_t step_rate = 1;
    std::span<const VertexAttributeDesc> attributes;
};

[[nodiscard]] constexpr bool is_valid(const VertexBufferLayoutDesc& desc) noexcept {
    if (desc.stride == 0 || desc.step_rate == 0 || desc.attributes.empty()) {
        return false;
    }
    if (desc.step_mode == VertexStepMode::per_vertex && desc.step_rate != 1) {
        return false;
    }
    for (const auto& attribute : desc.attributes) {
        const std::uint32_t size = vertex_format_size(attribute.format);
        if (attribute.semantic.empty() || attribute.shader_location >= max_vertex_attributes ||
            size == 0 || attribute.offset > desc.stride || size > desc.stride - attribute.offset) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr bool is_valid(
    const std::span<const VertexBufferLayoutDesc> layouts
) noexcept {
    if (layouts.size() > max_vertex_buffers) {
        return false;
    }

    bool used_locations[max_vertex_attributes]{};
    for (const auto& layout : layouts) {
        if (!is_valid(layout)) {
            return false;
        }
        for (const auto& attribute : layout.attributes) {
            if (used_locations[attribute.shader_location]) {
                return false;
            }
            used_locations[attribute.shader_location] = true;
        }
    }
    return true;
}

enum class IndexFormat : std::uint8_t {
    uint16,
    uint32,
};

[[nodiscard]] constexpr std::uint32_t index_format_size(const IndexFormat format) noexcept {
    return format == IndexFormat::uint16 ? 2U : 4U;
}

} // namespace vice::render
