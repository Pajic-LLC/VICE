#pragma once

#include <vice/render/environment_asset.hpp>
#include <vice/render/texture_asset.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vice::tools::environment_cooker {

inline constexpr std::uint32_t environment_source_format_version = 1U;

struct EnvironmentSource {
    render::EnvironmentStableId stable_id{};
    std::filesystem::path image_source;
    render::TextureColorSpace color_space = render::TextureColorSpace::srgb;
    render::EnvironmentScope scope = render::EnvironmentScope::global;
    float intensity = 1.0F;
    float rotation_degrees = 0.0F;
    std::uint32_t irradiance_width = 32U;
    std::uint32_t irradiance_height = 16U;
    std::uint32_t specular_width = 128U;
    std::uint32_t specular_height = 64U;
    std::uint32_t specular_mip_levels = 6U;
    std::uint32_t brdf_lut_size = 64U;
    std::uint32_t sample_count = 128U;
    std::uint64_t manifest_fingerprint = 0U;
};

struct ParseResult {
    std::optional<EnvironmentSource> source;
    std::string diagnostics;
};

struct CookResult {
    bool success = false;
    std::uint64_t source_fingerprint = 0U;
    std::vector<std::byte> bytes;
    std::string diagnostics;
};

[[nodiscard]] ParseResult parse_source_manifest(std::string_view source_text);
[[nodiscard]] CookResult cook(const EnvironmentSource& source, std::span<const std::byte> image_bytes);
[[nodiscard]] bool write_asset(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes,
    std::string* diagnostics = nullptr
);

} // namespace vice::tools::environment_cooker
