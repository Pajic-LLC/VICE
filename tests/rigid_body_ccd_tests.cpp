#include <vice/rigid_body/rigid_body.hpp>
#include <vice/simulation/simulation.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>

namespace {

using namespace vice;

void add_thin_wall(rigid_body::Domain& domain) {
    rigid_body::BoxBodyDesc wall;
    wall.participant = simulation::ParticipantId{1};
    wall.motion = rigid_body::MotionType::static_body;
    wall.half_extents = math::Vec3{0.025F, 4.0F, 4.0F};
    wall.position = math::Vec3{0.0F, 0.0F, 0.0F};
    assert(domain.create_box(wall));
}

rigid_body::BoxBodyDesc projectile(
    const simulation::ParticipantId participant,
    const rigid_body::MotionQuality motion_quality
) {
    rigid_body::BoxBodyDesc body;
    body.participant = participant;
    body.motion = rigid_body::MotionType::dynamic_body;
    body.motion_quality = motion_quality;
    body.half_extents = math::Vec3{0.05F, 0.05F, 0.05F};
    body.position = math::Vec3{-1.0F, 0.0F, 0.0F};
    body.linear_velocity = math::Vec3{240.0F, 0.0F, 0.0F};
    body.friction = 0.0F;
    body.restitution = 0.0F;
    return body;
}

} // namespace

int main() {
    using namespace vice;

    auto rigid = rigid_body::Domain::create(simulation::DomainId{75}, "ccd");
    assert(rigid != nullptr);

    simulation::Coordinator coordinator{1.0 / 120.0};
    assert(coordinator.register_domain(*rigid));
    add_thin_wall(*rigid);

    const simulation::ParticipantId discrete_id{2};
    auto discrete = projectile(discrete_id, rigid_body::MotionQuality::discrete);
    assert(rigid->create_box(discrete));

    const auto discrete_before = rigid->body_state(discrete_id);
    assert(discrete_before.has_value());
    assert(discrete_before->motion_quality == rigid_body::MotionQuality::discrete);

    coordinator.step();

    const auto discrete_after = rigid->body_state(discrete_id);
    assert(discrete_after.has_value());
    assert(discrete_after->motion_quality == rigid_body::MotionQuality::discrete);
    const bool discrete_crossed_wall = discrete_after->position.x > 0.10F;
    assert(rigid->destroy_body(discrete_id));

    const simulation::ParticipantId continuous_id{3};
    auto continuous = projectile(continuous_id, rigid_body::MotionQuality::continuous);
    assert(rigid->create_box(continuous));

    const auto continuous_before = rigid->body_state(continuous_id);
    assert(continuous_before.has_value());
    assert(continuous_before->motion_quality == rigid_body::MotionQuality::continuous);

    coordinator.step();

    const auto continuous_after = rigid->body_state(continuous_id);
    assert(continuous_after.has_value());
    assert(continuous_after->motion_quality == rigid_body::MotionQuality::continuous);
    assert(std::isfinite(continuous_after->position.x));
    assert(continuous_after->position.x < 0.10F);
    if (discrete_crossed_wall) {
        assert(continuous_after->position.x < discrete_after->position.x);
    }
    assert(rigid->destroy_body(continuous_id));

    const simulation::ParticipantId runtime_id{4};
    auto runtime = projectile(runtime_id, rigid_body::MotionQuality::discrete);
    assert(rigid->create_box(runtime));
    assert(rigid->body_state(runtime_id)->motion_quality == rigid_body::MotionQuality::discrete);
    assert(rigid->set_body_motion_quality(runtime_id, rigid_body::MotionQuality::continuous));
    assert(rigid->body_state(runtime_id)->motion_quality == rigid_body::MotionQuality::continuous);

    coordinator.step();

    const auto runtime_after = rigid->body_state(runtime_id);
    assert(runtime_after.has_value());
    assert(runtime_after->position.x < 0.10F);
    assert(rigid->set_body_motion_quality(runtime_id, rigid_body::MotionQuality::discrete));
    assert(rigid->body_state(runtime_id)->motion_quality == rigid_body::MotionQuality::discrete);
    assert(!rigid->set_body_motion_quality(
        runtime_id,
        static_cast<rigid_body::MotionQuality>(255)
    ));
    assert(rigid->destroy_body(runtime_id));

    rigid_body::BoxBodyDesc invalid_static;
    invalid_static.participant = simulation::ParticipantId{5};
    invalid_static.motion = rigid_body::MotionType::static_body;
    invalid_static.motion_quality = rigid_body::MotionQuality::continuous;
    assert(!rigid->create_box(invalid_static));
    assert(!rigid->body_state(invalid_static.participant).has_value());

    const rigid_body::ShapeId sphere_shape{10};
    assert(rigid->register_shape(sphere_shape, rigid_body::SphereShapeDesc{0.05F}));

    rigid_body::BodyDesc shaped;
    shaped.participant = simulation::ParticipantId{6};
    shaped.shape = sphere_shape;
    shaped.motion = rigid_body::MotionType::dynamic_body;
    shaped.motion_quality = rigid_body::MotionQuality::continuous;
    shaped.position = math::Vec3{-1.0F, 2.0F, 0.0F};
    shaped.linear_velocity = math::Vec3{240.0F, 0.0F, 0.0F};
    assert(rigid->create_body(shaped));
    assert(rigid->body_state(shaped.participant)->motion_quality ==
        rigid_body::MotionQuality::continuous);
    assert(rigid->destroy_body(shaped.participant));
    assert(rigid->unregister_shape(sphere_shape));

    assert(rigid->destroy_body(simulation::ParticipantId{1}));
    assert(coordinator.unregister_domain(rigid->id()));
    return 0;
}
