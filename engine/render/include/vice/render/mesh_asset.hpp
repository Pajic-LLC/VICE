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

inline constexpr std::uint32_t mesh_asset_format_version_v1 = 1U;
inline constexpr std::uint32_t mesh_asset_format_version_v2 = 2U;
inline constexpr std::uint32_t mesh_asset_format_version_v3 = 3U;
inline constexpr std::uint32_t mesh_asset_format_version = mesh_asset_format_version_v3;
inline constexpr std::uint32_t mesh_asset_min_supported_version = mesh_asset_format_version_v1;
inline constexpr std::size_t mesh_asset_magic_size = 8U;
inline constexpr std::size_t mesh_stable_id_size = 16U;

using MeshStableId = std::array<std::uint8_t, mesh_stable_id_size>;

[[nodiscard]] constexpr bool valid(const MeshStableId& id) noexcept {
    for (const std::uint8_t value : id) {
        if (value != 0U) return true;
    }
    return false;
}

struct MeshVertex {
    float position[3]{};
    float color[3]{1.0F, 1.0F, 1.0F};
    float normal[3]{0.0F, 1.0F, 0.0F};
    float uv[2]{};
};

static_assert(sizeof(MeshVertex) == 44U);

struct MeshAsset {
    std::uint32_t version = mesh_asset_format_version;
    MeshStableId stable_id{};
    std::uint64_t source_fingerprint = 0U;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct MeshAssetDecodeResult {
    std::optional<MeshAsset> asset;
    std::string diagnostics;
};

[[nodiscard]] MeshAssetDecodeResult decode_mesh_asset(std::span<const std::byte> bytes);
[[nodiscard]] MeshAssetDecodeResult load_mesh_asset(const std::filesystem::path& path);

} // namespace vice::render
