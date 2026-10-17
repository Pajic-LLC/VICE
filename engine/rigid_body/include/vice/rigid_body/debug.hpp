#pragma once

#include <vice/rigid_body/rigid_body.hpp>

#include <optional>

namespace vice::rigid_body {

struct BodyDebugSnapshot {
    simulation::ParticipantId participant{};
    simulation::MaterialId material{};
    ShapeId shape{};
    MotionType motion = MotionType::dynamic_body;
    MotionQuality motion_quality = MotionQuality::discrete;
    math::Vec3 position{};
    math::Quat orientation{};
    math::Vec3 linear_velocity{};
    math::Vec3 angular_velocity{};
    std::optional<math::Vec3> box_half_extents;
    bool trigger = false;
    bool active = false;
};

struct CharacterDebugSnapshot {
    CharacterId id{};
    float height = 0.0F;
    float radius = 0.0F;
    math::Vec3 position{};
    math::Vec3 velocity{};
    math::Vec3 horizontal_velocity{};
    CharacterGroundState ground_state = CharacterGroundState::airborne;
    math::Vec3 ground_normal{};
    math::Vec3 ground_velocity{};
    std::optional<simulation::ParticipantId> ground_participant;
    std::optional<simulation::MaterialId> ground_material;
};

struct ConstraintDebugSnapshot {
    ConstraintId id{};
    ConstraintType type = ConstraintType::fixed;
    simulation::ParticipantId first{};
    simulation::ParticipantId second{};
    bool enabled = false;
    std::optional<float> coordinate;
    ConstraintMotorMode motor_mode = ConstraintMotorMode::off;
    float linear_impulse = 0.0F;
    float angular_impulse = 0.0F;
    float limit_impulse = 0.0F;
    float motor_impulse = 0.0F;
};

[[nodiscard]] std::optional<BodyDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const BodyDesc& desc
) noexcept;

[[nodiscard]] std::optional<BodyDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const BoxBodyDesc& desc
) noexcept;

[[nodiscard]] std::optional<CharacterDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const CharacterDesc& desc
) noexcept;

[[nodiscard]] std::optional<ConstraintDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const FixedConstraintDesc& desc
) noexcept;

[[nodiscard]] std::optional<ConstraintDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const HingeConstraintDesc& desc
) noexcept;

[[nodiscard]] std::optional<ConstraintDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const SliderConstraintDesc& desc
) noexcept;

[[nodiscard]] std::optional<ConstraintDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const DistanceConstraintDesc& desc
) noexcept;

} // namespace vice::rigid_body
