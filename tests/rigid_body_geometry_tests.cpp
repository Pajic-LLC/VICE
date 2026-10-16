#include <vice/rigid_body/rigid_body.hpp>
#include <vice/simulation/simulation.hpp>

#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

namespace {

vice::rigid_body::ConvexHullShapeDesc cube_hull(const float half_extent) {
    using vice::math::Vec3;
    return vice::rigid_body::ConvexHullShapeDesc{
        .points = {
            Vec3{-half_extent, -half_extent, -half_extent},
            Vec3{-half_extent, -half_extent, half_extent},
            Vec3{-half_extent, half_extent, -half_extent},
            Vec3{-half_extent, half_extent, half_extent},
            Vec3{half_extent, -half_extent, -half_extent},
            Vec3{half_extent, -half_extent, half_extent},
            Vec3{half_extent, half_extent, -half_extent},
            Vec3{half_extent, half_extent, half_extent},
        },
    };
}

vice::rigid_body::TriangleMeshShapeDesc floor_mesh() {
    using vice::math::Vec3;
    return vice::rigid_body::TriangleMeshShapeDesc{
        .vertices = {
            Vec3{-6.0F, 0.0F, -6.0F},
            Vec3{-6.0F, 0.0F, 6.0F},
            Vec3{6.0F, 0.0F, 6.0F},
            Vec3{6.0F, 0.0F, -6.0F},
        },
        .indices = {0U, 1U, 2U, 0U, 2U, 3U},
    };
}

bool near(const float left, const float right, const float tolerance = 0.08F) {
    return std::abs(left - right) <= tolerance;
}

} // namespace

int main() {
    using namespace vice;

    auto domain = rigid_body::Domain::create(
        simulation::DomainId{400},
        "rigid.geometry"
    );
    assert(domain != nullptr);

    const rigid_body::ShapeId convex_shape{100};
    const rigid_body::ShapeId mesh_shape{200};
    const rigid_body::ShapeId child_box_shape{300};
    const rigid_body::ShapeId child_sphere_shape{301};
    const rigid_body::ShapeId compound_shape{400};
    const rigid_body::ShapeId mesh_compound_shape{401};

    assert(domain->register_shape(convex_shape, cube_hull(0.5F)));
    assert(domain->register_shape(mesh_shape, floor_mesh()));
    assert(domain->register_shape(
        child_box_shape,
        rigid_body::BoxShapeDesc{.half_extents = math::Vec3{0.5F, 0.5F, 0.5F}}
    ));
    assert(domain->register_shape(
        child_sphere_shape,
        rigid_body::SphereShapeDesc{.radius = 0.4F}
    ));

    rigid_body::CompoundShapeDesc compound;
    compound.children.push_back(rigid_body::CompoundShapeChild{
        .shape = child_box_shape,
        .position = math::Vec3{-1.0F, 0.0F, 0.0F},
    });
    compound.children.push_back(rigid_body::CompoundShapeChild{
        .shape = child_sphere_shape,
        .position = math::Vec3{1.0F, 0.0F, 0.0F},
    });
    assert(domain->register_shape(compound_shape, compound));

    rigid_body::CompoundShapeDesc mesh_compound;
    mesh_compound.children.push_back(rigid_body::CompoundShapeChild{
        .shape = mesh_shape,
    });
    mesh_compound.children.push_back(rigid_body::CompoundShapeChild{
        .shape = child_box_shape,
        .position = math::Vec3{0.0F, 1.0F, 0.0F},
    });
    assert(domain->register_shape(mesh_compound_shape, mesh_compound));
    assert(domain->shape_count() == 6U);

    assert(!domain->register_shape(
        rigid_body::ShapeId{500},
        rigid_body::ConvexHullShapeDesc{
            .points = {
                math::Vec3{0.0F, 0.0F, 0.0F},
                math::Vec3{1.0F, 0.0F, 0.0F},
                math::Vec3{2.0F, 0.0F, 0.0F},
                math::Vec3{3.0F, 0.0F, 0.0F},
            },
        }
    ));

    auto invalid_index_mesh = floor_mesh();
    invalid_index_mesh.indices[5] = 99U;
    assert(!domain->register_shape(rigid_body::ShapeId{501}, invalid_index_mesh));

    auto degenerate_mesh = floor_mesh();
    degenerate_mesh.indices = {0U, 0U, 1U};
    assert(!domain->register_shape(rigid_body::ShapeId{502}, degenerate_mesh));

    auto non_finite_mesh = floor_mesh();
    non_finite_mesh.vertices[0].x = std::numeric_limits<float>::infinity();
    assert(!domain->register_shape(rigid_body::ShapeId{503}, non_finite_mesh));

    rigid_body::CompoundShapeDesc missing_child;
    missing_child.children.push_back(rigid_body::CompoundShapeChild{
        .shape = rigid_body::ShapeId{9999},
    });
    assert(!domain->register_shape(rigid_body::ShapeId{504}, missing_child));

    rigid_body::CompoundShapeDesc self_reference;
    self_reference.children.push_back(rigid_body::CompoundShapeChild{
        .shape = rigid_body::ShapeId{505},
    });
    assert(!domain->register_shape(rigid_body::ShapeId{505}, self_reference));

    assert(!domain->unregister_shape(child_box_shape));
    assert(!domain->unregister_shape(child_sphere_shape));
    assert(!domain->unregister_shape(mesh_shape));

    const simulation::ParticipantId floor_participant{1000};
    rigid_body::BodyDesc floor;
    floor.participant = floor_participant;
    floor.shape = mesh_shape;
    floor.motion = rigid_body::MotionType::static_body;
    assert(domain->create_body(floor));

    rigid_body::BodyDesc moving_mesh = floor;
    moving_mesh.participant = simulation::ParticipantId{1001};
    moving_mesh.motion = rigid_body::MotionType::dynamic_body;
    assert(!domain->create_body(moving_mesh));

    rigid_body::BodyDesc moving_mesh_compound;
    moving_mesh_compound.participant = simulation::ParticipantId{1002};
    moving_mesh_compound.shape = mesh_compound_shape;
    moving_mesh_compound.motion = rigid_body::MotionType::dynamic_body;
    assert(!domain->create_body(moving_mesh_compound));

    const simulation::ParticipantId hull_participant{1100};
    rigid_body::BodyDesc hull;
    hull.participant = hull_participant;
    hull.shape = convex_shape;
    hull.motion = rigid_body::MotionType::dynamic_body;
    hull.position = math::Vec3{0.0F, 3.0F, 0.0F};
    hull.friction = 0.7F;
    assert(domain->create_body(hull));

    simulation::Coordinator coordinator{1.0 / 120.0};
    assert(coordinator.register_domain(*domain));
    for (int step = 0; step < 600; ++step) {
        coordinator.step();
    }

    const auto hull_state = domain->body_state(hull_participant);
    assert(hull_state.has_value());
    assert(hull_state->position.y > 0.42F);
    assert(hull_state->position.y < 0.75F);
    assert(std::abs(hull_state->linear_velocity.y) < 0.12F);

    rigid_body::QueryFilter static_only;
    static_only.include_static = true;
    static_only.include_dynamic = false;
    const auto mesh_hit = domain->raycast(
        rigid_body::RaycastDesc{
            .origin = math::Vec3{4.0F, 5.0F, 0.0F},
            .direction = math::Vec3{0.0F, -1.0F, 0.0F},
            .max_distance = 10.0F,
        },
        static_only
    );
    assert(mesh_hit.has_value());
    assert(mesh_hit->participant == floor_participant);
    assert(near(mesh_hit->distance, 5.0F));
    assert(mesh_hit->normal.y > 0.9F);

    const simulation::ParticipantId compound_participant{1200};
    rigid_body::BodyDesc compound_body;
    compound_body.participant = compound_participant;
    compound_body.shape = compound_shape;
    compound_body.motion = rigid_body::MotionType::static_body;
    compound_body.position = math::Vec3{10.0F, 1.0F, 0.0F};
    assert(domain->create_body(compound_body));

    const auto compound_hit = domain->raycast(rigid_body::RaycastDesc{
        .origin = math::Vec3{9.0F, 5.0F, 0.0F},
        .direction = math::Vec3{0.0F, -1.0F, 0.0F},
        .max_distance = 10.0F,
    });
    assert(compound_hit.has_value());
    assert(compound_hit->participant == compound_participant);

    const auto convex_overlaps = domain->overlap_shape(rigid_body::ShapePose{
        .shape = convex_shape,
        .position = math::Vec3{9.0F, 1.0F, 0.0F},
    });
    assert(!convex_overlaps.empty());
    assert(convex_overlaps.front().participant == compound_participant);

    const auto convex_sweep = domain->sweep_shape(rigid_body::SweepDesc{
        .pose = rigid_body::ShapePose{
            .shape = convex_shape,
            .position = math::Vec3{6.0F, 1.0F, 0.0F},
        },
        .direction = math::Vec3{1.0F, 0.0F, 0.0F},
        .max_distance = 8.0F,
    });
    assert(convex_sweep.has_value());
    assert(convex_sweep->participant == compound_participant);

    assert(domain->overlap_shape(rigid_body::ShapePose{
        .shape = mesh_shape,
        .position = math::Vec3{},
    }).empty());
    assert(!domain->sweep_shape(rigid_body::SweepDesc{
        .pose = rigid_body::ShapePose{
            .shape = mesh_shape,
            .position = math::Vec3{},
        },
        .direction = math::Vec3{1.0F, 0.0F, 0.0F},
        .max_distance = 1.0F,
    }).has_value());

    assert(domain->destroy_body(compound_participant));
    assert(domain->destroy_body(hull_participant));
    assert(domain->destroy_body(floor_participant));
    assert(coordinator.unregister_domain(domain->id()));

    assert(domain->unregister_shape(mesh_compound_shape));
    assert(domain->unregister_shape(compound_shape));
    assert(domain->unregister_shape(child_sphere_shape));
    assert(domain->unregister_shape(child_box_shape));
    assert(domain->unregister_shape(mesh_shape));
    assert(domain->unregister_shape(convex_shape));
    assert(domain->shape_count() == 0U);

    return 0;
}
