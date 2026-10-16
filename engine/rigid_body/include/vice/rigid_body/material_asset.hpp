#pragma once

#include <vice/rigid_body/rigid_body.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::rigid_body {

inline constexpr std::uint32_t rigid_material_asset_format_version = 1;
inline constexpr std::size_t rigid_material_stable_id_size = 16;

using RigidMaterialStableId = std::array<std::uint8_t, rigid_material_stable_id_size>;

struct RigidMaterialDefinition {
    RigidMaterialStableId stable_id{};
    simulation::MaterialId runtime_id{};
    std::string name;
    simulation::Material material;
};

struct RigidMaterialInteractionDefinition {
    simulation::MaterialId first{};
    simulation::MaterialId second{};
    MaterialInteraction interaction;
};

struct RigidMaterialAsset {
    std::uint32_t version = rigid_material_asset_format_version;
    std::uint64_t source_fingerprint = 0;
    std::vector<RigidMaterialDefinition> materials;
    std::vector<RigidMaterialInteractionDefinition> interactions;
};

struct RigidMaterialAssetDecodeResult {
    std::optional<RigidMaterialAsset> asset;
    std::string diagnostics;
};

[[nodiscard]] RigidMaterialAssetDecodeResult decode_rigid_material_asset(
    std::span<const std::byte> bytes
);

[[nodiscard]] RigidMaterialAssetDecodeResult load_rigid_material_asset(
    const std::filesystem::path& path
);

[[nodiscard]] const RigidMaterialDefinition* find_rigid_material(
    const RigidMaterialAsset& asset,
    const RigidMaterialStableId& stable_id
) noexcept;

bool register_rigid_material_asset(
    Domain& domain,
    const RigidMaterialAsset& asset,
    std::string* diagnostics = nullptr
);

} // namespace vice::rigid_body
