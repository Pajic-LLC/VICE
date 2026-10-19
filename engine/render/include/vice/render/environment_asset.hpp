#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::render {

inline constexpr std::uint32_t environment_asset_format_version_v1 = 1U;
inline constexpr std::uint32_t environment_asset_format_version_v2 = 2U;
inline constexpr std::uint32_t environment_asset_format_version = environment_asset_format_version_v2;
inline constexpr std::uint32_t environment_asset_min_supported_version = environment_asset_format_version_v1;
inline constexpr std::size_t environment_asset_magic_size = 8U;
inline constexpr std::size_t environment_stable_id_size = 16U;

using EnvironmentStableId = std::array<std::uint8_t, environment_stable_id_size>;

enum class EnvironmentScope : std::uint8_t {
    global = 1,
    local_probe = 2,
};

[[nodiscard]] constexpr bool valid(const EnvironmentScope scope) noexcept {
    return scope == EnvironmentScope::global || scope == EnvironmentScope::local_probe;
}

[[nodiscard]] constexpr bool valid_environment_id(const EnvironmentStableId& id) noexcept {
    for (const std::uint8_t value : id) {
        if (value != 0U) return true;
    }
    return false;
}

struct EnvironmentAsset {
    std::uint32_t version = environment_asset_format_version;
    EnvironmentStableId stable_id{};
    std::uint64_t source_fingerprint = 0U;
    float intensity = 1.0F;
    float rotation_radians = 0.0F;
    std::uint32_t irradiance_width = 0U;
    std::uint32_t irradiance_height = 0U;
    std::uint32_t specular_width = 0U;
    std::uint32_t specular_height = 0U;
    std::uint32_t specular_mip_levels = 0U;
    std::uint32_t brdf_lut_size = 0U;
    EnvironmentScope scope = EnvironmentScope::global;
    std::vector<std::byte> irradiance_rgba16f;
    std::vector<std::byte> specular_rgba16f;
    std::vector<std::byte> brdf_lut_rgba16f;
};

struct EnvironmentAssetEncodeResult {
    std::vector<std::byte> bytes;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty() && !bytes.empty();
    }
};

struct EnvironmentAssetDecodeResult {
    std::optional<EnvironmentAsset> asset;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return asset.has_value(); }
};

[[nodiscard]] EnvironmentAssetEncodeResult encode_environment_asset(const EnvironmentAsset& asset);
[[nodiscard]] EnvironmentAssetDecodeResult decode_environment_asset(std::span<const std::byte> bytes);
[[nodiscard]] EnvironmentAssetDecodeResult load_environment_asset(const std::filesystem::path& path);

} // namespace vice::render
