#include <Jolt/Physics/Body/MotionQuality.h>

namespace vice::rigid_body {
namespace {

[[nodiscard]] std::optional<JPH::EMotionQuality> jolt_motion_quality(
    const MotionQuality motion_quality
) noexcept {
    switch (motion_quality) {
    case MotionQuality::discrete:
        return JPH::EMotionQuality::Discrete;
    case MotionQuality::continuous:
        return JPH::EMotionQuality::LinearCast;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] MotionQuality vice_motion_quality(
    const JPH::EMotionQuality motion_quality
) noexcept {
    switch (motion_quality) {
    case JPH::EMotionQuality::LinearCast:
        return MotionQuality::continuous;
    case JPH::EMotionQuality::Discrete:
    default:
        return MotionQuality::discrete;
    }
}

} // namespace

bool Domain::create_body(const BodyDesc& desc) {
    if (!jolt_motion_quality(desc.motion_quality).has_value() ||
        (desc.motion == MotionType::static_body &&
            desc.motion_quality != MotionQuality::discrete)) {
        return false;
    }

    if (!create_body_unchecked(desc)) {
        return false;
    }

    if (desc.motion_quality == MotionQuality::continuous &&
        !set_body_motion_quality(desc.participant, desc.motion_quality)) {
        (void)destroy_body_unchecked(desc.participant);
        return false;
    }
    return true;
}

bool Domain::create_box(const BoxBodyDesc& desc) {
    if (!jolt_motion_quality(desc.motion_quality).has_value() ||
        (desc.motion == MotionType::static_body &&
            desc.motion_quality != MotionQuality::discrete)) {
        return false;
    }

    if (!create_box_unchecked(desc)) {
        return false;
    }

    if (desc.motion_quality == MotionQuality::continuous &&
        !set_body_motion_quality(desc.participant, desc.motion_quality)) {
        (void)destroy_body_unchecked(desc.participant);
        return false;
    }
    return true;
}

bool Domain::set_body_motion_quality(
    const simulation::ParticipantId participant,
    const MotionQuality motion_quality
) {
    const auto native_quality = jolt_motion_quality(motion_quality);
    auto* body = impl_->find(participant);
    if (!native_quality.has_value() || body == nullptr) {
        return false;
    }

    if (body->motion == MotionType::static_body) {
        return motion_quality == MotionQuality::discrete;
    }

    auto& body_interface = impl_->physics_system_.GetBodyInterface();
    body_interface.SetMotionQuality(body->body_id, *native_quality);
    return body_interface.GetMotionQuality(body->body_id) == *native_quality;
}

std::optional<BodyState> Domain::body_state(
    const simulation::ParticipantId participant
) const noexcept {
    auto state = body_state_unchecked(participant);
    const auto* body = impl_->find(participant);
    if (!state.has_value() || body == nullptr) {
        return std::nullopt;
    }

    const auto& body_interface = impl_->physics_system_.GetBodyInterface();
    state->motion_quality = vice_motion_quality(
        body_interface.GetMotionQuality(body->body_id)
    );
    return state;
}

} // namespace vice::rigid_body
