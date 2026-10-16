#include <vice/rigid_body/collision_asset.hpp>
#include <vice/tools/collision_import.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] bool near(const float left, const float right) noexcept {
    return std::abs(left - right) < 0.0001F;
}

[[nodiscard]] const vice::tools::collision_import::ShapeBinding* find_binding(
    const std::vector<vice::tools::collision_import::ShapeBinding>& bindings,
    const std::string& source_key
) {
    const auto found = std::find_if(
        bindings.begin(),
        bindings.end(),
        [&](const auto& binding) {
            return binding.source_key == source_key;
        }
    );
    return found == bindings.end() ? nullptr : &*found;
}

std::vector<vice::tools::collision_import::ImportedMesh> source_meshes() {
    using namespace vice;
    using namespace vice::tools::collision_import;

    ImportedMesh render_only;
    render_only.source_key = "scene/render/window";
    render_only.positions = {
        math::Vec3{0.0F, 0.0F, 0.0F},
        math::Vec3{1.0F, 0.0F, 0.0F},
        math::Vec3{0.0F, 1.0F, 0.0F},
    };
    render_only.indices = {0U, 1U, 2U};

    ImportedMesh collision_only;
    collision_only.source_key = "scene/collision/floor";
    collision_only.positions = {
        math::Vec3{0.0F, 0.0F, 0.0F},
        math::Vec3{1.0F, 0.0F, 0.0F},
        math::Vec3{0.0F, 0.0F, 1.0F},
    };
    collision_only.indices = {0U, 1U, 2U};
    collision_only.local_transform.position = math::Vec3{10.0F, 2.0F, -3.0F};
    collision_only.local_transform.scale = math::Vec3{-2.0F, 1.0F, 0.5F};
    collision_only.render_mesh = false;
    collision_only.collision.representation = CollisionRepresentation::triangle_mesh;
    collision_only.collision.shape_id = rigid_body::ShapeId{500};

    ImportedMesh shared_mesh;
    shared_mesh.source_key = "scene/props/crate";
    shared_mesh.positions = {
        math::Vec3{0.0F, 0.0F, 0.0F},
        math::Vec3{1.0F, 0.0F, 0.0F},
        math::Vec3{0.0F, 1.0F, 0.0F},
        math::Vec3{0.0F, 0.0F, 1.0F},
    };
    shared_mesh.local_transform.position = math::Vec3{1.0F, 0.0F, 0.0F};
    shared_mesh.local_transform.orientation = math::Quat{
        0.0F,
        0.70710678F,
        0.0F,
        0.70710678F,
    };
    shared_mesh.local_transform.scale = math::Vec3{2.0F, 1.0F, 1.0F};
    shared_mesh.collision.representation = CollisionRepresentation::convex_hull;

    return {render_only, collision_only, shared_mesh};
}

} // namespace

int main() {
    using namespace vice;
    using namespace vice::tools::collision_import;

    auto meshes = source_meshes();
    const auto built = build_collision_shapes(meshes);
    assert(built.shapes.has_value());
    assert(built.diagnostics.empty());
    assert(built.shapes->size() == 2U);
    assert(built.bindings.size() == 2U);

    const auto* floor_binding = find_binding(built.bindings, "scene/collision/floor");
    const auto* crate_binding = find_binding(built.bindings, "scene/props/crate");
    assert(floor_binding != nullptr);
    assert(crate_binding != nullptr);
    assert(floor_binding->shape == rigid_body::ShapeId{500});
    assert(!floor_binding->render_mesh);
    assert(crate_binding->shape);
    assert(crate_binding->shape != rigid_body::ShapeId{500});
    assert(crate_binding->render_mesh);

    const auto floor_shape = std::find_if(
        built.shapes->begin(),
        built.shapes->end(),
        [](const rigid_body::CollisionAssetShape& shape) {
            return shape.id == rigid_body::ShapeId{500};
        }
    );
    assert(floor_shape != built.shapes->end());
    const auto& floor = std::get<rigid_body::TriangleMeshShapeDesc>(floor_shape->desc);
    assert(floor.vertices.size() == 3U);
    assert(near(floor.vertices[0].x, 10.0F));
    assert(near(floor.vertices[0].y, 2.0F));
    assert(near(floor.vertices[0].z, -3.0F));
    assert(near(floor.vertices[1].x, 8.0F));
    assert(near(floor.vertices[1].z, -3.0F));
    assert(near(floor.vertices[2].x, 10.0F));
    assert(near(floor.vertices[2].z, -2.5F));
    assert((floor.indices == std::vector<std::uint32_t>{0U, 2U, 1U}));

    const auto cooked = cook_collision(meshes);
    assert(cooked.success);
    assert(cooked.diagnostics.empty());
    assert(cooked.asset_version == 1U);
    assert(cooked.source_fingerprint != 0U);
    assert(!cooked.bytes.empty());
    assert(cooked.bindings.size() == 2U);

    std::reverse(meshes.begin(), meshes.end());
    const auto recooked = cook_collision(meshes);
    assert(recooked.success);
    assert(recooked.asset_version == cooked.asset_version);
    assert(recooked.source_fingerprint == cooked.source_fingerprint);
    assert(recooked.bytes == cooked.bytes);

    const auto* reordered_crate = find_binding(recooked.bindings, "scene/props/crate");
    assert(reordered_crate != nullptr);
    assert(reordered_crate->shape == crate_binding->shape);

    const auto decoded = rigid_body::decode_collision_asset(cooked.bytes);
    assert(decoded.asset.has_value());
    assert(decoded.asset->version == 1U);
    assert(decoded.asset->shapes.size() == 2U);

    auto domain = rigid_body::Domain::create(simulation::DomainId{810}, "imported.collision");
    assert(domain != nullptr);
    std::string registration_diagnostics;
    assert(rigid_body::register_collision_asset(
        *domain,
        *decoded.asset,
        &registration_diagnostics
    ));
    assert(registration_diagnostics.empty());

    rigid_body::BodyDesc floor_body;
    floor_body.participant = simulation::ParticipantId{1};
    floor_body.shape = floor_binding->shape;
    floor_body.motion = rigid_body::MotionType::static_body;
    assert(domain->create_body(floor_body));

    rigid_body::BodyDesc crate_body;
    crate_body.participant = simulation::ParticipantId{2};
    crate_body.shape = crate_binding->shape;
    crate_body.motion = rigid_body::MotionType::dynamic_body;
    crate_body.position = math::Vec3{0.0F, 4.0F, 0.0F};
    assert(domain->create_body(crate_body));

    auto duplicate_key = source_meshes();
    duplicate_key.push_back(duplicate_key[1]);
    duplicate_key.back().collision.shape_id = rigid_body::ShapeId{501};
    assert(!build_collision_shapes(duplicate_key).shapes.has_value());

    auto duplicate_id = source_meshes();
    duplicate_id[2].collision.shape_id = rigid_body::ShapeId{500};
    assert(!build_collision_shapes(duplicate_id).shapes.has_value());

    auto zero_scale = source_meshes();
    zero_scale[2].local_transform.scale.x = 0.0F;
    assert(!build_collision_shapes(zero_scale).shapes.has_value());

    auto bad_index = source_meshes();
    bad_index[1].indices[2] = 99U;
    assert(!build_collision_shapes(bad_index).shapes.has_value());

    auto render_only = source_meshes();
    for (auto& mesh : render_only) {
        mesh.collision = {};
    }
    const auto no_collision = cook_collision(render_only);
    assert(!no_collision.success);
    assert(no_collision.bytes.empty());

    return 0;
}
