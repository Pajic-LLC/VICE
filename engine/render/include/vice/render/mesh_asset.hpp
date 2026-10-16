#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::render {

inline constexpr std::uint32_t mesh_asset_format_version = 1;
inline constexpr std::size_t mesh_asset_magic_size = 8;

struct MeshVertex {
    float position[3]{};
    float color[3]{1.0F, 1.0F, 1.0F};
};

static_assert(sizeof(MeshVertex) == 24U);

struct MeshAsset {
    std::uint32_t version = mesh_asset_format_version;
    std::uint64_t source_fingerprint = 0;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct MeshAssetDecodeResult {
    std::optional<MeshAsset> asset;
    std::string diagnostics;
};

[[nodiscard]] MeshAssetDecodeResult decode_mesh_asset(
    std::span<const std::byte> bytes
);

[[nodiscard]] MeshAssetDecodeResult load_mesh_asset(
    const std::filesystem::path& path
);

} // namespace vice::render
