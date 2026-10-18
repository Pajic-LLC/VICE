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

inline constexpr std::uint32_t texture_asset_format_version = 1U;
inline constexpr std::size_t texture_asset_magic_size = 8U;
inline constexpr std::size_t texture_stable_id_size = 16U;

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

struct TextureAsset {
    std::uint32_t version = texture_asset_format_version;
    TextureStableId stable_id{};
    std::uint64_t source_fingerprint = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    TextureColorSpace color_space = TextureColorSpace::srgb;
    std::vector<std::byte> rgba8;
};

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
