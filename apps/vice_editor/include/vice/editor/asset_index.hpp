#pragma once

#include <vice/world/resource.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::editor {

enum class AssetKind : std::uint8_t {
    world,
    mesh,
    collision,
    rigid_material,
    shader_source,
    shader_manifest,
    unknown,
};

[[nodiscard]] const char* asset_kind_name(AssetKind kind) noexcept;

struct AssetRecord {
    AssetKind kind = AssetKind::unknown;
    std::string name;
    std::filesystem::path relative_path;
    std::filesystem::path source_path;
    std::filesystem::path cooked_path;
    std::optional<world::ResourceId> resource_id;
    std::uint32_t format_version = 0;
    std::uint64_t source_fingerprint = 0;
    std::string summary;
    std::string diagnostics;
    bool cooked_available = false;
    bool cooked_valid = false;

    [[nodiscard]] bool instantiable_mesh() const noexcept {
        return kind == AssetKind::mesh && cooked_valid && resource_id.has_value();
    }
};

class AssetIndex {
public:
    bool refresh(
        const std::filesystem::path& source_root,
        const std::filesystem::path& cooked_root
    );

    [[nodiscard]] std::span<const AssetRecord> assets() const noexcept;
    [[nodiscard]] const AssetRecord* find_resource(world::ResourceId resource_id) const noexcept;
    [[nodiscard]] const std::filesystem::path& source_root() const noexcept;
    [[nodiscard]] const std::filesystem::path& cooked_root() const noexcept;
    [[nodiscard]] const std::string& diagnostics() const noexcept;

private:
    std::filesystem::path source_root_;
    std::filesystem::path cooked_root_;
    std::vector<AssetRecord> assets_;
    std::string diagnostics_;
};

} // namespace vice::editor
