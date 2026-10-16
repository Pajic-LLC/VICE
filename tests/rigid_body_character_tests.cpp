#include <vice/rigid_body/rigid_body.hpp>
#include <vice/simulation/simulation.hpp>

#include <cassert>
#include <cmath>

namespace {

using namespace vice;

void basic_character_motion() {
    auto rigid = rigid_body::Domain::create(simulation::DomainId{77}, "character.basic");
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
}

void slope_classification() {
    constexpr float padding = 0.02F;
    constexpr float floor_half_height = 1.0F;

    for (const auto test : {
            std::pair{30.0F, rigid_body::CharacterGroundState::grounded},
            std::pair{70.0F, rigid_body::CharacterGroundState::steep_ground},
        }) {
        const float angle = test.first * rigid_body::pi / 180.0F;
        auto rigid = rigid_body::Domain::create(
            simulation::DomainId{static_cast<std::uint64_t>(780 + static_cast<int>(test.first))},
            "character.slope"
        );
        assert(rigid != nullptr);
        simulation::Coordinator coordinator{1.0 / 120.0};
        assert(coordinator.register_domain(*rigid));

        rigid_body::BoxBodyDesc slope;
        slope.participant = simulation::ParticipantId{1};
        slope.motion = rigid_body::MotionType::static_body;
        slope.half_extents = math::Vec3{20.0F, floor_half_height, 20.0F};
        slope.orientation = math::Quat{
            0.0F,
            0.0F,
            std::sin(0.5F * angle),
            std::cos(0.5F * angle),
        };
        assert(rigid->create_box(slope));

        rigid_body::CharacterDesc character;
        character.id = rigid_body::CharacterId{1};
        character.height = 1.8F;
        character.radius = 0.3F;
        character.max_slope_angle = 50.0F * rigid_body::pi / 180.0F;
        const float radius_and_padding = character.radius + padding;
        character.position.y =
            (radius_and_padding + floor_half_height) / std::cos(angle) -
            radius_and_padding;
        assert(rigid->create_character(character));

        coordinator.step();
        const auto state = rigid->character_state(character.id);
        assert(state.has_value());
        assert(state->ground_state == test.second);
        assert(state->ground_participant == slope.participant);

        assert(rigid->destroy_character(character.id));
        assert(rigid->destroy_body(slope.participant));
        assert(coordinator.unregister_domain(rigid->id()));
    }
}

void stair_policy() {
    for (const auto test : {
            std::pair{0.30F, true},
            std::pair{0.65F, false},
        }) {
        auto rigid = rigid_body::Domain::create(
            simulation::DomainId{static_cast<std::uint64_t>(900 + static_cast<int>(test.first * 100.0F))},
            "character.stairs"
        );
        assert(rigid != nullptr);
        simulation::Coordinator coordinator{1.0 / 120.0};
        assert(coordinator.register_domain(*rigid));

        rigid_body::BoxBodyDesc floor;
        floor.participant = simulation::ParticipantId{1};
        floor.motion = rigid_body::MotionType::static_body;
        floor.half_extents = math::Vec3{10.0F, 0.5F, 10.0F};
        floor.position = math::Vec3{0.0F, -0.5F, 0.0F};
        assert(rigid->create_box(floor));

        rigid_body::BoxBodyDesc step_box;
        step_box.participant = simulation::ParticipantId{2};
        step_box.motion = rigid_body::MotionType::static_body;
        step_box.half_extents = math::Vec3{0.6F, 0.5F * test.first, 2.0F};
        step_box.position = math::Vec3{1.2F, 0.5F * test.first, 0.0F};
        assert(rigid->create_box(step_box));

        rigid_body::CharacterDesc character;
        character.id = rigid_body::CharacterId{1};
        character.position = math::Vec3{0.0F, 0.0F, 0.0F};
        character.step_height = 0.4F;
        character.stick_to_floor_distance = 0.5F;
        assert(rigid->create_character(character));

        for (int frame = 0; frame < 4; ++frame) {
            coordinator.step();
        }
        assert(rigid->set_character_horizontal_velocity(
            character.id,
            math::Vec3{2.0F, 0.0F, 0.0F}
        ));

        for (int frame = 0; frame < 90; ++frame) {
            coordinator.step();
        }
        const auto state = rigid->character_state(character.id);
        assert(state.has_value());
        if (test.second) {
            assert(state->position.x > 1.0F);
            assert(state->position.y > 0.15F);
        } else {
            assert(state->position.x < 0.55F);
            assert(state->position.y < 0.10F);
        }

        assert(rigid->destroy_character(character.id));
        assert(rigid->destroy_body(step_box.participant));
        assert(rigid->destroy_body(floor.participant));
        assert(coordinator.unregister_domain(rigid->id()));
    }
}

} // namespace

int main() {
    basic_character_motion();
    slope_classification();
    stair_policy();
    return 0;
}
