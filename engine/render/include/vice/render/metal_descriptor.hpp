#pragma once

#include <bit>
#include <cstdint>

namespace vice::render {

inline constexpr std::uint32_t metal_converter_argument_buffer_bind_point = 2U;
inline constexpr std::uint32_t metal_converter_descriptor_size = 24U;
inline constexpr std::uint32_t metal_inline_argument_buffer_limit = 4096U;

struct MetalConverterDescriptor {
    std::uint64_t gpu_address_or_resource_id = 0;
    std::uint64_t texture_buffer_view_id = 0;
    std::uint64_t metadata = 0;
};

static_assert(sizeof(MetalConverterDescriptor) == metal_converter_descriptor_size);

[[nodiscard]] constexpr MetalConverterDescriptor metal_buffer_descriptor(
    const std::uint64_t gpu_address,
    const std::uint32_t length
) noexcept {
    return MetalConverterDescriptor{
        .gpu_address_or_resource_id = gpu_address,
        .texture_buffer_view_id = 0,
        .metadata = static_cast<std::uint64_t>(length),
    };
}

[[nodiscard]] constexpr MetalConverterDescriptor metal_texture_descriptor(
    const std::uint64_t resource_id,
    const float minimum_lod_clamp = 0.0F
) noexcept {
    return MetalConverterDescriptor{
        .gpu_address_or_resource_id = 0,
        .texture_buffer_view_id = resource_id,
        .metadata = static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(minimum_lod_clamp)),
    };
}

[[nodiscard]] constexpr MetalConverterDescriptor metal_sampler_descriptor(
    const std::uint64_t resource_id,
    const float lod_bias = 0.0F
) noexcept {
    return MetalConverterDescriptor{
        .gpu_address_or_resource_id = resource_id,
        .texture_buffer_view_id = 0,
        .metadata = static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(lod_bias)),
    };
}

} // namespace vice::render
