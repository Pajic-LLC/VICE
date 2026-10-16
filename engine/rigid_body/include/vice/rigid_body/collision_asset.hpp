#pragma once

#include <vice/rigid_body/rigid_body.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::rigid_body {

inline constexpr std::uint32_t collision_asset_format_version_v1 = 1;
inline constexpr std::uint32_t collision_asset_format_version_v2 = 2;
inline constexpr std::uint32_t collision_asset_format_version = collision_asset_format_version_v2;
inline constexpr std::size_t collision_asset_magic_size = 8;

struct CollisionAssetShape {
    ShapeId id{};
    ShapeDesc desc;
};

struct CollisionAsset {
    std::uint32_t version = collision_asset_format_version;
    std::uint64_t source_fingerprint = 0;
    std::vector<CollisionAssetShape> shapes;
};

struct CollisionAssetDecodeResult {
    std::optional<CollisionAsset> asset;
    std::string diagnostics;
};

[[nodiscard]] CollisionAssetDecodeResult decode_collision_asset(
    std::span<const std::byte> bytes
);

[[nodiscard]] CollisionAssetDecodeResult load_collision_asset(
    const std::filesystem::path& path
);

[[nodiscard]] bool register_collision_asset(
    Domain& domain,
    const CollisionAsset& asset,
    std::string* diagnostics = nullptr
);

} // namespace vice::rigid_body
