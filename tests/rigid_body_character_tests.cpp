#include <vice/rigid_body/rigid_body.hpp>
#include <vice/simulation/simulation.hpp>

#include <cassert>
#include <cmath>

int main() {
    using namespace vice;

    auto rigid = rigid_body::Domain::create(simulation::DomainId{77}, "character");
    assert(rigid != nullptr);

    simulation::Coordinator coordinator{1.0 / 120.0};
    assert(coordinator.register_domain(*rigid));

    rigid_body::BoxBodyDesc floor;
    floor.participant = simulation::ParticipantId{1};
    floor.motion = rigid_body::MotionType::static_body;
    floor.half_extents = math::Vec3{20.0F, 0.5F, 20.0F};
    floor.position = math::Vec3{0.0F, -0.5F, 0.0F};
    assert(rigid->create_box(floor));

    rigid_body::CharacterDesc character;
    character.id = rigid_body::CharacterId{1};
    character.position = math::Vec3{0.0F, 2.0F, 0.0F};
    character.height = 1.8F;
    character.radius = 0.3F;
    character.max_slope_angle = 50.0F * rigid_body::pi / 180.0F;
    character.step_height = 0.4F;
    character.stick_to_floor_distance = 0.5F;
    assert(rigid->create_character(character));
    assert(!rigid->create_character(character));
    assert(rigid->character_count() == 1U);

    rigid_body::CharacterDesc invalid = character;
    invalid.id = rigid_body::CharacterId{2};
    invalid.height = 0.5F;
    invalid.radius = 0.3F;
    assert(!rigid->create_character(invalid));

    auto state = rigid->character_state(character.id);
    assert(state.has_value());
    assert(state->ground_state == rigid_body::CharacterGroundState::airborne);

    for (int step = 0; step < 240; ++step) {
        coordinator.step();
    }

    state = rigid->character_state(character.id);
    assert(state.has_value());
    assert(state->ground_state == rigid_body::CharacterGroundState::grounded);
    assert(std::abs(state->position.y) < 0.03F);
    assert(state->ground_participant == floor.participant);

    assert(!rigid->set_character_horizontal_velocity(
        character.id,
        math::Vec3{1.0F, 1.0F, 0.0F}
    ));
    assert(rigid->set_character_horizontal_velocity(
        character.id,
        math::Vec3{2.0F, 0.0F, 0.0F}
    ));

    const float walk_start = state->position.x;
    for (int step = 0; step < 60; ++step) {
        coordinator.step();
    }
    state = rigid->character_state(character.id);
    assert(state.has_value());
    assert(state->position.x > walk_start + 0.8F);
    assert(state->ground_state == rigid_body::CharacterGroundState::grounded);

    assert(rigid->set_character_horizontal_velocity(character.id, math::Vec3{}));
    assert(rigid->request_character_jump(character.id, 5.0F));
    coordinator.step();
    state = rigid->character_state(character.id);
    assert(state.has_value());
    assert(state->position.y > 0.0F);
    assert(state->velocity.y > 4.0F);
    assert(state->ground_state != rigid_body::CharacterGroundState::grounded);

    for (int step = 0; step < 240; ++step) {
        coordinator.step();
    }
    state = rigid->character_state(character.id);
    assert(state.has_value());
    assert(state->ground_state == rigid_body::CharacterGroundState::grounded);

    rigid_body::BoxBodyDesc trigger;
    trigger.participant = simulation::ParticipantId{2};
    trigger.motion = rigid_body::MotionType::static_body;
    trigger.trigger = true;
    trigger.half_extents = math::Vec3{0.1F, 2.0F, 2.0F};
    trigger.position = math::Vec3{state->position.x + 1.0F, 1.0F, 0.0F};
    assert(rigid->create_box(trigger));

    const float trigger_x = trigger.position.x;
    assert(rigid->set_character_horizontal_velocity(
        character.id,
        math::Vec3{2.0F, 0.0F, 0.0F}
    ));
    for (int step = 0; step < 120; ++step) {
        coordinator.step();
    }
    state = rigid->character_state(character.id);
    assert(state.has_value());
    assert(state->position.x > trigger_x + 0.5F);

    const auto teleported = math::Vec3{-2.0F, 0.0F, 0.0F};
    assert(rigid->set_character_position(character.id, teleported));
    state = rigid->character_state(character.id);
    assert(state.has_value());
    assert(std::abs(state->position.x - teleported.x) < 0.001F);

    assert(rigid->destroy_character(character.id));
    assert(!rigid->destroy_character(character.id));
    assert(rigid->character_count() == 0U);
    assert(!rigid->character_state(character.id).has_value());

    assert(rigid->destroy_body(trigger.participant));
    assert(rigid->destroy_body(floor.participant));
    assert(coordinator.unregister_domain(rigid->id()));
    return 0;
}
