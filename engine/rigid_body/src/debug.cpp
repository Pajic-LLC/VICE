#include <vice/rigid_body/debug.hpp>

namespace vice::rigid_body {
namespace {

[[nodiscard]] BodyDebugSnapshot body_snapshot(
    const BodyState& state,
    const simulation::ParticipantId participant,
    const simulation::MaterialId material,
    const ShapeId shape,
    const MotionType motion,
    const bool trigger
) noexcept {
    return BodyDebugSnapshot{
        .participant = participant,
        .material = material,
        .shape = shape,
        .motion = motion,
        .motion_quality = state.motion_quality,
        .position = state.position,
        .orientation = state.orientation,
        .linear_velocity = state.linear_velocity,
        .angular_velocity = state.angular_velocity,
        .trigger = trigger,
        .active = state.active,
    };
}

[[nodiscard]] ConstraintDebugSnapshot constraint_snapshot(
    const ConstraintId id,
    const ConstraintState& state
) noexcept {
    return ConstraintDebugSnapshot{
        .id = id,
        .type = state.type,
        .first = state.first,
        .second = state.second,
        .enabled = state.enabled,
        .coordinate = state.coordinate,
        .motor_mode = state.motor_mode,
        .linear_impulse = state.linear_impulse,
        .angular_impulse = state.angular_impulse,
        .limit_impulse = state.limit_impulse,
        .motor_impulse = state.motor_impulse,
    };
}

} // namespace

std::optional<BodyDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const BodyDesc& desc
) noexcept {
    const auto state = domain.body_state(desc.participant);
    if (!state.has_value()) return std::nullopt;
    return body_snapshot(
        *state,
        desc.participant,
        desc.material,
        desc.shape,
        desc.motion,
        desc.trigger
    );
}

std::optional<BodyDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const BoxBodyDesc& desc
) noexcept {
    const auto state = domain.body_state(desc.participant);
    if (!state.has_value()) return std::nullopt;
    auto snapshot = body_snapshot(
        *state,
        desc.participant,
        desc.material,
        ShapeId{},
        desc.motion,
        desc.trigger
    );
    snapshot.box_half_extents = desc.half_extents;
    return snapshot;
}

std::optional<CharacterDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const CharacterDesc& desc
) noexcept {
    const auto state = domain.character_state(desc.id);
    if (!state.has_value()) return std::nullopt;
    return CharacterDebugSnapshot{
        .id = desc.id,
        .height = desc.height,
        .radius = desc.radius,
        .position = state->position,
        .velocity = state->velocity,
        .horizontal_velocity = state->horizontal_velocity,
        .ground_state = state->ground_state,
        .ground_normal = state->ground_normal,
        .ground_velocity = state->ground_velocity,
        .ground_participant = state->ground_participant,
        .ground_material = state->ground_material,
    };
}

std::optional<ConstraintDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const FixedConstraintDesc& desc
) noexcept {
    const auto state = domain.constraint_state(desc.id);
    return state.has_value()
        ? std::optional<ConstraintDebugSnapshot>{constraint_snapshot(desc.id, *state)}
        : std::nullopt;
}

std::optional<ConstraintDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const HingeConstraintDesc& desc
) noexcept {
    const auto state = domain.constraint_state(desc.id);
    return state.has_value()
        ? std::optional<ConstraintDebugSnapshot>{constraint_snapshot(desc.id, *state)}
        : std::nullopt;
}

std::optional<ConstraintDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const SliderConstraintDesc& desc
) noexcept {
    const auto state = domain.constraint_state(desc.id);
    return state.has_value()
        ? std::optional<ConstraintDebugSnapshot>{constraint_snapshot(desc.id, *state)}
        : std::nullopt;
}

std::optional<ConstraintDebugSnapshot> debug_snapshot(
    const Domain& domain,
    const DistanceConstraintDesc& desc
) noexcept {
    const auto state = domain.constraint_state(desc.id);
    return state.has_value()
        ? std::optional<ConstraintDebugSnapshot>{constraint_snapshot(desc.id, *state)}
        : std::nullopt;
}

} // namespace vice::rigid_body
