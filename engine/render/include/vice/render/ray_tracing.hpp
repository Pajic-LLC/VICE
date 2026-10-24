#pragma once

#include <vice/render/rhi.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace vice::render {

enum class AccelerationStructureType : std::uint8_t {
    bottom_level,
    top_level,
};

enum class AccelerationStructureBuildFlags : std::uint32_t {
    none = 0,
    allow_update = 1U << 0U,
    prefer_fast_trace = 1U << 1U,
    prefer_fast_build = 1U << 2U,
    minimize_memory = 1U << 3U,
};

[[nodiscard]] constexpr AccelerationStructureBuildFlags operator|(
    const AccelerationStructureBuildFlags left,
    const AccelerationStructureBuildFlags right
) noexcept {
    return static_cast<AccelerationStructureBuildFlags>(
        static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right)
    );
}

[[nodiscard]] constexpr AccelerationStructureBuildFlags operator&(
    const AccelerationStructureBuildFlags left,
    const AccelerationStructureBuildFlags right
) noexcept {
    return static_cast<AccelerationStructureBuildFlags>(
        static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right)
    );
}

constexpr AccelerationStructureBuildFlags& operator|=(
    AccelerationStructureBuildFlags& left,
    const AccelerationStructureBuildFlags right
) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool has_flag(
    const AccelerationStructureBuildFlags value,
    const AccelerationStructureBuildFlags flag
) noexcept {
    return (value & flag) == flag;
}

[[nodiscard]] constexpr bool is_valid(
    const AccelerationStructureBuildFlags flags
) noexcept {
    return !(has_flag(flags, AccelerationStructureBuildFlags::prefer_fast_trace) &&
        has_flag(flags, AccelerationStructureBuildFlags::prefer_fast_build));
}

class AccelerationStructure {
public:
    virtual ~AccelerationStructure() = default;

    [[nodiscard]] virtual AccelerationStructureType type() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t size() const noexcept = 0;
};

struct AccelerationStructureTriangleGeometryDesc {
    Buffer* vertex_buffer = nullptr;
    std::uint64_t vertex_offset = 0U;
    std::uint32_t vertex_count = 0U;
    std::uint32_t vertex_stride = 0U;
    VertexFormat vertex_format = VertexFormat::float32x3;
    Buffer* index_buffer = nullptr;
    std::uint64_t index_offset = 0U;
    std::uint32_t index_count = 0U;
    IndexFormat index_format = IndexFormat::uint32;
    bool opaque = true;
};

inline constexpr std::uint32_t acceleration_structure_instance_id_max = 0x00FF'FFFFU;
inline constexpr std::uint32_t acceleration_structure_instance_mask_max = 0x0000'00FFU;

struct AccelerationStructureInstanceDesc {
    AccelerationStructure* bottom_level = nullptr;
    std::array<float, 12> transform{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
    };
    std::uint32_t instance_id = 0U;
    std::uint32_t mask = acceleration_structure_instance_mask_max;
};

struct AccelerationStructureBuildDesc {
    AccelerationStructureType type = AccelerationStructureType::bottom_level;
    AccelerationStructureBuildFlags flags = AccelerationStructureBuildFlags::prefer_fast_trace;
    std::span<const AccelerationStructureTriangleGeometryDesc> geometries;
    std::span<const AccelerationStructureInstanceDesc> instances;
};

struct AccelerationStructureBuildSizes {
    std::uint64_t result_size = 0U;
    std::uint64_t build_scratch_size = 0U;
    std::uint64_t update_scratch_size = 0U;
};

[[nodiscard]] inline bool is_valid(
    const AccelerationStructureTriangleGeometryDesc& desc
) noexcept {
    if (desc.vertex_buffer == nullptr || desc.index_buffer == nullptr ||
        desc.vertex_count < 3U || desc.index_count < 3U || desc.index_count % 3U != 0U ||
        desc.vertex_format != VertexFormat::float32x3 ||
        desc.vertex_stride < vertex_format_size(desc.vertex_format) ||
        !has_usage(desc.vertex_buffer->usage(), BufferUsage::vertex) ||
        !has_usage(desc.index_buffer->usage(), BufferUsage::index)) {
        return false;
    }

    const std::uint64_t vertex_size = vertex_format_size(desc.vertex_format);
    const std::uint64_t index_size = index_format_size(desc.index_format);
    if (desc.vertex_offset % alignof(float) != 0U || desc.index_offset % index_size != 0U) {
        return false;
    }

    const std::uint64_t vertex_span =
        static_cast<std::uint64_t>(desc.vertex_count - 1U) * desc.vertex_stride + vertex_size;
    const std::uint64_t index_span = static_cast<std::uint64_t>(desc.index_count) * index_size;
    if (desc.vertex_offset > desc.vertex_buffer->size() ||
        vertex_span > desc.vertex_buffer->size() - desc.vertex_offset ||
        desc.index_offset > desc.index_buffer->size() ||
        index_span > desc.index_buffer->size() - desc.index_offset) {
        return false;
    }

    return true;
}

[[nodiscard]] inline bool is_valid(
    const AccelerationStructureInstanceDesc& desc
) noexcept {
    if (desc.bottom_level == nullptr ||
        desc.bottom_level->type() != AccelerationStructureType::bottom_level ||
        desc.instance_id > acceleration_structure_instance_id_max ||
        desc.mask > acceleration_structure_instance_mask_max) {
        return false;
    }

    for (const float value : desc.transform) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

[[nodiscard]] inline bool is_valid(const AccelerationStructureBuildDesc& desc) noexcept {
    if (!is_valid(desc.flags)) return false;

    switch (desc.type) {
    case AccelerationStructureType::bottom_level:
        if (desc.geometries.empty() || !desc.instances.empty()) return false;
        for (const auto& geometry : desc.geometries) {
            if (!is_valid(geometry)) return false;
        }
        return true;
    case AccelerationStructureType::top_level:
        if (desc.instances.empty() || !desc.geometries.empty()) return false;
        for (const auto& instance : desc.instances) {
            if (!is_valid(instance)) return false;
        }
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool is_valid(
    const AccelerationStructureBuildSizes& sizes
) noexcept {
    return sizes.result_size > 0U && sizes.build_scratch_size > 0U;
}

[[nodiscard]] constexpr bool supports_acceleration_structures(
    const DeviceCapabilities& capabilities
) noexcept {
    return capabilities.ray_tracing;
}

} // namespace vice::render
