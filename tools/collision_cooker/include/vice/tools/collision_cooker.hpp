#pragma once

#include <vice/rigid_body/collision_asset.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vice::tools::collision_cooker {

struct ManifestParseResult {
    std::optional<std::vector<rigid_body::CollisionAssetShape>> shapes;
    std::string diagnostics;
};

struct CookResult {
    bool success = false;
    std::uint64_t source_fingerprint = 0;
    std::vector<std::byte> bytes;
    std::string diagnostics;
};

[[nodiscard]] ManifestParseResult parse_source_manifest(std::string_view source);

[[nodiscard]] CookResult cook(
    std::span<const rigid_body::CollisionAssetShape> shapes
);

[[nodiscard]] bool write_asset(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes,
    std::string* diagnostics = nullptr
);

} // namespace vice::tools::collision_cooker
