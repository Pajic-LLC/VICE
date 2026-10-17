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
    std::filesystem::path asset_root;
    std::filesystem::path startup_world;

    constexpr bool operator==(const Manifest&) const noexcept = default;
};

struct LoadResult {
    std::optional<Manifest> manifest;
    std::string diagnostics;
};

[[nodiscard]] bool valid_relative_path(const std::filesystem::path& path) noexcept;
[[nodiscard]] bool validate(const Manifest& manifest, std::string* diagnostics = nullptr);
[[nodiscard]] LoadResult parse_manifest(std::string_view text);
[[nodiscard]] LoadResult load_manifest(const std::filesystem::path& path);
[[nodiscard]] std::string serialize_manifest(const Manifest& manifest);

[[nodiscard]] std::filesystem::path resolve_asset_root(
    const std::filesystem::path& manifest_path,
    const Manifest& manifest
);

[[nodiscard]] std::filesystem::path resolve_startup_world(
    const std::filesystem::path& manifest_path,
    const Manifest& manifest
);

} // namespace vice::project
