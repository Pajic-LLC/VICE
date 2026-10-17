#pragma once

#include <vice/editor/asset_index.hpp>
#include <vice/math/math.hpp>
#include <vice/world/resource.hpp>
#include <vice/world/world.hpp>

#include <optional>

namespace vice::editor {

struct MeshPickResult {
    world::PersistentId persistent_id{};
    world::ResourceId resource{};
    float distance = 0.0F;

    friend bool operator==(const MeshPickResult&, const MeshPickResult&) = default;
};

[[nodiscard]] std::optional<MeshPickResult> pick_render_mesh(
    const world::World& world,
    const AssetIndex& assets,
    math::Vec3 ray_origin,
    math::Vec3 ray_direction
) noexcept;

} // namespace vice::editor
