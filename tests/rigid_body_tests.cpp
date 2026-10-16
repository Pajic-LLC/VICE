#include <vice/rigid_body/rigid_body.hpp>
#include <vice/simulation/entity_bindings.hpp>
#include <vice/world/world.hpp>

#include <cassert>
#include <cmath>

int main() {
    using namespace vice;

    auto rigid = rigid_body::Domain::create(
        simulation::DomainId{10},
        "rigid"
    );
    assert(rigid != nullptr);

    simulation::Coordinator coordinator{1.0 / 120.0};
    assert(coordinator.register_domain(*rigid));

    const simulation::ParticipantId floor_participant{1};
    rigid_body::BoxBodyDesc floor;
    floor.participant = floor_participant;
    floor.motion = rigid_body::MotionType::static_body;
    floor.half_extents = math::Vec3{8.0F, 1.0F, 8.0F};
    floor.position = math::Vec3{0.0F, -1.0F, 0.0F};
    assert(rigid->create_box(floor));

    const simulation::ParticipantId box_participant{2};
    rigid_body::BoxBodyDesc box;
    box.participant = box_participant;
    box.motion = rigid_body::MotionType::dynamic_body;
    box.half_extents = math::Vec3{0.5F, 0.5F, 0.5F};
    box.position = math::Vec3{0.0F, 4.0F, 0.0F};
    box.orientation = math::Quat{0.0F, 0.0F, 0.2588190451F, 0.9659258263F};
    box.friction = 0.6F;
    assert(rigid->create_box(box));
    assert(!rigid->create_box(box));
    assert(rigid->body_count() == 2U);

    rigid_body::BoxBodyDesc invalid_orientation;
    invalid_orientation.participant = simulation::ParticipantId{3};
    invalid_orientation.orientation = math::Quat{0.0F, 0.0F, 0.0F, 0.0F};
    assert(!rigid->create_box(invalid_orientation));

    world::World world;
    const auto entity = world.create_entity();
    simulation::EntityBindings bindings;
    assert(bindings.bind(
        world,
        coordinator,
        entity,
        rigid->id(),
        box_participant
    ));

    const auto initial = rigid->body_state(box_participant);
    assert(initial.has_value());
    assert(initial->position.y > 3.9F);
    const float initial_orientation_length = std::sqrt(
        initial->orientation.x * initial->orientation.x +
        initial->orientation.y * initial->orientation.y +
        initial->orientation.z * initial->orientation.z +
        initial->orientation.w * initial->orientation.w
    );
    assert(std::abs(initial_orientation_length - 1.0F) < 0.001F);

    for (int step = 0; step < 600; ++step) {
        coordinator.step();
    }

    const auto settled = rigid->body_state(box_participant);
    assert(settled.has_value());
    assert(settled->position.y > 0.45F);
    assert(settled->position.y < 0.80F);
    assert(std::abs(settled->linear_velocity.y) < 0.1F);
    assert(std::isfinite(settled->orientation.x));
    assert(std::isfinite(settled->orientation.y));
    assert(std::isfinite(settled->orientation.z));
    assert(std::isfinite(settled->orientation.w));

    assert(bindings.find(world, coordinator, entity, rigid->id()) == box_participant);
    assert(rigid->destroy_body(box_participant));
    assert(!rigid->destroy_body(box_participant));
    assert(!rigid->body_state(box_participant).has_value());
    assert(world.alive(entity));
    assert(bindings.find(world, coordinator, entity, rigid->id()) == box_participant);

    assert(bindings.unbind(entity, rigid->id()));
    assert(rigid->destroy_body(floor_participant));
    assert(rigid->body_count() == 0U);

    return 0;
}
