#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace vice::rigid_body {

struct Domain::CharacterRuntimeState {
    struct Entry {
        CharacterId id{};
        CharacterDesc desc{};
        math::Vec3 horizontal_velocity{};
        std::optional<float> pending_jump_speed;
        JPH::Ref<JPH::CharacterVirtual> character;
    };

    std::vector<Entry> characters;
};

namespace {

[[nodiscard]] bool valid_character_desc(const CharacterDesc& desc) noexcept {
    constexpr auto native_invalid_id = std::numeric_limits<std::uint32_t>::max();
    if(desc.collision.excluded_participants.size()>4096 || std::any_of(desc.collision.excluded_participants.begin(),desc.collision.excluded_participants.end(),[](auto p){return !p;})) return false;
    return desc.id && desc.id.value != native_invalid_id &&
        finite(desc.position) &&
        valid_positive(desc.height) && valid_positive(desc.radius) &&
        desc.height > 2.0F * desc.radius &&
        valid_positive(desc.mass) && valid_nonnegative(desc.max_strength) &&
        std::isfinite(desc.max_slope_angle) && desc.max_slope_angle >= 0.0F &&
        desc.max_slope_angle <= 0.5F * pi &&
        valid_nonnegative(desc.step_height) &&
        valid_nonnegative(desc.stick_to_floor_distance) &&
        (desc.collision.include_static || desc.collision.include_dynamic);
}

[[nodiscard]] CharacterGroundState character_ground_state(
    const JPH::CharacterBase::EGroundState state
) noexcept {
    switch (state) {
    case JPH::CharacterBase::EGroundState::OnGround:
        return CharacterGroundState::grounded;
    case JPH::CharacterBase::EGroundState::OnSteepGround:
        return CharacterGroundState::steep_ground;
    case JPH::CharacterBase::EGroundState::NotSupported:
        return CharacterGroundState::unsupported;
    case JPH::CharacterBase::EGroundState::InAir:
    default:
        return CharacterGroundState::airborne;
    }
}

template <typename Runtime>
[[nodiscard]] auto* find_character(Runtime& runtime, const CharacterId id) noexcept {
    const auto found = std::find_if(
        runtime.characters.begin(),
        runtime.characters.end(),
        [id](const auto& entry) {
            return entry.id == id;
        }
    );
    return found != runtime.characters.end() ? &*found : nullptr;
}

template <typename ImplType>
class CharacterBodyFilter final : public JPH::BodyFilter {
public:
    CharacterBodyFilter(
        const ImplType& impl,
        const CharacterCollisionFilter& filter
    ) noexcept : impl_{impl}, filter_{filter} {}

    bool ShouldCollide(const JPH::BodyID& body_id) const override {
        const auto* body = impl_.find(body_id);
        if (body == nullptr) {
            return false;
        }
        if(std::find(filter_.excluded_participants.begin(),filter_.excluded_participants.end(),body->participant)!=filter_.excluded_participants.end()) return false;
        if (body->trigger && !filter_.include_triggers) {
            return false;
        }
        if (body->motion == MotionType::static_body && !filter_.include_static) {
            return false;
        }
        if (body->motion == MotionType::dynamic_body && !filter_.include_dynamic) {
            return false;
        }
        return true;
    }

private:
    const ImplType& impl_;
    const CharacterCollisionFilter &filter_;
};

[[nodiscard]] JPH::CharacterVirtual::ExtendedUpdateSettings character_update_settings(
    const CharacterDesc& desc
) noexcept {
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = JPH::Vec3{0.0F, desc.step_height, 0.0F};
    settings.mStickToFloorStepDown = JPH::Vec3{
        0.0F,
        -desc.stick_to_floor_distance,
        0.0F,
    };
    return settings;
}

} // namespace

bool Domain::create_character(const CharacterDesc& desc) {
    if (!valid_character_desc(desc)) {
        return false;
    }
    if (!character_runtime_) {
        character_runtime_ = std::make_shared<CharacterRuntimeState>();
    }
    if (find_character(*character_runtime_, desc.id) != nullptr) {
        return false;
    }

    const float cylinder_height = desc.height - 2.0F * desc.radius;
    JPH::CapsuleShapeSettings capsule_settings{0.5F * cylinder_height, desc.radius};
    const auto capsule_result = capsule_settings.Create();
    if (capsule_result.HasError()) {
        return false;
    }

    JPH::RotatedTranslatedShapeSettings standing_settings{
        JPH::Vec3{0.0F, 0.5F * cylinder_height + desc.radius, 0.0F},
        JPH::Quat::sIdentity(),
        capsule_result.Get()
    };
    const auto standing_result = standing_settings.Create();
    if (standing_result.HasError()) {
        return false;
    }

    JPH::CharacterVirtualSettings settings;
    settings.mID = JPH::CharacterID{desc.id.value};
    settings.mMass = desc.mass;
    settings.mMaxStrength = desc.max_strength;
    settings.mMaxSlopeAngle = desc.max_slope_angle;
    settings.mSupportingVolume = JPH::Plane{JPH::Vec3::sAxisY(), -cylinder_height};
    settings.mShape = standing_result.Get();

    JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual{
        &settings,
        jolt_position(desc.position),
        JPH::Quat::sIdentity(),
        static_cast<JPH::uint64>(desc.id.value),
        &impl_->physics_system_
    };
    if (character == nullptr) {
        return false;
    }

    character_runtime_->characters.push_back(CharacterRuntimeState::Entry{
        .id = desc.id,
        .desc = desc,
        .character = std::move(character),
    });
    std::sort(
        character_runtime_->characters.begin(),
        character_runtime_->characters.end(),
        [](const auto& left, const auto& right) {
            return left.id.value < right.id.value;
        }
    );
    return true;
}

bool Domain::destroy_character(const CharacterId id) {
    if (!id || !character_runtime_) {
        return false;
    }
    const auto before = character_runtime_->characters.size();
    std::erase_if(
        character_runtime_->characters,
        [id](const auto& entry) {
            return entry.id == id;
        }
    );
    return character_runtime_->characters.size() != before;
}

bool Domain::set_character_velocity(const CharacterId id, const math::Vec3 velocity) {
    if (!character_runtime_ || !finite(velocity)) {
        return false;
    }
    auto* entry = find_character(*character_runtime_, id);
    if (entry == nullptr) {
        return false;
    }
    entry->character->SetLinearVelocity(jolt_vector(velocity));
    return true;
}

bool Domain::set_character_horizontal_velocity(
    const CharacterId id,
    const math::Vec3 velocity
) {
    if (!character_runtime_ || !finite(velocity) || std::abs(velocity.y) > 0.00001F) {
        return false;
    }
    auto* entry = find_character(*character_runtime_, id);
    if (entry == nullptr) {
        return false;
    }
    entry->horizontal_velocity = velocity;
    return true;
}

bool Domain::request_character_jump(const CharacterId id, const float speed) {
    if (!character_runtime_ || !valid_positive(speed)) {
        return false;
    }
    auto* entry = find_character(*character_runtime_, id);
    if (entry == nullptr) {
        return false;
    }
    entry->pending_jump_speed = speed;
    return true;
}

bool Domain::set_character_position(
    const CharacterId id,
    const math::Vec3 position
) {
    if (!character_runtime_ || !finite(position)) {
        return false;
    }
    auto* entry = find_character(*character_runtime_, id);
    if (entry == nullptr) {
        return false;
    }
    entry->character->SetPosition(jolt_position(position));
    CharacterBodyFilter filter{*impl_, entry->desc.collision};
    entry->character->RefreshContacts(
        impl_->physics_system_.GetDefaultBroadPhaseLayerFilter(object_layers::dynamic_body),
        impl_->physics_system_.GetDefaultLayerFilter(object_layers::dynamic_body),
        filter,
        {},
        impl_->temp_allocator_
    );
    return true;
}

std::optional<CharacterState> Domain::character_state(const CharacterId id) const noexcept {
    if (!id || !character_runtime_) {
        return std::nullopt;
    }
    const auto* entry = find_character(*character_runtime_, id);
    if (entry == nullptr) {
        return std::nullopt;
    }

    CharacterState state{
        .position = vec3(entry->character->GetPosition()),
        .velocity = vec3(entry->character->GetLinearVelocity()),
        .horizontal_velocity = entry->horizontal_velocity,
        .pending_jump_speed = entry->pending_jump_speed,
        .ground_state = character_ground_state(entry->character->GetGroundState()),
        .ground_normal = vec3(entry->character->GetGroundNormal()),
        .ground_velocity = vec3(entry->character->GetGroundVelocity()),
    };

    const simulation::ParticipantId ground_participant{
        entry->character->GetGroundUserData()
    };
    if (ground_participant) {
        if (const auto* ground = impl_->find(ground_participant); ground != nullptr) {
            state.ground_participant = ground_participant;
            if (ground->material) {
                state.ground_material = ground->material;
            }
        }
    }
    return state;
}

std::size_t Domain::character_count() const noexcept {
    return character_runtime_ ? character_runtime_->characters.size() : 0U;
}

void Domain::step(
    const simulation::StepContext& context,
    simulation::InteractionEmitter& emitter
) {
    step_constraints_unchecked(context, emitter);
    if (!character_runtime_ || character_runtime_->characters.empty()) {
        return;
    }

    const float delta_time = static_cast<float>(context.fixed_delta_seconds);
    const JPH::Vec3 gravity = impl_->physics_system_.GetGravity();

    for (auto& entry : character_runtime_->characters) {
        auto& character = *entry.character;
        const JPH::Vec3 current_velocity = character.GetLinearVelocity();
        const JPH::Vec3 ground_velocity = character.GetGroundVelocity();
        const bool grounded = character.GetGroundState() ==
            JPH::CharacterBase::EGroundState::OnGround;

        JPH::Vec3 velocity;
        if (grounded && current_velocity.GetY() - ground_velocity.GetY() < 0.1F) {
            velocity = ground_velocity;
            if (entry.pending_jump_speed.has_value()) {
                velocity += JPH::Vec3{0.0F, *entry.pending_jump_speed, 0.0F};
            }
        } else {
            velocity = JPH::Vec3{0.0F, current_velocity.GetY(), 0.0F};
        }
        entry.pending_jump_speed.reset();

        velocity += jolt_vector(entry.horizontal_velocity);
        velocity += gravity * delta_time;
        character.SetLinearVelocity(velocity);

        CharacterBodyFilter filter{*impl_, entry.desc.collision};
        const auto update_settings = character_update_settings(entry.desc);
        character.ExtendedUpdate(
            delta_time,
            gravity,
            update_settings,
            impl_->physics_system_.GetDefaultBroadPhaseLayerFilter(object_layers::dynamic_body),
            impl_->physics_system_.GetDefaultLayerFilter(object_layers::dynamic_body),
            filter,
            {},
            impl_->temp_allocator_
        );
    }
}

} // namespace vice::rigid_body
