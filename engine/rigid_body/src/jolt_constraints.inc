#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>

#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>

namespace vice::rigid_body {
namespace {

[[nodiscard]] bool valid_constraint_spring(const ConstraintSpring spring) noexcept {
    return std::isfinite(spring.frequency_hz) && spring.frequency_hz >= 0.0F &&
        std::isfinite(spring.damping_ratio) && spring.damping_ratio >= 0.0F;
}

[[nodiscard]] JPH::SpringSettings jolt_spring(const ConstraintSpring spring) noexcept {
    return JPH::SpringSettings{
        JPH::ESpringMode::FrequencyAndDamping,
        spring.frequency_hz,
        spring.damping_ratio,
    };
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
            return constraint.Get();
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
            return constraint.Get();
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
    impl.physics_system_.AddConstraint(constraint.Get());
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

} // namespace

bool Domain::create_constraint(const FixedConstraintDesc& desc) {
    if (!valid_constraint_pair(*impl_, desc.id, desc.first, desc.second)) {
        return false;
    }

    JPH::FixedConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mAutoDetectPoint = true;
    settings.mEnabled = desc.enabled;
    return add_constraint(*impl_, desc.id, desc.first, desc.second, settings);
}

bool Domain::create_constraint(const HingeConstraintDesc& desc) {
    if (!valid_constraint_pair(*impl_, desc.id, desc.first, desc.second) ||
        !finite(desc.anchor) || !perpendicular_axes(desc.axis, desc.normal) ||
        !std::isfinite(desc.min_angle) || !std::isfinite(desc.max_angle) ||
        desc.min_angle < -pi || desc.min_angle > 0.0F ||
        desc.max_angle < 0.0F || desc.max_angle > pi ||
        desc.min_angle > desc.max_angle ||
        !valid_constraint_spring(desc.limits_spring) ||
        !valid_nonnegative(desc.max_friction_torque)) {
        return false;
    }

    JPH::HingeConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mPoint1 = jolt_position(desc.anchor);
    settings.mPoint2 = jolt_position(desc.anchor);
    settings.mHingeAxis1 = normalized_jolt_vector(desc.axis);
    settings.mHingeAxis2 = settings.mHingeAxis1;
    settings.mNormalAxis1 = normalized_jolt_vector(desc.normal);
    settings.mNormalAxis2 = settings.mNormalAxis1;
    settings.mLimitsMin = desc.min_angle;
    settings.mLimitsMax = desc.max_angle;
    settings.mLimitsSpringSettings = jolt_spring(desc.limits_spring);
    settings.mMaxFrictionTorque = desc.max_friction_torque;
    settings.mEnabled = desc.enabled;
    return add_constraint(*impl_, desc.id, desc.first, desc.second, settings);
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
        !valid_nonnegative(desc.max_friction_force)) {
        return false;
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
    settings.mEnabled = desc.enabled;
    return add_constraint(*impl_, desc.id, desc.first, desc.second, settings);
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
        !valid_constraint_spring(desc.limits_spring)) {
        return false;
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
    return add_constraint(*impl_, desc.id, desc.first, desc.second, settings);
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

    const auto keep_alive = impl_->physics_system_.GetConstraints();
    impl_->physics_system_.RemoveConstraint(constraint);

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

    if (enabled && constraint->GetType() == JPH::EConstraintType::TwoBodyConstraint) {
        const auto& two_body = static_cast<const JPH::TwoBodyConstraint&>(*constraint);
        auto& body_interface = impl_->physics_system_.GetBodyInterface();
        const simulation::ParticipantId participants[]{
            simulation::ParticipantId{two_body.GetBody1()->GetUserData()},
            simulation::ParticipantId{two_body.GetBody2()->GetUserData()},
        };
        for (const auto participant : participants) {
            if (const auto* body = impl_->find(participant); body != nullptr &&
                body->motion == MotionType::dynamic_body) {
                body_interface.ActivateBody(body->body_id);
            }
        }
    }
    return true;
}

std::optional<ConstraintState> Domain::constraint_state(const ConstraintId id) const noexcept {
    const auto* constraint = find_constraint(*impl_, id);
    if (constraint == nullptr || constraint->GetType() != JPH::EConstraintType::TwoBodyConstraint ||
        !supported_constraint(constraint->GetSubType())) {
        return std::nullopt;
    }

    const auto& two_body = static_cast<const JPH::TwoBodyConstraint&>(*constraint);
    ConstraintState state{
        .type = constraint_type(constraint->GetSubType()),
        .first = simulation::ParticipantId{two_body.GetBody1()->GetUserData()},
        .second = simulation::ParticipantId{two_body.GetBody2()->GetUserData()},
        .enabled = constraint->GetEnabled(),
    };
    if (constraint->GetSubType() == JPH::EConstraintSubType::Hinge) {
        state.coordinate = static_cast<const JPH::HingeConstraint*>(constraint)->GetCurrentAngle();
    } else if (constraint->GetSubType() == JPH::EConstraintSubType::Slider) {
        state.coordinate = static_cast<const JPH::SliderConstraint*>(constraint)->GetCurrentPosition();
    }
    return state;
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

std::size_t Domain::constraint_count() const noexcept {
    return impl_->physics_system_.GetConstraints().size();
}

} // namespace vice::rigid_body
