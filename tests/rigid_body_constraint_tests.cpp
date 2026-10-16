#include <vice/rigid_body/rigid_body.hpp>
#include <vice/simulation/simulation.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>

namespace {

using namespace vice;

void step(rigid_body::Domain& domain, const std::size_t count) {
    simulation::Coordinator coordinator{1.0 / 120.0};
    assert(coordinator.register_domain(domain));
    for (std::size_t index = 0; index < count; ++index) {
        coordinator.step();
    }
    assert(coordinator.unregister_domain(domain.id()));
}

void create_box(
    rigid_body::Domain& domain,
    const simulation::ParticipantId participant,
    const rigid_body::MotionType motion,
    const math::Vec3 position,
    const math::Vec3 half_extents = math::Vec3{0.25F, 0.25F, 0.25F},
    const math::Vec3 linear_velocity = {},
    const math::Vec3 angular_velocity = {}
) {
    rigid_body::BoxBodyDesc body;
    body.participant = participant;
    body.motion = motion;
    body.position = position;
    body.half_extents = half_extents;
    body.linear_velocity = linear_velocity;
    body.angular_velocity = angular_velocity;
    assert(domain.create_box(body));
}

void test_fixed_constraint() {
    auto domain = rigid_body::Domain::create(simulation::DomainId{710}, "constraints.fixed");
    assert(domain);

    const simulation::ParticipantId anchor{1};
    const simulation::ParticipantId payload{2};
    create_box(*domain, anchor, rigid_body::MotionType::static_body, math::Vec3{0.0F, 2.0F, 2.0F});
    create_box(*domain, payload, rigid_body::MotionType::dynamic_body, math::Vec3{1.0F, 2.0F, 0.0F});

    rigid_body::FixedConstraintDesc joint;
    joint.id = rigid_body::ConstraintId{1001};
    joint.first = anchor;
    joint.second = payload;
    assert(domain->create_constraint(joint));
    assert(!domain->create_constraint(joint));
    assert(domain->constraint_count() == 1U);

    const auto state = domain->constraint_state(joint.id);
    assert(state.has_value());
    assert(state->type == rigid_body::ConstraintType::fixed);
    assert(state->first == anchor);
    assert(state->second == payload);
    assert(state->enabled);
    assert(!state->coordinate.has_value());

    step(*domain, 240U);
    const auto body = domain->body_state(payload);
    assert(body.has_value());
    assert(std::abs(body->position.x - 1.0F) < 0.08F);
    assert(std::abs(body->position.y - 2.0F) < 0.08F);
    assert(std::abs(body->position.z) < 0.08F);

    const auto loaded = domain->constraint_state(joint.id);
    assert(loaded.has_value());
    assert(std::isfinite(loaded->linear_impulse));
    assert(std::isfinite(loaded->angular_impulse));
    assert(loaded->linear_impulse >= 0.0F);
    assert(loaded->angular_impulse >= 0.0F);

    assert(!domain->destroy_body(payload));
    assert(domain->set_constraint_enabled(joint.id, false));
    assert(!domain->destroy_body(payload));
    assert(domain->destroy_constraint(joint.id));
    assert(!domain->destroy_constraint(joint.id));
    assert(domain->constraint_count() == 0U);
    assert(domain->destroy_body(payload));
    assert(domain->destroy_body(anchor));
}

void test_hinge_constraint() {
    auto domain = rigid_body::Domain::create(simulation::DomainId{711}, "constraints.hinge");
    assert(domain);

    const simulation::ParticipantId anchor{11};
    const simulation::ParticipantId door{12};
    create_box(*domain, anchor, rigid_body::MotionType::static_body, math::Vec3{0.0F, 0.0F, 2.0F});
    create_box(
        *domain,
        door,
        rigid_body::MotionType::dynamic_body,
        math::Vec3{1.0F, 0.0F, 0.0F},
        math::Vec3{0.35F, 0.8F, 0.12F},
        {},
        math::Vec3{0.0F, 5.0F, 0.0F}
    );

    rigid_body::HingeConstraintDesc invalid;
    invalid.id = rigid_body::ConstraintId{2000};
    invalid.first = anchor;
    invalid.second = door;
    invalid.axis = math::Vec3{0.0F, 1.0F, 0.0F};
    invalid.normal = math::Vec3{0.0F, 2.0F, 0.0F};
    assert(!domain->create_constraint(invalid));

    rigid_body::HingeConstraintDesc joint;
    joint.id = rigid_body::ConstraintId{2001};
    joint.first = anchor;
    joint.second = door;
    joint.anchor = math::Vec3{0.0F, 0.0F, 0.0F};
    joint.axis = math::Vec3{0.0F, 1.0F, 0.0F};
    joint.normal = math::Vec3{1.0F, 0.0F, 0.0F};
    joint.min_angle = -0.35F;
    joint.max_angle = 0.35F;
    joint.limits_spring.frequency_hz = 5.0F;
    joint.limits_spring.damping_ratio = 0.8F;
    joint.max_friction_torque = 0.1F;
    assert(domain->create_constraint(joint));

    step(*domain, 720U);
    const auto state = domain->constraint_state(joint.id);
    assert(state.has_value());
    assert(state->type == rigid_body::ConstraintType::hinge);
    assert(state->coordinate.has_value());
    assert(std::isfinite(*state->coordinate));
    assert(*state->coordinate >= -0.50F);
    assert(*state->coordinate <= 0.50F);

    const auto body = domain->body_state(door);
    assert(body.has_value());
    assert(std::isfinite(body->orientation.x));
    assert(std::isfinite(body->orientation.y));
    assert(std::isfinite(body->orientation.z));
    assert(std::isfinite(body->orientation.w));

    assert(domain->destroy_constraint(joint.id));
    assert(domain->destroy_body(door));
    assert(domain->destroy_body(anchor));
}

void test_slider_constraint() {
    auto domain = rigid_body::Domain::create(simulation::DomainId{712}, "constraints.slider");
    assert(domain);

    const simulation::ParticipantId anchor{21};
    const simulation::ParticipantId carriage{22};
    create_box(*domain, anchor, rigid_body::MotionType::static_body, math::Vec3{0.0F, 0.0F, 2.0F});
    create_box(
        *domain,
        carriage,
        rigid_body::MotionType::dynamic_body,
        math::Vec3{0.0F, 0.0F, 0.0F},
        math::Vec3{0.3F, 0.3F, 0.3F},
        math::Vec3{5.0F, 0.0F, 0.0F}
    );

    rigid_body::SliderConstraintDesc invalid;
    invalid.id = rigid_body::ConstraintId{3000};
    invalid.first = anchor;
    invalid.second = carriage;
    invalid.axis = math::Vec3{};
    assert(!domain->create_constraint(invalid));

    rigid_body::SliderConstraintDesc joint;
    joint.id = rigid_body::ConstraintId{3001};
    joint.first = anchor;
    joint.second = carriage;
    joint.anchor = math::Vec3{0.0F, 0.0F, 0.0F};
    joint.axis = math::Vec3{1.0F, 0.0F, 0.0F};
    joint.min_position = -1.0F;
    joint.max_position = 1.0F;
    joint.limits_spring.frequency_hz = 6.0F;
    joint.limits_spring.damping_ratio = 0.8F;
    joint.max_friction_force = 0.05F;
    assert(domain->create_constraint(joint));

    step(*domain, 720U);
    const auto state = domain->constraint_state(joint.id);
    assert(state.has_value());
    assert(state->type == rigid_body::ConstraintType::slider);
    assert(state->coordinate.has_value());
    assert(std::isfinite(*state->coordinate));
    assert(*state->coordinate >= -1.15F);
    assert(*state->coordinate <= 1.15F);

    const auto body = domain->body_state(carriage);
    assert(body.has_value());
    assert(std::abs(body->position.y) < 0.15F);
    assert(std::abs(body->position.z) < 0.15F);

    assert(domain->destroy_constraint(joint.id));
    assert(domain->destroy_body(carriage));
    assert(domain->destroy_body(anchor));
}

void test_distance_constraint() {
    auto domain = rigid_body::Domain::create(simulation::DomainId{713}, "constraints.distance");
    assert(domain);

    const simulation::ParticipantId anchor{31};
    const simulation::ParticipantId payload{32};
    create_box(*domain, anchor, rigid_body::MotionType::static_body, math::Vec3{0.0F, 5.0F, 0.0F});
    create_box(*domain, payload, rigid_body::MotionType::dynamic_body, math::Vec3{0.0F, 2.0F, 0.0F});

    rigid_body::DistanceConstraintDesc invalid;
    invalid.id = rigid_body::ConstraintId{4000};
    invalid.first = anchor;
    invalid.second = payload;
    invalid.min_distance = 2.0F;
    assert(!domain->create_constraint(invalid));

    rigid_body::DistanceConstraintDesc joint;
    joint.id = rigid_body::ConstraintId{4001};
    joint.first = anchor;
    joint.second = payload;
    joint.min_distance = 2.9F;
    joint.max_distance = 3.1F;
    joint.limits_spring.frequency_hz = 5.0F;
    joint.limits_spring.damping_ratio = 0.8F;
    assert(domain->create_constraint(joint));

    step(*domain, 720U);
    const auto anchor_state = domain->body_state(anchor);
    const auto payload_state = domain->body_state(payload);
    assert(anchor_state.has_value());
    assert(payload_state.has_value());
    const float dx = payload_state->position.x - anchor_state->position.x;
    const float dy = payload_state->position.y - anchor_state->position.y;
    const float dz = payload_state->position.z - anchor_state->position.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    assert(distance >= 2.75F);
    assert(distance <= 3.25F);

    const auto state = domain->constraint_state(joint.id);
    assert(state.has_value());
    assert(state->type == rigid_body::ConstraintType::distance);
    assert(!state->coordinate.has_value());
    assert(std::isfinite(state->linear_impulse));

    assert(domain->destroy_constraint(joint.id));
    assert(domain->destroy_body(payload));
    assert(domain->destroy_body(anchor));
}

void test_hinge_motor_controls() {
    auto domain = rigid_body::Domain::create(simulation::DomainId{715}, "constraints.hinge_motor");
    assert(domain);

    const simulation::ParticipantId anchor{51};
    const simulation::ParticipantId door{52};
    create_box(*domain, anchor, rigid_body::MotionType::static_body, math::Vec3{0.0F, 0.0F, 0.0F});
    create_box(
        *domain,
        door,
        rigid_body::MotionType::dynamic_body,
        math::Vec3{1.0F, 0.0F, 0.0F},
        math::Vec3{0.35F, 0.8F, 0.12F}
    );

    rigid_body::HingeConstraintDesc joint;
    joint.id = rigid_body::ConstraintId{6001};
    joint.first = anchor;
    joint.second = door;
    joint.anchor = math::Vec3{0.0F, 0.0F, 0.0F};
    joint.min_angle = -1.2F;
    joint.max_angle = 1.2F;
    joint.motor.mode = rigid_body::ConstraintMotorMode::velocity;
    joint.motor.target_angular_velocity = 1.5F;
    joint.motor.max_torque = 80.0F;
    assert(domain->create_constraint(joint));

    step(*domain, 40U);
    auto state = domain->constraint_state(joint.id);
    const auto velocity_body = domain->body_state(door);
    assert(state.has_value());
    assert(velocity_body.has_value());
    assert(state->motor_mode == rigid_body::ConstraintMotorMode::velocity);
    assert(state->coordinate.has_value());
    assert(std::abs(velocity_body->angular_velocity.y) > 0.50F);
    assert(std::isfinite(state->motor_impulse));
    assert(state->motor_impulse >= 0.0F);

    rigid_body::HingeMotorDesc invalid_motor;
    invalid_motor.mode = rigid_body::ConstraintMotorMode::velocity;
    invalid_motor.target_angular_velocity = 1.0F;
    invalid_motor.max_torque = 0.0F;
    assert(!domain->set_hinge_motor(joint.id, invalid_motor));

    rigid_body::HingeMotorDesc position_motor;
    position_motor.mode = rigid_body::ConstraintMotorMode::position;
    position_motor.target_angle = -0.5F;
    position_motor.drive_spring.frequency_hz = 4.0F;
    position_motor.drive_spring.damping_ratio = 1.0F;
    position_motor.max_torque = 120.0F;
    assert(domain->set_hinge_motor(joint.id, position_motor));
    assert(!domain->set_slider_motor(joint.id, rigid_body::SliderMotorDesc{}));

    step(*domain, 240U);
    state = domain->constraint_state(joint.id);
    assert(state.has_value());
    assert(state->motor_mode == rigid_body::ConstraintMotorMode::position);
    assert(state->coordinate.has_value());
    assert(std::abs(*state->coordinate - position_motor.target_angle) < 0.18F);
    assert(std::isfinite(state->linear_impulse));
    assert(std::isfinite(state->angular_impulse));
    assert(std::isfinite(state->limit_impulse));
    assert(std::isfinite(state->motor_impulse));

    assert(domain->destroy_constraint(joint.id));
    assert(domain->destroy_body(door));
    assert(domain->destroy_body(anchor));
}

void test_slider_motor_controls() {
    auto domain = rigid_body::Domain::create(simulation::DomainId{716}, "constraints.slider_motor");
    assert(domain);

    const simulation::ParticipantId anchor{61};
    const simulation::ParticipantId carriage{62};
    create_box(*domain, anchor, rigid_body::MotionType::static_body, math::Vec3{0.0F, 0.0F, 0.0F});
    create_box(*domain, carriage, rigid_body::MotionType::dynamic_body, math::Vec3{0.0F, 0.0F, 0.0F});

    rigid_body::SliderConstraintDesc joint;
    joint.id = rigid_body::ConstraintId{7001};
    joint.first = anchor;
    joint.second = carriage;
    joint.axis = math::Vec3{1.0F, 0.0F, 0.0F};
    joint.min_position = -2.0F;
    joint.max_position = 2.0F;
    joint.motor.mode = rigid_body::ConstraintMotorMode::velocity;
    joint.motor.target_velocity = 1.0F;
    joint.motor.max_force = 100.0F;
    assert(domain->create_constraint(joint));

    step(*domain, 40U);
    auto state = domain->constraint_state(joint.id);
    const auto velocity_body = domain->body_state(carriage);
    assert(state.has_value());
    assert(velocity_body.has_value());
    assert(state->motor_mode == rigid_body::ConstraintMotorMode::velocity);
    assert(state->coordinate.has_value());
    assert(std::abs(velocity_body->linear_velocity.x) > 0.30F);
    assert(std::isfinite(state->motor_impulse));

    rigid_body::SliderMotorDesc position_motor;
    position_motor.mode = rigid_body::ConstraintMotorMode::position;
    position_motor.target_position = -0.75F;
    position_motor.drive_spring.frequency_hz = 4.0F;
    position_motor.drive_spring.damping_ratio = 1.0F;
    position_motor.max_force = 160.0F;
    assert(domain->set_slider_motor(joint.id, position_motor));

    step(*domain, 240U);
    state = domain->constraint_state(joint.id);
    assert(state.has_value());
    assert(state->motor_mode == rigid_body::ConstraintMotorMode::position);
    assert(state->coordinate.has_value());
    assert(std::abs(*state->coordinate - position_motor.target_position) < 0.18F);
    assert(std::isfinite(state->linear_impulse));
    assert(std::isfinite(state->angular_impulse));

    assert(domain->destroy_constraint(joint.id));
    assert(domain->destroy_body(carriage));
    assert(domain->destroy_body(anchor));
}

void test_breakable_constraints() {
    auto domain = rigid_body::Domain::create(simulation::DomainId{717}, "constraints.breakable");
    assert(domain);

    const simulation::ParticipantId anchor_a{71};
    const simulation::ParticipantId payload_a{72};
    const simulation::ParticipantId anchor_b{73};
    const simulation::ParticipantId payload_b{74};
    create_box(*domain, anchor_a, rigid_body::MotionType::static_body, math::Vec3{-2.0F, 3.0F, 0.0F});
    create_box(*domain, payload_a, rigid_body::MotionType::dynamic_body, math::Vec3{-2.0F, 3.0F, 0.0F});
    create_box(*domain, anchor_b, rigid_body::MotionType::static_body, math::Vec3{2.0F, 3.0F, 0.0F});
    create_box(*domain, payload_b, rigid_body::MotionType::dynamic_body, math::Vec3{2.0F, 3.0F, 0.0F});

    rigid_body::FixedConstraintDesc invalid;
    invalid.id = rigid_body::ConstraintId{8000};
    invalid.first = anchor_a;
    invalid.second = payload_a;
    invalid.break_thresholds.linear_impulse = 0.0F;
    assert(!domain->create_constraint(invalid));

    rigid_body::FixedConstraintDesc high_id;
    high_id.id = rigid_body::ConstraintId{8002};
    high_id.first = anchor_a;
    high_id.second = payload_a;
    high_id.break_thresholds.linear_impulse = 0.000001F;
    assert(domain->create_constraint(high_id));

    rigid_body::FixedConstraintDesc low_id;
    low_id.id = rigid_body::ConstraintId{8001};
    low_id.first = anchor_b;
    low_id.second = payload_b;
    low_id.break_thresholds.linear_impulse = 0.000001F;
    assert(domain->create_constraint(low_id));
    assert(domain->constraint_count() == 2U);
    assert(!domain->destroy_body(payload_a));
    assert(!domain->destroy_body(payload_b));

    step(*domain, 1U);
    assert(domain->constraint_count() == 0U);
    assert(!domain->constraint_state(low_id.id).has_value());
    assert(!domain->constraint_state(high_id.id).has_value());

    const auto events = domain->constraint_break_events();
    assert(events.size() == 2U);
    assert(events[0].id == low_id.id);
    assert(events[1].id == high_id.id);
    assert(events[0].type == rigid_body::ConstraintType::fixed);
    assert(events[1].type == rigid_body::ConstraintType::fixed);
    assert(events[0].linear_impulse > 0.000001F);
    assert(events[1].linear_impulse > 0.000001F);
    assert(std::isfinite(events[0].angular_impulse));
    assert(std::isfinite(events[1].angular_impulse));

    assert(domain->destroy_body(payload_a));
    assert(domain->destroy_body(payload_b));
    assert(domain->destroy_body(anchor_a));
    assert(domain->destroy_body(anchor_b));
}

void test_domain_shutdown_with_live_constraint() {
    auto domain = rigid_body::Domain::create(simulation::DomainId{714}, "constraints.shutdown");
    assert(domain);
    create_box(
        *domain,
        simulation::ParticipantId{41},
        rigid_body::MotionType::static_body,
        math::Vec3{0.0F, 0.0F, 2.0F}
    );
    create_box(
        *domain,
        simulation::ParticipantId{42},
        rigid_body::MotionType::dynamic_body,
        math::Vec3{0.0F, 0.0F, 0.0F}
    );

    rigid_body::FixedConstraintDesc joint;
    joint.id = rigid_body::ConstraintId{5001};
    joint.first = simulation::ParticipantId{41};
    joint.second = simulation::ParticipantId{42};
    assert(domain->create_constraint(joint));
    assert(domain->constraint_count() == 1U);
}

} // namespace

int main() {
    test_fixed_constraint();
    test_hinge_constraint();
    test_slider_constraint();
    test_distance_constraint();
    test_hinge_motor_controls();
    test_slider_motor_controls();
    test_breakable_constraints();
    test_domain_shutdown_with_live_constraint();
    return 0;
}
