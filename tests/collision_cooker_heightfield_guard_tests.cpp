#include <vice/rigid_body/collision_asset.hpp>
#include <vice/tools/collision_cooker.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace {

vice::rigid_body::HeightFieldShapeDesc terrain_desc() {
    using namespace vice;

    rigid_body::HeightFieldShapeDesc terrain;
    terrain.sample_count = 4;
    terrain.offset = math::Vec3{-3.0F, 1.0F, -3.0F};
    terrain.sample_spacing_x = 2.0F;
    terrain.sample_spacing_z = 2.0F;
    terrain.vertical_scale = 2.0F;
    terrain.samples = {
        rigid_body::heightfield_hole_sample, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 1.0F, 0.0F,
        0.0F, 1.0F, 2.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 0.0F,
    };
    return terrain;
}

} // namespace

int main() {
    using namespace vice;

    std::vector<rigid_body::CollisionAssetShape> shapes{
        rigid_body::CollisionAssetShape{
            .id = rigid_body::ShapeId{60},
            .desc = terrain_desc(),
        },
        rigid_body::CollisionAssetShape{
            .id = rigid_body::ShapeId{10},
            .desc = rigid_body::SphereShapeDesc{.radius = 0.5F},
        },
    };

    const auto cooked = tools::collision_cooker::cook(shapes);
    assert(cooked.success);
    assert(!cooked.bytes.empty());
    assert(cooked.bytes[8] == std::byte{0x02});
    assert(cooked.source_fingerprint != 0U);

    std::reverse(shapes.begin(), shapes.end());
    const auto recooked = tools::collision_cooker::cook(shapes);
    assert(recooked.success);
    assert(recooked.source_fingerprint == cooked.source_fingerprint);
    assert(recooked.bytes == cooked.bytes);

    const auto decoded = rigid_body::decode_collision_asset(cooked.bytes);
    assert(decoded.asset.has_value());
    assert(decoded.diagnostics.empty());
    assert(decoded.asset->version == rigid_body::collision_asset_format_version_v2);
    assert(decoded.asset->shapes.size() == 2U);
    assert(decoded.asset->shapes[0].id == rigid_body::ShapeId{10});
    assert(decoded.asset->shapes[1].id == rigid_body::ShapeId{60});
    assert(std::holds_alternative<rigid_body::HeightFieldShapeDesc>(
        decoded.asset->shapes[1].desc
    ));

    const auto& terrain = std::get<rigid_body::HeightFieldShapeDesc>(
        decoded.asset->shapes[1].desc
    );
    assert(terrain.sample_count == 4U);
    assert(terrain.offset.x == -3.0F);
    assert(terrain.offset.y == 1.0F);
    assert(terrain.offset.z == -3.0F);
    assert(terrain.sample_spacing_x == 2.0F);
    assert(terrain.sample_spacing_z == 2.0F);
    assert(terrain.vertical_scale == 2.0F);
    assert(terrain.samples.size() == 16U);
    assert(terrain.samples.front() == rigid_body::heightfield_hole_sample);
    assert(terrain.samples[10] == 2.0F);

    auto domain = rigid_body::Domain::create(
        simulation::DomainId{710},
        "cooked.heightfield"
    );
    assert(domain != nullptr);
    std::string registration_diagnostics;
    assert(rigid_body::register_collision_asset(
        *domain,
        *decoded.asset,
        &registration_diagnostics
    ));
    assert(registration_diagnostics.empty());
    assert(domain->shape_count() == 2U);

    rigid_body::BodyDesc terrain_body;
    terrain_body.participant = simulation::ParticipantId{1};
    terrain_body.shape = rigid_body::ShapeId{60};
    terrain_body.motion = rigid_body::MotionType::static_body;
    assert(domain->create_body(terrain_body));

    const auto hit = domain->raycast(rigid_body::RaycastDesc{
        .origin = math::Vec3{1.0F, 10.0F, 1.0F},
        .direction = math::Vec3{0.0F, -1.0F, 0.0F},
        .max_distance = 20.0F,
    });
    assert(hit.has_value());
    assert(hit->participant == terrain_body.participant);

    auto tampered = cooked.bytes;
    tampered.back() ^= std::byte{0x01};
    const auto tampered_decode = rigid_body::decode_collision_asset(tampered);
    assert(!tampered_decode.asset.has_value());
    assert(!tampered_decode.diagnostics.empty());

    auto mislabeled_v1 = cooked.bytes;
    mislabeled_v1[8] = std::byte{0x01};
    const auto v1_tag7_decode = rigid_body::decode_collision_asset(mislabeled_v1);
    assert(!v1_tag7_decode.asset.has_value());
    assert(!v1_tag7_decode.diagnostics.empty());

    rigid_body::CollisionAsset invalid_v1;
    invalid_v1.version = rigid_body::collision_asset_format_version_v1;
    invalid_v1.shapes.push_back(rigid_body::CollisionAssetShape{
        .id = rigid_body::ShapeId{100},
        .desc = terrain_desc(),
    });
    std::string invalid_v1_diagnostics;
    assert(!rigid_body::register_collision_asset(
        *domain,
        invalid_v1,
        &invalid_v1_diagnostics
    ));
    assert(!invalid_v1_diagnostics.empty());

    const std::string manifest =
        "vice-collision-source 2\n"
        "sphere 10 0.5\n"
        "heightfield 60 4 -3 1 -3 2 2 2 "
        "hole 0 0 0 0 1 1 0 0 1 2 0 0 0 0 0\n";
    const auto parsed = tools::collision_cooker::parse_source_manifest(manifest);
    assert(parsed.shapes.has_value());
    assert(parsed.shapes->size() == 2U);
    const auto manifest_cooked = tools::collision_cooker::cook(*parsed.shapes);
    assert(manifest_cooked.success);
    assert(manifest_cooked.bytes[8] == std::byte{0x02});

    const auto v1_heightfield_manifest = tools::collision_cooker::parse_source_manifest(
        "vice-collision-source 1\n"
        "heightfield 1 4 0 0 0 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
    );
    assert(!v1_heightfield_manifest.shapes.has_value());

    auto all_holes = terrain_desc();
    all_holes.samples.assign(16U, rigid_body::heightfield_hole_sample);
    const std::array invalid_shapes{
        rigid_body::CollisionAssetShape{
            .id = rigid_body::ShapeId{1},
            .desc = all_holes,
        },
    };
    assert(!tools::collision_cooker::cook(invalid_shapes).success);

    assert(domain->destroy_body(terrain_body.participant));
    assert(domain->unregister_shape(rigid_body::ShapeId{60}));
    assert(domain->unregister_shape(rigid_body::ShapeId{10}));
    assert(domain->shape_count() == 0U);

    return 0;
}
