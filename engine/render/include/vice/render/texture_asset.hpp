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

inline constexpr std::uint32_t texture_asset_format_version_v1 = 1U;
inline constexpr std::uint32_t texture_asset_format_version_v2 = 2U;
inline constexpr std::uint32_t texture_asset_format_version = texture_asset_format_version_v2;
inline constexpr std::size_t texture_asset_magic_size = 8U;
inline constexpr std::size_t texture_stable_id_size = 16U;
inline constexpr std::uint32_t virtual_texture_tile_extent = 128U;

using TextureStableId = std::array<std::uint8_t, texture_stable_id_size>;

[[nodiscard]] constexpr bool valid_texture_id(const TextureStableId& id) noexcept {
    for (const std::uint8_t value : id) {
        if (value != 0U) return true;
    }
    return false;
}

enum class TextureColorSpace : std::uint8_t {
    linear = 0U,
    srgb = 1U,
};

struct TextureMipLevel {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint64_t payload_offset = 0U;
    std::uint64_t payload_size = 0U;
    std::uint32_t first_tile = 0U;
    std::uint32_t tile_count = 0U;

    friend bool operator==(const TextureMipLevel&, const TextureMipLevel&) = default;
};

struct VirtualTextureTile {
    std::uint32_t mip_level = 0U;
    std::uint32_t tile_x = 0U;
    std::uint32_t tile_y = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;

    friend bool operator==(const VirtualTextureTile&, const VirtualTextureTile&) = default;
};

struct TextureAsset {
    // Keep direct/manual TextureAsset construction source-compatible with the legacy
    // one-mip contract. Offline texture cooking explicitly emits the current v2 format.
    std::uint32_t version = texture_asset_format_version_v1;
    TextureStableId stable_id{};
    std::uint64_t source_fingerprint = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    TextureColorSpace color_space = TextureColorSpace::srgb;
    // v1: the single authored mip. v2: every mip packed consecutively in level order.
    std::vector<std::byte> rgba8;
    std::vector<TextureMipLevel> mip_levels;
    std::vector<VirtualTextureTile> virtual_tiles;
};

[[nodiscard]] std::uint32_t texture_mip_count(const TextureAsset& asset) noexcept;
[[nodiscard]] std::span<const std::byte> texture_mip_rgba8(
    const TextureAsset& asset,
    std::uint32_t mip_level
) noexcept;

struct TextureAssetEncodeResult {
    std::vector<std::byte> bytes;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty() && !bytes.empty();
    }
};

struct TextureAssetDecodeResult {
    std::optional<TextureAsset> asset;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return asset.has_value();
    }
};

[[nodiscard]] TextureAssetEncodeResult encode_texture_asset(const TextureAsset& asset);
[[nodiscard]] TextureAssetDecodeResult decode_texture_asset(std::span<const std::byte> bytes);
[[nodiscard]] TextureAssetDecodeResult load_texture_asset(const std::filesystem::path& path);

} // namespace vice::render
