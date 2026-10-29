#pragma once

#include <vice/world/snapshot.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::world {

inline constexpr std::uint32_t world_snapshot_file_format_version = 22;
inline constexpr std::uint32_t world_snapshot_file_min_supported_version = 1;
inline constexpr std::size_t world_snapshot_file_magic_size = 8;

struct WorldSnapshotEncodeResult {
    std::vector<std::byte> bytes;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty() && !bytes.empty();
    }
};

struct WorldSnapshotDecodeResult {
    std::optional<WorldSnapshot> snapshot;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return snapshot.has_value();
    }
};

[[nodiscard]] WorldSnapshotEncodeResult encode_world_snapshot(
    const WorldSnapshot& snapshot
);

[[nodiscard]] WorldSnapshotDecodeResult decode_world_snapshot(
    std::span<const std::byte> bytes
);

[[nodiscard]] WorldSnapshotDecodeResult load_world_snapshot(
    const std::filesystem::path& path
);

[[nodiscard]] bool save_world_snapshot(
    const std::filesystem::path& path,
    const WorldSnapshot& snapshot,
    std::string* diagnostics = nullptr
);

} // namespace vice::world
