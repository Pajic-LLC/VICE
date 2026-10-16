#include <vice/rigid_body/rigid_body.hpp>

#include <cassert>
#include <cmath>

namespace {

bool near(const float left, const float right, const float tolerance = 0.03F) {
    return std::abs(left - right) <= tolerance;
}

} // namespace

int main() {
    using namespace vice;

    auto domain = rigid_body::Domain::create(
        simulation::DomainId{300},
        "rigid.queries"
    );
    assert(domain != nullptr);

    const rigid_body::ShapeId box_shape{1};
    const rigid_body::ShapeId sphere_shape{2};
    const rigid_body::ShapeId capsule_shape{3};

    assert(domain->register_shape(
        box_shape,
        rigid_body::BoxShapeDesc{.half_extents = math::Vec3{1.0F, 1.0F, 1.0F}}
    ));
    assert(domain->register_shape(
        sphere_shape,
        rigid_body::SphereShapeDesc{.radius = 0.5F}
    ));
    assert(domain->register_shape(
        capsule_shape,
        rigid_body::CapsuleShapeDesc{.half_height = 0.75F, .radius = 0.25F}
    ));
    assert(domain->shape_count() == 3U);
    assert(!domain->register_shape(box_shape, rigid_body::SphereShapeDesc{}));
    assert(!domain->register_shape(
        rigid_body::ShapeId{4},
        rigid_body::SphereShapeDesc{.radius = 0.0F}
    ));
    assert(!domain->register_shape(
        rigid_body::ShapeId{5},
        rigid_body::CapsuleShapeDesc{.half_height = -1.0F, .radius = 0.25F}
    ));

    const simulation::ParticipantId box_participant{10};
    rigid_body::BodyDesc box;
    box.participant = box_participant;
    box.shape = box_shape;
    box.motion = rigid_body::MotionType::static_body;
    box.position = math::Vec3{0.0F, 0.0F, 0.0F};
    assert(domain->create_body(box));

    const simulation::ParticipantId trigger_participant{20};
    rigid_body::BodyDesc trigger;
    trigger.participant = trigger_participant;
    trigger.shape = sphere_shape;
    trigger.motion = rigid_body::MotionType::static_body;
    trigger.position = math::Vec3{0.0F, 3.0F, 0.0F};
    trigger.trigger = true;
    assert(domain->create_body(trigger));

    const simulation::ParticipantId capsule_participant{30};
    rigid_body::BodyDesc capsule;
    capsule.participant = capsule_participant;
    capsule.shape = capsule_shape;
    capsule.motion = rigid_body::MotionType::static_body;
    capsule.position = math::Vec3{3.0F, 0.0F, 0.0F};
    assert(domain->create_body(capsule));

    assert(domain->body_count() == 3U);
    assert(!domain->unregister_shape(box_shape));
    assert(!domain->unregister_shape(sphere_shape));
    assert(!domain->unregister_shape(capsule_shape));

    rigid_body::BodyDesc missing_shape;
    missing_shape.participant = simulation::ParticipantId{99};
    missing_shape.shape = rigid_body::ShapeId{999};
    assert(!domain->create_body(missing_shape));

    const auto trigger_hit = domain->raycast(rigid_body::RaycastDesc{
        .origin = math::Vec3{0.0F, 6.0F, 0.0F},
        .direction = math::Vec3{0.0F, -2.0F, 0.0F},
        .max_distance = 10.0F,
    });
    assert(trigger_hit.has_value());
    assert(trigger_hit->participant == trigger_participant);
    assert(trigger_hit->trigger);
    assert(near(trigger_hit->distance, 2.5F));
    assert(trigger_hit->fraction > 0.0F && trigger_hit->fraction < 1.0F);
    assert(trigger_hit->normal.y > 0.9F);

    rigid_body::QueryFilter solid_only;
    solid_only.include_triggers = false;
    const auto solid_hit = domain->raycast(
        rigid_body::RaycastDesc{
            .origin = math::Vec3{0.0F, 6.0F, 0.0F},
            .direction = math::Vec3{0.0F, -1.0F, 0.0F},
            .max_distance = 10.0F,
        },
        solid_only
    );
    assert(solid_hit.has_value());
    assert(solid_hit->participant == box_participant);
    assert(!solid_hit->trigger);
    assert(near(solid_hit->distance, 5.0F));
    assert(solid_hit->normal.y > 0.9F);

    rigid_body::QueryFilter exclude_box = solid_only;
    exclude_box.exclude_participant = box_participant;
    assert(!domain->raycast(
        rigid_body::RaycastDesc{
            .origin = math::Vec3{0.0F, 6.0F, 0.0F},
            .direction = math::Vec3{0.0F, -1.0F, 0.0F},
            .max_distance = 10.0F,
        },
        exclude_box
    ).has_value());

    rigid_body::QueryFilter dynamic_only;
    dynamic_only.include_static = false;
    dynamic_only.include_dynamic = true;
    assert(!domain->raycast(
        rigid_body::RaycastDesc{
            .origin = math::Vec3{0.0F, 6.0F, 0.0F},
            .direction = math::Vec3{0.0F, -1.0F, 0.0F},
            .max_distance = 10.0F,
        },
        dynamic_only
    ).has_value());

    assert(!domain->raycast(rigid_body::RaycastDesc{
        .origin = math::Vec3{},
        .direction = math::Vec3{},
        .max_distance = 1.0F,
    }).has_value());

    const simulation::ParticipantId overlap_high{50};
    rigid_body::BodyDesc high;
    high.participant = overlap_high;
    high.shape = sphere_shape;
    high.motion = rigid_body::MotionType::static_body;
    high.position = math::Vec3{0.6F, 0.0F, 0.0F};
    assert(domain->create_body(high));

    const simulation::ParticipantId overlap_low{40};
    rigid_body::BodyDesc low;
    low.participant = overlap_low;
    low.shape = sphere_shape;
    low.motion = rigid_body::MotionType::static_body;
    low.position = math::Vec3{-0.6F, 0.0F, 0.0F};
    assert(domain->create_body(low));

    const auto overlaps = domain->overlap_shape(rigid_body::ShapePose{
        .shape = sphere_shape,
        .position = math::Vec3{0.0F, 0.0F, 0.0F},
    }, solid_only);
    assert(overlaps.size() == 3U);
    assert(overlaps[0].participant == box_participant);
    assert(overlaps[1].participant == overlap_low);
    assert(overlaps[2].participant == overlap_high);
    for (const auto& overlap : overlaps) {
        assert(overlap.penetration_depth >= 0.0F);
        assert(overlap.fraction == 0.0F);
        assert(overlap.distance == 0.0F);
    }

    rigid_body::QueryFilter overlap_exclude = solid_only;
    overlap_exclude.exclude_participant = box_participant;
    const auto filtered_overlaps = domain->overlap_shape(rigid_body::ShapePose{
        .shape = sphere_shape,
        .position = math::Vec3{0.0F, 0.0F, 0.0F},
    }, overlap_exclude);
    assert(filtered_overlaps.size() == 2U);
    assert(filtered_overlaps[0].participant == overlap_low);
    assert(filtered_overlaps[1].participant == overlap_high);

    const auto sweep = domain->sweep_shape(
        rigid_body::SweepDesc{
            .pose = rigid_body::ShapePose{
                .shape = sphere_shape,
                .position = math::Vec3{-5.0F, 0.0F, 0.0F},
            },
            .direction = math::Vec3{2.0F, 0.0F, 0.0F},
            .max_distance = 10.0F,
        },
        solid_only
    );
    assert(sweep.has_value());
    assert(sweep->participant == overlap_low);
    assert(sweep->distance > 3.0F && sweep->distance < 4.5F);
    assert(sweep->normal.x < -0.8F);

    assert(!domain->sweep_shape(rigid_body::SweepDesc{
        .pose = rigid_body::ShapePose{
            .shape = rigid_body::ShapeId{999},
            .position = math::Vec3{},
        },
        .direction = math::Vec3{1.0F, 0.0F, 0.0F},
        .max_distance = 10.0F,
    }).has_value());

    assert(domain->destroy_body(overlap_high));
    assert(domain->destroy_body(overlap_low));
    assert(domain->destroy_body(capsule_participant));
    assert(domain->destroy_body(trigger_participant));
    assert(domain->destroy_body(box_participant));
    assert(domain->body_count() == 0U);

    assert(domain->unregister_shape(capsule_shape));
    assert(domain->unregister_shape(sphere_shape));
    assert(domain->unregister_shape(box_shape));
    assert(domain->shape_count() == 0U);

    rigid_body::BoxBodyDesc legacy;
    legacy.participant = simulation::ParticipantId{70};
    legacy.motion = rigid_body::MotionType::static_body;
    legacy.half_extents = math::Vec3{0.5F, 0.5F, 0.5F};
    assert(domain->create_box(legacy));
    assert(domain->destroy_body(legacy.participant));

    return 0;
}
