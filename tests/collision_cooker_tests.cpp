#include <vice/rigid_body/collision_asset.hpp>
#include <vice/tools/collision_cooker.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace {

std::vector<vice::rigid_body::CollisionAssetShape> source_shapes() {
    using namespace vice;

    rigid_body::CompoundShapeDesc compound;
    compound.children.push_back(rigid_body::CompoundShapeChild{
        .shape = rigid_body::ShapeId{10},
        .position = math::Vec3{-1.0F, 0.0F, 0.0F},
    });
    compound.children.push_back(rigid_body::CompoundShapeChild{
        .shape = rigid_body::ShapeId{20},
        .position = math::Vec3{1.0F, 0.0F, 0.0F},
    });

    return {
        rigid_body::CollisionAssetShape{
            .id = rigid_body::ShapeId{50},
            .desc = compound,
        },
        rigid_body::CollisionAssetShape{
            .id = rigid_body::ShapeId{30},
            .desc = rigid_body::ConvexHullShapeDesc{
                .points = {
                    math::Vec3{-0.5F, -0.5F, -0.5F},
                    math::Vec3{-0.5F, -0.5F, 0.5F},
                    math::Vec3{-0.5F, 0.5F, -0.5F},
                    math::Vec3{0.5F, -0.5F, -0.5F},
                    math::Vec3{0.5F, 0.5F, 0.5F},
                },
            },
        },
        rigid_body::CollisionAssetShape{
            .id = rigid_body::ShapeId{20},
            .desc = rigid_body::SphereShapeDesc{.radius = 0.4F},
        },
        rigid_body::CollisionAssetShape{
            .id = rigid_body::ShapeId{40},
            .desc = rigid_body::TriangleMeshShapeDesc{
                .vertices = {
                    math::Vec3{-2.0F, 0.0F, -2.0F},
                    math::Vec3{-2.0F, 0.0F, 2.0F},
                    math::Vec3{2.0F, 0.0F, 2.0F},
                    math::Vec3{2.0F, 0.0F, -2.0F},
                },
                .indices = {0U, 1U, 2U, 0U, 2U, 3U},
            },
        },
        rigid_body::CollisionAssetShape{
            .id = rigid_body::ShapeId{10},
            .desc = rigid_body::BoxShapeDesc{
                .half_extents = math::Vec3{0.5F, 0.75F, 1.0F},
            },
        },
        rigid_body::CollisionAssetShape{
            .id = rigid_body::ShapeId{25},
            .desc = rigid_body::CapsuleShapeDesc{
                .half_height = 0.6F,
                .radius = 0.25F,
            },
        },
    };
}

} // namespace

int main() {
    using namespace vice;

    auto shapes = source_shapes();
    const auto cooked = tools::collision_cooker::cook(shapes);
    assert(cooked.success);
    assert(!cooked.bytes.empty());
    assert(cooked.source_fingerprint != 0U);
    assert(cooked.bytes[8] == std::byte{0x01});

    std::reverse(shapes.begin(), shapes.end());
    const auto recooked = tools::collision_cooker::cook(shapes);
    assert(recooked.success);
    assert(recooked.source_fingerprint == cooked.source_fingerprint);
    assert(recooked.bytes == cooked.bytes);

    const auto decoded = rigid_body::decode_collision_asset(cooked.bytes);
    assert(decoded.asset.has_value());
    assert(decoded.diagnostics.empty());
    assert(decoded.asset->version == rigid_body::collision_asset_format_version_v1);
    assert(decoded.asset->source_fingerprint == cooked.source_fingerprint);
    assert(decoded.asset->shapes.size() == 6U);

    assert(decoded.asset->shapes[0].id == rigid_body::ShapeId{10});
    assert(decoded.asset->shapes[1].id == rigid_body::ShapeId{20});
    assert(decoded.asset->shapes[2].id == rigid_body::ShapeId{25});
    assert(decoded.asset->shapes[3].id == rigid_body::ShapeId{30});
    assert(decoded.asset->shapes[4].id == rigid_body::ShapeId{40});
    assert(decoded.asset->shapes[5].id == rigid_body::ShapeId{50});
    assert(std::holds_alternative<rigid_body::CompoundShapeDesc>(
        decoded.asset->shapes[5].desc
    ));

    auto domain = rigid_body::Domain::create(simulation::DomainId{700}, "cooked.geometry");
    assert(domain != nullptr);
    std::string registration_diagnostics;
    assert(rigid_body::register_collision_asset(
        *domain,
        *decoded.asset,
        &registration_diagnostics
    ));
    assert(registration_diagnostics.empty());
    assert(domain->shape_count() == 6U);

    rigid_body::BodyDesc mesh_body;
    mesh_body.participant = simulation::ParticipantId{1};
    mesh_body.shape = rigid_body::ShapeId{40};
    mesh_body.motion = rigid_body::MotionType::static_body;
    assert(domain->create_body(mesh_body));

    rigid_body::BodyDesc convex_body;
    convex_body.participant = simulation::ParticipantId{2};
    convex_body.shape = rigid_body::ShapeId{30};
    convex_body.motion = rigid_body::MotionType::dynamic_body;
    convex_body.position = math::Vec3{0.0F, 2.0F, 0.0F};
    assert(domain->create_body(convex_body));

    std::string duplicate_diagnostics;
    assert(!rigid_body::register_collision_asset(
        *domain,
        *decoded.asset,
        &duplicate_diagnostics
    ));
    assert(!duplicate_diagnostics.empty());
    assert(domain->shape_count() == 6U);

    auto tampered = cooked.bytes;
    tampered.back() ^= std::byte{0x01};
    const auto tampered_decode = rigid_body::decode_collision_asset(tampered);
    assert(!tampered_decode.asset.has_value());
    assert(!tampered_decode.diagnostics.empty());

    auto wrong_version = cooked.bytes;
    wrong_version[8] = std::byte{0x03};
    wrong_version[9] = std::byte{0x00};
    wrong_version[10] = std::byte{0x00};
    wrong_version[11] = std::byte{0x00};
    const auto version_decode = rigid_body::decode_collision_asset(wrong_version);
    assert(!version_decode.asset.has_value());

    const std::string manifest =
        "# VICE collision source fixture\n"
        "vice-collision-source 1\n"
        "compound 50 2 10 -1 0 0 0 0 0 1 20 1 0 0 0 0 0 1\n"
        "sphere 20 0.4\n"
        "box 10 0.5 0.75 1\n";
    const auto parsed = tools::collision_cooker::parse_source_manifest(manifest);
    assert(parsed.shapes.has_value());
    assert(parsed.shapes->size() == 3U);
    const auto manifest_cooked = tools::collision_cooker::cook(*parsed.shapes);
    assert(manifest_cooked.success);
    assert(manifest_cooked.bytes[8] == std::byte{0x01});

    const auto bad_header = tools::collision_cooker::parse_source_manifest(
        "vice-collision-source 3\nbox 1 1 1 1\n"
    );
    assert(!bad_header.shapes.has_value());

    auto duplicate_shapes = source_shapes();
    duplicate_shapes.push_back(duplicate_shapes.front());
    assert(!tools::collision_cooker::cook(duplicate_shapes).success);

    auto missing_child = source_shapes();
    auto& missing_compound = std::get<rigid_body::CompoundShapeDesc>(
        missing_child.front().desc
    );
    missing_compound.children.front().shape = rigid_body::ShapeId{9999};
    assert(!tools::collision_cooker::cook(missing_child).success);

    std::vector<rigid_body::CollisionAssetShape> cycle;
    cycle.push_back(rigid_body::CollisionAssetShape{
        .id = rigid_body::ShapeId{1},
        .desc = rigid_body::CompoundShapeDesc{
            .children = {
                rigid_body::CompoundShapeChild{.shape = rigid_body::ShapeId{2}},
            },
        },
    });
    cycle.push_back(rigid_body::CollisionAssetShape{
        .id = rigid_body::ShapeId{2},
        .desc = rigid_body::CompoundShapeDesc{
            .children = {
                rigid_body::CompoundShapeChild{.shape = rigid_body::ShapeId{1}},
            },
        },
    });
    assert(!tools::collision_cooker::cook(cycle).success);

    const std::vector<rigid_body::CollisionAssetShape> flat_hull{
        rigid_body::CollisionAssetShape{
            .id = rigid_body::ShapeId{1},
            .desc = rigid_body::ConvexHullShapeDesc{
                .points = {
                    math::Vec3{0.0F, 0.0F, 0.0F},
                    math::Vec3{1.0F, 0.0F, 0.0F},
                    math::Vec3{0.0F, 1.0F, 0.0F},
                    math::Vec3{1.0F, 1.0F, 0.0F},
                },
            },
        },
    };
    assert(!tools::collision_cooker::cook(flat_hull).success);

    assert(domain->destroy_body(simulation::ParticipantId{2}));
    assert(domain->destroy_body(simulation::ParticipantId{1}));
    assert(domain->unregister_shape(rigid_body::ShapeId{50}));
    assert(domain->unregister_shape(rigid_body::ShapeId{40}));
    assert(domain->unregister_shape(rigid_body::ShapeId{30}));
    assert(domain->unregister_shape(rigid_body::ShapeId{25}));
    assert(domain->unregister_shape(rigid_body::ShapeId{20}));
    assert(domain->unregister_shape(rigid_body::ShapeId{10}));
    assert(domain->shape_count() == 0U);

    return 0;
}
