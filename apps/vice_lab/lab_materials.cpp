#include "lab_materials.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace vice::lab {
namespace {

constexpr std::string_view cube_id_text =
    "10000000-0000-4000-8000-000000000002";
constexpr std::string_view terrain_a_id_text =
    "10000000-0000-4000-8000-000000000003";
constexpr std::string_view terrain_b_id_text =
    "10000000-0000-4000-8000-000000000004";
constexpr std::string_view ramp_id_text =
    "10000000-0000-4000-8000-000000000005";
constexpr std::string_view concrete_material_id_text =
    "20000000-0000-4000-8000-000000000001";
constexpr std::string_view cube_material_id_text =
    "20000000-0000-4000-8000-000000000002";
constexpr std::string_view uses_material_relation_text =
    "30000000-0000-4000-8000-000000000001";

struct MaterialRoleIds {
    world::PersistentId cube{};
    world::PersistentId terrain_a{};
    world::PersistentId terrain_b{};
    world::PersistentId ramp{};
    world::PersistentId concrete_material{};
    world::PersistentId cube_material{};
    world::PersistentId uses_material{};
};

[[nodiscard]] std::optional<MaterialRoleIds> parse_role_ids() noexcept {
    const auto cube = world::PersistentId::parse(cube_id_text);
    const auto terrain_a = world::PersistentId::parse(terrain_a_id_text);
    const auto terrain_b = world::PersistentId::parse(terrain_b_id_text);
    const auto ramp = world::PersistentId::parse(ramp_id_text);
    const auto concrete = world::PersistentId::parse(concrete_material_id_text);
    const auto cube_material = world::PersistentId::parse(cube_material_id_text);
    const auto uses_material = world::PersistentId::parse(uses_material_relation_text);
    if (!cube || !terrain_a || !terrain_b || !ramp || !concrete || !cube_material ||
        !uses_material) {
        return std::nullopt;
    }
    return MaterialRoleIds{
        .cube = *cube,
        .terrain_a = *terrain_a,
        .terrain_b = *terrain_b,
        .ramp = *ramp,
        .concrete_material = *concrete,
        .cube_material = *cube_material,
        .uses_material = *uses_material,
    };
}

[[nodiscard]] std::optional<world::EntityId> ensure_entity(
    world::World& world,
    const world::PersistentId id
) {
    if (const auto existing = world.find(id)) return existing;
    return world.create_entity(id);
}

bool ensure_relation(
    world::World& world,
    const world::EntityId subject,
    const world::PersistentId kind,
    const world::EntityId object
) {
    const world::Relation wanted{subject, kind, object};
    const auto found = std::find(world.relations().begin(), world.relations().end(), wanted);
    return found != world.relations().end() || world.add_relation(wanted);
}

[[nodiscard]] rigid_body::RigidMaterialStableId material_stable_id(
    const world::PersistentId id
) noexcept {
    rigid_body::RigidMaterialStableId stable{};
    std::copy(id.bytes().begin(), id.bytes().end(), stable.begin());
    return stable;
}

[[nodiscard]] std::optional<LabMaterialAssignment> resolve_assignment(
    const world::World& world,
    const rigid_body::RigidMaterialAsset& asset,
    const world::PersistentId role_id,
    const world::PersistentId relation_kind,
    std::string& diagnostics
) {
    const auto role = world.find(role_id);
    if (!role.has_value()) {
        diagnostics = "Material role entity is missing from World state.";
        return std::nullopt;
    }

    std::optional<world::PersistentId> material_id;
    for (const auto& relation : world.relations()) {
        if (relation.subject != *role || relation.kind != relation_kind) continue;
        const auto candidate = world.persistent_id(relation.object);
        if (!candidate.has_value()) {
            diagnostics = "Material assignment relation points to a missing World entity.";
            return std::nullopt;
        }
        if (material_id.has_value() && *material_id != *candidate) {
            diagnostics = "Material role has multiple material assignments.";
            return std::nullopt;
        }
        material_id = *candidate;
    }
    if (!material_id.has_value()) {
        diagnostics = "Material role has no persisted material assignment.";
        return std::nullopt;
    }

    const auto* definition = rigid_body::find_rigid_material(
        asset,
        material_stable_id(*material_id)
    );
    if (definition == nullptr) {
        diagnostics = "Persisted material assignment is not present in the cooked material asset.";
        return std::nullopt;
    }
    return LabMaterialAssignment{
        .runtime_id = definition->runtime_id,
        .name = definition->name,
    };
}

} // namespace

bool ensure_material_roles(world::World& world, std::string* diagnostics) {
    const auto ids = parse_role_ids();
    if (!ids.has_value()) {
        if (diagnostics != nullptr) *diagnostics = "VICE Lab material role IDs are invalid.";
        return false;
    }

    const auto cube = world.find(ids->cube);
    const auto terrain_a = ensure_entity(world, ids->terrain_a);
    const auto terrain_b = ensure_entity(world, ids->terrain_b);
    const auto ramp = ensure_entity(world, ids->ramp);
    const auto concrete = ensure_entity(world, ids->concrete_material);
    const auto cube_material = ensure_entity(world, ids->cube_material);
    if (!cube.has_value() || !terrain_a.has_value() || !terrain_b.has_value() ||
        !ramp.has_value() || !concrete.has_value() || !cube_material.has_value()) {
        if (diagnostics != nullptr) {
            *diagnostics = "VICE Lab could not create or resolve material role entities.";
        }
        return false;
    }

    if (!ensure_relation(world, *cube, ids->uses_material, *cube_material) ||
        !ensure_relation(world, *terrain_a, ids->uses_material, *concrete) ||
        !ensure_relation(world, *terrain_b, ids->uses_material, *concrete) ||
        !ensure_relation(world, *ramp, ids->uses_material, *concrete)) {
        if (diagnostics != nullptr) {
            *diagnostics = "VICE Lab could not establish persisted material assignment relations.";
        }
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

LabMaterialLoadResult load_materials(
    const world::World& world,
    const std::filesystem::path& asset_path
) {
    LabMaterialLoadResult result;
    const auto ids = parse_role_ids();
    if (!ids.has_value()) {
        result.diagnostics = "VICE Lab material role IDs are invalid.";
        return result;
    }

    auto loaded = rigid_body::load_rigid_material_asset(asset_path);
    if (!loaded.asset.has_value()) {
        result.diagnostics = std::move(loaded.diagnostics);
        return result;
    }

    std::string diagnostics;
    const auto terrain_a = resolve_assignment(
        world,
        *loaded.asset,
        ids->terrain_a,
        ids->uses_material,
        diagnostics
    );
    if (!terrain_a.has_value()) {
        result.diagnostics = "Terrain A material: " + diagnostics;
        return result;
    }
    const auto terrain_b = resolve_assignment(
        world,
        *loaded.asset,
        ids->terrain_b,
        ids->uses_material,
        diagnostics
    );
    if (!terrain_b.has_value()) {
        result.diagnostics = "Terrain B material: " + diagnostics;
        return result;
    }
    const auto ramp = resolve_assignment(
        world,
        *loaded.asset,
        ids->ramp,
        ids->uses_material,
        diagnostics
    );
    if (!ramp.has_value()) {
        result.diagnostics = "Ramp material: " + diagnostics;
        return result;
    }
    const auto cube = resolve_assignment(
        world,
        *loaded.asset,
        ids->cube,
        ids->uses_material,
        diagnostics
    );
    if (!cube.has_value()) {
        result.diagnostics = "Cube material: " + diagnostics;
        return result;
    }

    result.materials = LabMaterialSet{
        .asset = std::move(*loaded.asset),
        .terrain_a = *terrain_a,
        .terrain_b = *terrain_b,
        .ramp = *ramp,
        .cube = *cube,
    };
    return result;
}

} // namespace vice::lab
