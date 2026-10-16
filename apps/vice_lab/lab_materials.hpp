#pragma once

#include <vice/rigid_body/material_asset.hpp>
#include <vice/world/world.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace vice::lab {

struct LabMaterialAssignment {
    simulation::MaterialId runtime_id{};
    std::string name;
};

struct LabMaterialSet {
    rigid_body::RigidMaterialAsset asset;
    LabMaterialAssignment terrain_a;
    LabMaterialAssignment terrain_b;
    LabMaterialAssignment ramp;
    LabMaterialAssignment cube;
};

struct LabMaterialLoadResult {
    std::optional<LabMaterialSet> materials;
    std::string diagnostics;
};

bool ensure_material_roles(world::World& world, std::string* diagnostics = nullptr);

[[nodiscard]] LabMaterialLoadResult load_materials(
    const world::World& world,
    const std::filesystem::path& asset_path
);

} // namespace vice::lab
