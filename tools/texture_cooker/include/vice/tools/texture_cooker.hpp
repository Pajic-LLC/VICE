#pragma once

#include <vice/render/texture_asset.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vice::tools::texture_cooker {

inline constexpr std::uint32_t texture_source_format_version = 1U;

struct TextureSource {
    render::TextureStableId stable_id{};
    std::filesystem::path image_source;
    render::TextureColorSpace color_space = render::TextureColorSpace::srgb;
    std::uint64_t manifest_fingerprint = 0U;
};

struct ParseResult {
    std::optional<TextureSource> source;
    std::string diagnostics;
};

struct ImageData {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::byte> rgba8;
};

struct ImageDecodeResult {
    std::optional<ImageData> image;
    std::string diagnostics;
};

struct CookResult {
    bool success = false;
    std::uint64_t source_fingerprint = 0U;
    std::vector<std::byte> bytes;
    std::vector<std::byte> tile_archive_bytes;
    std::string diagnostics;
};

[[nodiscard]] ParseResult parse_source_manifest(std::string_view source_text);
[[nodiscard]] ImageDecodeResult decode_ppm(std::span<const std::byte> bytes);
[[nodiscard]] CookResult cook(const TextureSource& source, std::span<const std::byte> image_bytes);
[[nodiscard]] bool write_asset(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes,
    std::string* diagnostics = nullptr
);

} // namespace vice::tools::texture_cooker
