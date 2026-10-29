#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vice::rigid_body {

struct Domain::ConstraintRuntimeState {
    std::unordered_map<std::uint64_t, ConstraintBreakThresholds> break_thresholds;
    std::vector<ConstraintBreakEvent> break_events;
    std::unordered_map<std::uint64_t, std::array<JPH::BodyID, 2>> excluded_pairs;
};

namespace {

struct NativeConstraintImpulse {
    float linear = 0.0F;
    float angular = 0.0F;
    float limit = 0.0F;
    float motor = 0.0F;
};

[[nodiscard]] bool valid_constraint_spring(const ConstraintSpring spring) noexcept {
    return std::isfinite(spring.frequency_hz) && spring.frequency_hz >= 0.0F &&
        std::isfinite(spring.damping_ratio) && spring.damping_ratio >= 0.0F;
}

[[nodiscard]] bool valid_break_thresholds(
    const ConstraintBreakThresholds& thresholds
) noexcept {
    return (!thresholds.linear_impulse.has_value() ||
            valid_positive(*thresholds.linear_impulse)) &&
        (!thresholds.angular_impulse.has_value() ||
            valid_positive(*thresholds.angular_impulse));
}

[[nodiscard]] bool motor_uses_position(const ConstraintMotorMode mode) noexcept {
    return mode == ConstraintMotorMode::position ||
        mode == ConstraintMotorMode::position_and_velocity;
}

[[nodiscard]] bool valid_hinge_motor(const HingeMotorDesc& motor) noexcept {
    if (!std::isfinite(motor.target_angular_velocity) || !std::isfinite(motor.target_angle) ||
        !valid_constraint_spring(motor.drive_spring) ||
        !valid_nonnegative(motor.max_torque)) {
        return false;
    }
    if (motor.mode != ConstraintMotorMode::off && !valid_positive(motor.max_torque)) {
        return false;
    }
    if (motor_uses_position(motor.mode) && motor.drive_spring.frequency_hz <= 0.0F) {
        return false;
    }
    return true;
}

[[nodiscard]] bool valid_slider_motor(const SliderMotorDesc& motor) noexcept {
    if (!std::isfinite(motor.target_velocity) || !std::isfinite(motor.target_position) ||
        !valid_constraint_spring(motor.drive_spring) ||
        !valid_nonnegative(motor.max_force)) {
        return false;
    }
    if (motor.mode != ConstraintMotorMode::off && !valid_positive(motor.max_force)) {
        return false;
    }
    if (motor_uses_position(motor.mode) && motor.drive_spring.frequency_hz <= 0.0F) {
        return false;
    }
    return true;
}

[[nodiscard]] JPH::SpringSettings jolt_spring(const ConstraintSpring spring) noexcept {
    return JPH::SpringSettings{
        JPH::ESpringMode::FrequencyAndDamping,
        spring.frequency_hz,
        spring.damping_ratio,
    };
}

[[nodiscard]] JPH::EMotorState jolt_motor_mode(const ConstraintMotorMode mode) noexcept {
    switch (mode) {
    case ConstraintMotorMode::velocity:
        return JPH::EMotorState::Velocity;
    case ConstraintMotorMode::position:
        return JPH::EMotorState::Position;
    case ConstraintMotorMode::position_and_velocity:
        return JPH::EMotorState::PositionAndVelocity;
    case ConstraintMotorMode::off:
    default:
        return JPH::EMotorState::Off;
    }
}

[[nodiscard]] ConstraintMotorMode vice_motor_mode(const JPH::EMotorState mode) noexcept {
    switch (mode) {
    case JPH::EMotorState::Velocity:
        return ConstraintMotorMode::velocity;
    case JPH::EMotorState::Position:
        return ConstraintMotorMode::position;
    case JPH::EMotorState::PositionAndVelocity:
        return ConstraintMotorMode::position_and_velocity;
    case JPH::EMotorState::Off:
    default:
        return ConstraintMotorMode::off;
    }
}

[[nodiscard]] JPH::Vec3 normalized_jolt_vector(const math::Vec3 value) noexcept {
    const float inverse_length = 1.0F / std::sqrt(length_squared(value));
    return JPH::Vec3{
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
    };
}

[[nodiscard]] bool perpendicular_axes(
    const math::Vec3 first,
    const math::Vec3 second
) noexcept {
    if (!finite(first) || !finite(second) ||
        length_squared(first) <= 0.000001F || length_squared(second) <= 0.000001F) {
        return false;
    }
    const float inverse_lengths = 1.0F /
        std::sqrt(length_squared(first) * length_squared(second));
    const float dot = (first.x * second.x + first.y * second.y + first.z * second.z) *
        inverse_lengths;
    return std::abs(dot) <= 0.001F;
}

[[nodiscard]] JPH::uint32 constraint_priority(const ConstraintId id) noexcept {
    return static_cast<JPH::uint32>(id.value ^ (id.value >> 32U));
}

template <typename ImplType>
[[nodiscard]] JPH::Constraint* find_constraint(
    ImplType& impl,
    const ConstraintId id
) noexcept {
    if (!id) {
        return nullptr;
    }
    const auto constraints = impl.physics_system_.GetConstraints();
    for (const auto& constraint : constraints) {
        if (constraint != nullptr && constraint->GetUserData() == id.value) {
            return constraint.GetPtr();
        }
    }
    return nullptr;
}

template <typename ImplType>
[[nodiscard]] const JPH::Constraint* find_constraint(
    const ImplType& impl,
    const ConstraintId id
) noexcept {
    if (!id) {
        return nullptr;
    }
    const auto constraints = impl.physics_system_.GetConstraints();
    for (const auto& constraint : constraints) {
        if (constraint != nullptr && constraint->GetUserData() == id.value) {
            return constraint.GetPtr();
        }
    }
    return nullptr;
}

template <typename ImplType>
[[nodiscard]] bool valid_constraint_pair(
    const ImplType& impl,
    const ConstraintId id,
    const simulation::ParticipantId first,
    const simulation::ParticipantId second
) noexcept {
    if (!id || !first || !second || first == second || find_constraint(impl, id) != nullptr) {
        return false;
    }
    const auto* first_body = impl.find(first);
    const auto* second_body = impl.find(second);
    if (first_body == nullptr || second_body == nullptr) {
        return false;
    }
    return first_body->motion == MotionType::dynamic_body ||
        second_body->motion == MotionType::dynamic_body;
}

template <typename ImplType, typename SettingsType>
[[nodiscard]] bool add_constraint(
    ImplType& impl,
    const ConstraintId id,
    const simulation::ParticipantId first,
    const simulation::ParticipantId second,
    SettingsType& settings
) {
    const auto* first_entry = impl.find(first);
    const auto* second_entry = impl.find(second);
    if (first_entry == nullptr || second_entry == nullptr) {
        return false;
    }

    const std::array body_ids{first_entry->body_id, second_entry->body_id};
    JPH::BodyLockMultiWrite lock{
        impl.physics_system_.GetBodyLockInterface(),
        body_ids.data(),
        static_cast<int>(body_ids.size())
    };
    auto* first_body = lock.GetBody(0);
    auto* second_body = lock.GetBody(1);
    if (first_body == nullptr || second_body == nullptr) {
        return false;
    }

    settings.mUserData = id.value;
    settings.mConstraintPriority = constraint_priority(id);
    JPH::Ref<JPH::Constraint> constraint{settings.Create(*first_body, *second_body)};
    if (constraint == nullptr) {
        return false;
    }
    impl.physics_system_.AddConstraint(constraint.GetPtr());
    lock.ReleaseLocks();

    auto& body_interface = impl.physics_system_.GetBodyInterface();
    if (first_entry->motion == MotionType::dynamic_body) {
        body_interface.ActivateBody(first_entry->body_id);
    }
    if (second_entry->motion == MotionType::dynamic_body) {
        body_interface.ActivateBody(second_entry->body_id);
    }
    return true;
}

[[nodiscard]] ConstraintType constraint_type(const JPH::EConstraintSubType type) noexcept {
    switch (type) {
    case JPH::EConstraintSubType::Hinge:
        return ConstraintType::hinge;
    case JPH::EConstraintSubType::Slider:
        return ConstraintType::slider;
    case JPH::EConstraintSubType::Distance:
        return ConstraintType::distance;
    case JPH::EConstraintSubType::Fixed:
    default:
        return ConstraintType::fixed;
    }
}

[[nodiscard]] bool supported_constraint(const JPH::EConstraintSubType type) noexcept {
    return type == JPH::EConstraintSubType::Fixed ||
        type == JPH::EConstraintSubType::Hinge ||
        type == JPH::EConstraintSubType::Slider ||
        type == JPH::EConstraintSubType::Distance;
}

[[nodiscard]] bool constraint_references(
    const JPH::Constraint& constraint,
    const simulation::ParticipantId participant
) noexcept {
    if (constraint.GetType() != JPH::EConstraintType::TwoBodyConstraint) {
        return false;
    }
    const auto& two_body = static_cast<const JPH::TwoBodyConstraint&>(constraint);
    return two_body.GetBody1()->GetUserData() == participant.value ||
        two_body.GetBody2()->GetUserData() == participant.value;
}

template <typename ImplType>
void activate_constraint_bodies(ImplType& impl, const JPH::Constraint& constraint) {
    if (constraint.GetType() != JPH::EConstraintType::TwoBodyConstraint) {
        return;
    }
    const auto& two_body = static_cast<const JPH::TwoBodyConstraint&>(constraint);
    auto& body_interface = impl.physics_system_.GetBodyInterface();
    const simulation::ParticipantId participants[]{
        simulation::ParticipantId{two_body.GetBody1()->GetUserData()},
        simulation::ParticipantId{two_body.GetBody2()->GetUserData()},
    };
    for (const auto participant : participants) {
        if (const auto* body = impl.find(participant); body != nullptr &&
            body->motion == MotionType::dynamic_body) {
            body_interface.ActivateBody(body->body_id);
        }
    }
}

void configure_hinge_motor(JPH::HingeConstraint& constraint, const HingeMotorDesc& motor) {
    auto& settings = constraint.GetMotorSettings();
    settings.mSpringSettings = jolt_spring(motor.drive_spring);
    settings.SetTorqueLimit(motor.max_torque);
    constraint.SetTargetAngularVelocity(motor.target_angular_velocity);
    constraint.SetTargetAngle(motor.target_angle);
    constraint.SetMotorState(jolt_motor_mode(motor.mode));
}

void configure_slider_motor(JPH::SliderConstraint& constraint, const SliderMotorDesc& motor) {
    auto& settings = constraint.GetMotorSettings();
    settings.mSpringSettings = jolt_spring(motor.drive_spring);
    settings.SetForceLimit(motor.max_force);
    constraint.SetTargetVelocity(motor.target_velocity);
    constraint.SetTargetPosition(motor.target_position);
    constraint.SetMotorState(jolt_motor_mode(motor.mode));
}

[[nodiscard]] NativeConstraintImpulse constraint_impulse(
    const JPH::Constraint& constraint
) noexcept {
    NativeConstraintImpulse result;
    switch (constraint.GetSubType()) {
    case JPH::EConstraintSubType::Fixed: {
        const auto& fixed = static_cast<const JPH::FixedConstraint&>(constraint);
        result.linear = fixed.GetTotalLambdaPosition().Length();
        result.angular = fixed.GetTotalLambdaRotation().Length();
        break;
    }
    case JPH::EConstraintSubType::Hinge: {
        const auto& hinge = static_cast<const JPH::HingeConstraint&>(constraint);
        const float rotation = hinge.GetTotalLambdaRotation().Length();
        result.linear = hinge.GetTotalLambdaPosition().Length();
        result.limit = std::abs(hinge.GetTotalLambdaRotationLimits());
        result.motor = std::abs(hinge.GetTotalLambdaMotor());
        result.angular = std::sqrt(
            rotation * rotation + result.limit * result.limit + result.motor * result.motor
        );
        break;
    }
    case JPH::EConstraintSubType::Slider: {
        const auto& slider = static_cast<const JPH::SliderConstraint&>(constraint);
        const float position = slider.GetTotalLambdaPosition().Length();
        result.limit = std::abs(slider.GetTotalLambdaPositionLimits());
        result.motor = std::abs(slider.GetTotalLambdaMotor());
        result.linear = std::sqrt(
            position * position + result.limit * result.limit + result.motor * result.motor
        );
        result.angular = slider.GetTotalLambdaRotation().Length();
        break;
    }
    case JPH::EConstraintSubType::Distance: {
        const auto& distance = static_cast<const JPH::DistanceConstraint&>(constraint);
        result.linear = std::abs(distance.GetTotalLambdaPosition());
        break;
    }
    default:
        break;
    }
    return result;
}

} // namespace

bool Domain::create_constraint(const FixedConstraintDesc& desc) {
    if (!valid_constraint_pair(*impl_, desc.id, desc.first, desc.second) ||
        !valid_break_thresholds(desc.break_thresholds)) {
        return false;
    }
    if (!constraint_runtime_) {
        constraint_runtime_ = std::make_shared<ConstraintRuntimeState>();
    }

    JPH::FixedConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mAutoDetectPoint = true;
    settings.mEnabled = desc.enabled;
    if (!add_constraint(*impl_, desc.id, desc.first, desc.second, settings)) {
        return false;
    }
    constraint_runtime_->break_thresholds[desc.id.value] = desc.break_thresholds;
    return true;
}

bool Domain::create_constraint(const HingeConstraintDesc& desc) {
    if (!valid_constraint_pair(*impl_, desc.id, desc.first, desc.second) ||
        !finite(desc.anchor) || !perpendicular_axes(desc.axis, desc.normal) ||
        !std::isfinite(desc.min_angle) || !std::isfinite(desc.max_angle) ||
        desc.min_angle < -pi || desc.min_angle > 0.0F ||
        desc.max_angle < 0.0F || desc.max_angle > pi ||
        desc.min_angle > desc.max_angle ||
        !valid_constraint_spring(desc.limits_spring) ||
        !valid_nonnegative(desc.max_friction_torque) ||
        !valid_hinge_motor(desc.motor) ||
        !valid_break_thresholds(desc.break_thresholds)) {
        return false;
    }
    if (desc.local_frames)
        for (const auto &f : *desc.local_frames)
            if (!finite(f.anchor) || !perpendicular_axes(f.axis, f.normal))
                return false;
    if (!constraint_runtime_) {
        constraint_runtime_ = std::make_shared<ConstraintRuntimeState>();
    }

    JPH::HingeConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mPoint1 = jolt_position(desc.anchor);
    settings.mPoint2 = jolt_position(desc.anchor);
    settings.mHingeAxis1 = normalized_jolt_vector(desc.axis);
    settings.mHingeAxis2 = settings.mHingeAxis1;
    settings.mNormalAxis1 = normalized_jolt_vector(desc.normal);
    settings.mNormalAxis2 = settings.mNormalAxis1;
    if (desc.local_frames) {
        const auto &a = (*desc.local_frames)[0];
        const auto &b = (*desc.local_frames)[1];
        settings.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
        settings.mPoint1 = jolt_position(a.anchor);
        settings.mPoint2 = jolt_position(b.anchor);
        settings.mHingeAxis1 = normalized_jolt_vector(a.axis);
        settings.mHingeAxis2 = normalized_jolt_vector(b.axis);
        settings.mNormalAxis1 = normalized_jolt_vector(a.normal);
        settings.mNormalAxis2 = normalized_jolt_vector(b.normal);
    }
    settings.mLimitsMin = desc.min_angle;
    settings.mLimitsMax = desc.max_angle;
    settings.mLimitsSpringSettings = jolt_spring(desc.limits_spring);
    settings.mMaxFrictionTorque = desc.max_friction_torque;
    settings.mMotorSettings.mSpringSettings = jolt_spring(desc.motor.drive_spring);
    settings.mMotorSettings.SetTorqueLimit(desc.motor.max_torque);
    settings.mEnabled = desc.enabled;
    if (!add_constraint(*impl_, desc.id, desc.first, desc.second, settings)) {
        return false;
    }
    auto* constraint = static_cast<JPH::HingeConstraint*>(find_constraint(*impl_, desc.id));
    if (constraint == nullptr) {
        return false;
    }
    if (!desc.collide_connected) {
        const auto a = impl_->find(desc.first)->body_id, b = impl_->find(desc.second)->body_id;
        ++impl_->excluded_contact_pairs_[Impl::contact_pair(a, b)];
        constraint_runtime_->excluded_pairs.emplace(desc.id.value, std::array{a, b});
        auto &bi = impl_->physics_system_.GetBodyInterface();
        bi.InvalidateContactCache(a);
        bi.InvalidateContactCache(b);
    }
    configure_hinge_motor(*constraint, desc.motor);
    constraint_runtime_->break_thresholds[desc.id.value] = desc.break_thresholds;
    return true;
}

bool Domain::create_constraint(const SliderConstraintDesc& desc) {
    if (!valid_constraint_pair(*impl_, desc.id, desc.first, desc.second) ||
        !finite(desc.anchor) || !finite(desc.axis) || length_squared(desc.axis) <= 0.000001F ||
        (desc.min_position.has_value() &&
            (!std::isfinite(*desc.min_position) || *desc.min_position > 0.0F)) ||
        (desc.max_position.has_value() &&
            (!std::isfinite(*desc.max_position) || *desc.max_position < 0.0F)) ||
        (desc.min_position.has_value() && desc.max_position.has_value() &&
            *desc.min_position > *desc.max_position) ||
        !valid_constraint_spring(desc.limits_spring) ||
        (desc.limits_spring.frequency_hz > 0.0F &&
            !desc.min_position.has_value() && !desc.max_position.has_value()) ||
        !valid_nonnegative(desc.max_friction_force) ||
        !valid_slider_motor(desc.motor) ||
        !valid_break_thresholds(desc.break_thresholds)) {
        return false;
    }
    if (!constraint_runtime_) {
        constraint_runtime_ = std::make_shared<ConstraintRuntimeState>();
    }

    JPH::SliderConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mAutoDetectPoint = false;
    settings.mPoint1 = jolt_position(desc.anchor);
    settings.mPoint2 = jolt_position(desc.anchor);
    settings.SetSliderAxis(normalized_jolt_vector(desc.axis));
    settings.mLimitsMin = desc.min_position.value_or(-FLT_MAX);
    settings.mLimitsMax = desc.max_position.value_or(FLT_MAX);
    settings.mLimitsSpringSettings = jolt_spring(desc.limits_spring);
    settings.mMaxFrictionForce = desc.max_friction_force;
    settings.mMotorSettings.mSpringSettings = jolt_spring(desc.motor.drive_spring);
    settings.mMotorSettings.SetForceLimit(desc.motor.max_force);
    settings.mEnabled = desc.enabled;
    if (!add_constraint(*impl_, desc.id, desc.first, desc.second, settings)) {
        return false;
    }
    auto* constraint = static_cast<JPH::SliderConstraint*>(find_constraint(*impl_, desc.id));
    if (constraint == nullptr) {
        return false;
    }
    configure_slider_motor(*constraint, desc.motor);
    constraint_runtime_->break_thresholds[desc.id.value] = desc.break_thresholds;
    return true;
}

bool Domain::create_constraint(const DistanceConstraintDesc& desc) {
    const bool has_first_anchor = desc.first_anchor.has_value();
    const bool has_second_anchor = desc.second_anchor.has_value();
    const bool has_min_distance = desc.min_distance.has_value();
    const bool has_max_distance = desc.max_distance.has_value();
    if (!valid_constraint_pair(*impl_, desc.id, desc.first, desc.second) ||
        has_first_anchor != has_second_anchor ||
        (has_first_anchor && (!finite(*desc.first_anchor) || !finite(*desc.second_anchor))) ||
        has_min_distance != has_max_distance ||
        (has_min_distance &&
            (!valid_nonnegative(*desc.min_distance) || !valid_nonnegative(*desc.max_distance) ||
                *desc.min_distance > *desc.max_distance)) ||
        !valid_constraint_spring(desc.limits_spring) ||
        !valid_break_thresholds(desc.break_thresholds)) {
        return false;
    }
    if (!constraint_runtime_) {
        constraint_runtime_ = std::make_shared<ConstraintRuntimeState>();
    }

    const auto* first_entry = impl_->find(desc.first);
    const auto* second_entry = impl_->find(desc.second);
    if (first_entry == nullptr || second_entry == nullptr) {
        return false;
    }

    const std::array body_ids{first_entry->body_id, second_entry->body_id};
    JPH::BodyLockMultiRead lock{
        impl_->physics_system_.GetBodyLockInterface(),
        body_ids.data(),
        static_cast<int>(body_ids.size())
    };
    const auto* first_body = lock.GetBody(0);
    const auto* second_body = lock.GetBody(1);
    if (first_body == nullptr || second_body == nullptr) {
        return false;
    }
    const JPH::RVec3 first_anchor = has_first_anchor
        ? jolt_position(*desc.first_anchor)
        : first_body->GetCenterOfMassPosition();
    const JPH::RVec3 second_anchor = has_second_anchor
        ? jolt_position(*desc.second_anchor)
        : second_body->GetCenterOfMassPosition();
    lock.ReleaseLocks();

    JPH::DistanceConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mPoint1 = first_anchor;
    settings.mPoint2 = second_anchor;
    if (has_min_distance) {
        settings.mMinDistance = *desc.min_distance;
        settings.mMaxDistance = *desc.max_distance;
    }
    settings.mLimitsSpringSettings = jolt_spring(desc.limits_spring);
    settings.mEnabled = desc.enabled;
    if (!add_constraint(*impl_, desc.id, desc.first, desc.second, settings)) {
        return false;
    }
    constraint_runtime_->break_thresholds[desc.id.value] = desc.break_thresholds;
    return true;
}

bool Domain::destroy_constraint(const ConstraintId id) {
    auto* constraint = find_constraint(*impl_, id);
    if (constraint == nullptr || !supported_constraint(constraint->GetSubType())) {
        return false;
    }

    simulation::ParticipantId first{};
    simulation::ParticipantId second{};
    if (constraint->GetType() == JPH::EConstraintType::TwoBodyConstraint) {
        const auto& two_body = static_cast<const JPH::TwoBodyConstraint&>(*constraint);
        first = simulation::ParticipantId{two_body.GetBody1()->GetUserData()};
        second = simulation::ParticipantId{two_body.GetBody2()->GetUserData()};
    }

    impl_->physics_system_.RemoveConstraint(constraint);
    if (constraint_runtime_) {
        constraint_runtime_->break_thresholds.erase(id.value);
        if (const auto pair = constraint_runtime_->excluded_pairs.find(id.value);
            pair != constraint_runtime_->excluded_pairs.end()) {
            const auto a = pair->second[0], b = pair->second[1];
            const auto key = Impl::contact_pair(a, b);
            if (auto i = impl_->excluded_contact_pairs_.find(key);
                i != impl_->excluded_contact_pairs_.end() && !--i->second)
                impl_->excluded_contact_pairs_.erase(i);
            auto &bi = impl_->physics_system_.GetBodyInterface();
            bi.InvalidateContactCache(a);
            bi.InvalidateContactCache(b);
            constraint_runtime_->excluded_pairs.erase(pair);
        }
    }

    auto& body_interface = impl_->physics_system_.GetBodyInterface();
    if (const auto* body = impl_->find(first); body != nullptr &&
        body->motion == MotionType::dynamic_body) {
        body_interface.ActivateBody(body->body_id);
    }
    if (const auto* body = impl_->find(second); body != nullptr &&
        body->motion == MotionType::dynamic_body) {
        body_interface.ActivateBody(body->body_id);
    }
    return true;
}

bool Domain::set_constraint_enabled(const ConstraintId id, const bool enabled) {
    auto* constraint = find_constraint(*impl_, id);
    if (constraint == nullptr || !supported_constraint(constraint->GetSubType())) {
        return false;
    }
    constraint->SetEnabled(enabled);
    if (enabled) {
        activate_constraint_bodies(*impl_, *constraint);
    }
    return true;
}

bool Domain::set_hinge_motor(const ConstraintId id, const HingeMotorDesc& motor) {
    auto* constraint = find_constraint(*impl_, id);
    if (constraint == nullptr || constraint->GetSubType() != JPH::EConstraintSubType::Hinge ||
        !valid_hinge_motor(motor)) {
        return false;
    }
    auto& hinge = static_cast<JPH::HingeConstraint&>(*constraint);
    configure_hinge_motor(hinge, motor);
    activate_constraint_bodies(*impl_, hinge);
    return true;
}

bool Domain::set_slider_motor(const ConstraintId id, const SliderMotorDesc& motor) {
    auto* constraint = find_constraint(*impl_, id);
    if (constraint == nullptr || constraint->GetSubType() != JPH::EConstraintSubType::Slider ||
        !valid_slider_motor(motor)) {
        return false;
    }
    auto& slider = static_cast<JPH::SliderConstraint&>(*constraint);
    configure_slider_motor(slider, motor);
    activate_constraint_bodies(*impl_, slider);
    return true;
}

std::optional<ConstraintState> Domain::constraint_state(const ConstraintId id) const noexcept {
    const auto* constraint = find_constraint(*impl_, id);
    if (constraint == nullptr || constraint->GetType() != JPH::EConstraintType::TwoBodyConstraint ||
        !supported_constraint(constraint->GetSubType())) {
        return std::nullopt;
    }

    const auto& two_body = static_cast<const JPH::TwoBodyConstraint&>(*constraint);
    const NativeConstraintImpulse impulse = constraint_impulse(*constraint);
    ConstraintState state{
        .type = constraint_type(constraint->GetSubType()),
        .first = simulation::ParticipantId{two_body.GetBody1()->GetUserData()},
        .second = simulation::ParticipantId{two_body.GetBody2()->GetUserData()},
        .enabled = constraint->GetEnabled(),
        .linear_impulse = impulse.linear,
        .angular_impulse = impulse.angular,
        .limit_impulse = impulse.limit,
        .motor_impulse = impulse.motor,
    };
    if (constraint->GetSubType() == JPH::EConstraintSubType::Hinge) {
        const auto& hinge = static_cast<const JPH::HingeConstraint&>(*constraint);
        state.coordinate = hinge.GetCurrentAngle();
        state.motor_mode = vice_motor_mode(hinge.GetMotorState());
    } else if (constraint->GetSubType() == JPH::EConstraintSubType::Slider) {
        const auto& slider = static_cast<const JPH::SliderConstraint&>(*constraint);
        state.coordinate = slider.GetCurrentPosition();
        state.motor_mode = vice_motor_mode(slider.GetMotorState());
    }
    return state;
}

void Domain::step(
    const simulation::StepContext& context,
    simulation::InteractionEmitter& emitter
) {
    step_unchecked(context, emitter);

    if (!constraint_runtime_) {
        return;
    }
    constraint_runtime_->break_events.clear();

    struct BreakCandidate {
        ConstraintId id{};
        ConstraintBreakEvent event{};
    };
    std::vector<BreakCandidate> candidates;

    const auto constraints = impl_->physics_system_.GetConstraints();
    for (const auto& constraint_ref : constraints) {
        const auto* constraint = constraint_ref.GetPtr();
        if (constraint == nullptr || !constraint->GetEnabled() ||
            !supported_constraint(constraint->GetSubType()) ||
            constraint->GetType() != JPH::EConstraintType::TwoBodyConstraint) {
            continue;
        }
        const ConstraintId id{constraint->GetUserData()};
        const auto threshold = constraint_runtime_->break_thresholds.find(id.value);
        if (threshold == constraint_runtime_->break_thresholds.end()) {
            continue;
        }

        const NativeConstraintImpulse impulse = constraint_impulse(*constraint);
        const bool linear_break = threshold->second.linear_impulse.has_value() &&
            impulse.linear > *threshold->second.linear_impulse;
        const bool angular_break = threshold->second.angular_impulse.has_value() &&
            impulse.angular > *threshold->second.angular_impulse;
        if (!linear_break && !angular_break) {
            continue;
        }

        const auto& two_body = static_cast<const JPH::TwoBodyConstraint&>(*constraint);
        candidates.push_back(BreakCandidate{
            .id = id,
            .event = ConstraintBreakEvent{
                .id = id,
                .type = constraint_type(constraint->GetSubType()),
                .first = simulation::ParticipantId{two_body.GetBody1()->GetUserData()},
                .second = simulation::ParticipantId{two_body.GetBody2()->GetUserData()},
                .linear_impulse = impulse.linear,
                .angular_impulse = impulse.angular,
            },
        });
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const BreakCandidate& left, const BreakCandidate& right) {
            return left.id.value < right.id.value;
        }
    );
    for (const auto& candidate : candidates) {
        if (destroy_constraint(candidate.id)) {
            constraint_runtime_->break_events.push_back(candidate.event);
        }
    }
}

bool Domain::destroy_body(const simulation::ParticipantId participant) {
    if (!participant) {
        return false;
    }
    const auto constraints = impl_->physics_system_.GetConstraints();
    for (const auto& constraint : constraints) {
        if (constraint != nullptr && constraint_references(*constraint, participant)) {
            return false;
        }
    }
    return destroy_body_unchecked(participant);
}

std::span<const ConstraintBreakEvent> Domain::constraint_break_events() const noexcept {
    if (!constraint_runtime_) {
        return {};
    }
    return constraint_runtime_->break_events;
}

std::size_t Domain::constraint_count() const noexcept {
    return impl_->physics_system_.GetConstraints().size();
}

} // namespace vice::rigid_body
