#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace vice::project {

inline constexpr std::uint32_t project_manifest_version = 1;
inline constexpr std::size_t project_id_size = 16;
inline constexpr std::size_t game_target_max_size = 64;
inline constexpr std::size_t dade_asset_id_max_size = 255;

struct ProjectId {
    std::array<std::uint8_t, project_id_size> bytes{};

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        for (const auto byte : bytes) {
            if (byte != 0U) return true;
        }
        return false;
    }

    constexpr bool operator==(const ProjectId&) const noexcept = default;

    [[nodiscard]] static std::optional<ProjectId> parse(std::string_view text) noexcept;
    [[nodiscard]] std::string to_string() const;
};

struct Manifest {
    std::uint32_t version = project_manifest_version;
    ProjectId id{};
    std::string name;
    std::string game_name;
    std::string game_target;
    std::string dade_asset_id;
    std::filesystem::path asset_root;
    std::filesystem::path startup_world;

    bool operator==(const Manifest&) const = default;
};

struct LoadResult {
    std::optional<Manifest> manifest;
    std::string diagnostics;
};

[[nodiscard]] bool valid_relative_path(const std::filesystem::path& path) noexcept;
[[nodiscard]] bool valid_game_target(std::string_view target) noexcept;
[[nodiscard]] bool valid_dade_asset_id(std::string_view asset_id) noexcept;
[[nodiscard]] std::string default_game_target(std::string_view display_name);
[[nodiscard]] bool validate(const Manifest& manifest, std::string* diagnostics = nullptr);
[[nodiscard]] LoadResult parse_manifest(std::string_view text);
[[nodiscard]] LoadResult load_manifest(const std::filesystem::path& path);
[[nodiscard]] std::string serialize_manifest(const Manifest& manifest);
[[nodiscard]] bool save_manifest(
    const std::filesystem::path& path,
    const Manifest& manifest,
    std::string* diagnostics = nullptr
);

[[nodiscard]] std::filesystem::path resolve_asset_root(
    const std::filesystem::path& manifest_path,
    const Manifest& manifest
);

[[nodiscard]] std::filesystem::path resolve_startup_world(
    const std::filesystem::path& manifest_path,
    const Manifest& manifest
);

[[nodiscard]] std::filesystem::path resolve_cooked_root(
    const std::filesystem::path& cooked_cache_base,
    const Manifest& manifest
);

[[nodiscard]] std::filesystem::path resolve_cooked_startup_world(
    const std::filesystem::path& cooked_cache_base,
    const Manifest& manifest
);

[[nodiscard]] std::filesystem::path resolve_game_output_root(
    const std::filesystem::path& manifest_path,
    const Manifest& manifest
);

} // namespace vice::project
