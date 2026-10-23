#pragma once

#include <vice/render/texture_asset.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::render {

inline constexpr std::uint32_t virtual_texture_archive_format_version = 1U;

struct VirtualTextureArchiveTile {
    std::uint32_t mip_level = 0U;
    std::uint32_t tile_x = 0U;
    std::uint32_t tile_y = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint64_t payload_offset = 0U;
    std::uint64_t payload_size = 0U;
    std::uint64_t payload_fingerprint = 0U;

    friend bool operator==(const VirtualTextureArchiveTile&, const VirtualTextureArchiveTile&) = default;
};

struct VirtualTextureArchiveDirectory {
    TextureStableId stable_id{};
    std::uint64_t source_fingerprint = 0U;
    std::vector<VirtualTextureArchiveTile> tiles;
};

struct VirtualTextureArchiveEncodeResult {
    std::vector<std::byte> bytes;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty() && !bytes.empty();
    }
};

struct VirtualTextureArchiveDirectoryResult {
    std::optional<VirtualTextureArchiveDirectory> directory;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return directory.has_value();
    }
};

struct VirtualTextureArchiveTilePayload {
    VirtualTextureArchiveTile descriptor;
    std::vector<std::byte> rgba8;
};

struct VirtualTextureArchiveTileResult {
    std::optional<VirtualTextureArchiveTilePayload> tile;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return tile.has_value();
    }
};

[[nodiscard]] VirtualTextureArchiveEncodeResult encode_virtual_texture_archive(
    const TextureAsset& asset
);

[[nodiscard]] VirtualTextureArchiveDirectoryResult load_virtual_texture_archive_directory(
    const std::filesystem::path& path
);

[[nodiscard]] VirtualTextureArchiveTileResult load_virtual_texture_archive_tile(
    const std::filesystem::path& path,
    const VirtualTextureArchiveDirectory& directory,
    std::uint32_t mip_level,
    std::uint32_t tile_x,
    std::uint32_t tile_y
);

[[nodiscard]] bool virtual_texture_archive_matches_asset(
    const VirtualTextureArchiveDirectory& directory,
    const TextureAsset& asset
) noexcept;

} // namespace vice::render
