#include <vice/rigid_body/debug.hpp>
#include <vice/rigid_body/rigid_body.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "rigid debug test failed: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    using namespace vice;

    auto domain = rigid_body::Domain::create(simulation::DomainId{1}, "debug-test");
    require(domain != nullptr, "domain creation");
    domain->set_fidelity(simulation::Fidelity::full);

    const simulation::MaterialId floor_material{100};
    const simulation::MaterialId cube_material{200};
    require(domain->register_material(floor_material, simulation::Material{}), "floor material registration");
    require(domain->register_material(cube_material, simulation::Material{}), "cube material registration");

    rigid_body::BoxBodyDesc floor;
    floor.participant = simulation::ParticipantId{10};
    floor.material = floor_material;
    floor.motion = rigid_body::MotionType::static_body;
    floor.half_extents = {4.0F, 0.5F, 4.0F};
    floor.position = {0.0F, -0.5F, 0.0F};
    require(domain->create_box(floor), "floor creation");

    rigid_body::BoxBodyDesc cube;
    cube.participant = simulation::ParticipantId{20};
    cube.material = cube_material;
    cube.motion = rigid_body::MotionType::dynamic_body;
    cube.motion_quality = rigid_body::MotionQuality::continuous;
    cube.half_extents = {0.75F, 0.5F, 0.25F};
    cube.position = {0.0F, 2.0F, 0.0F};
    cube.linear_velocity = {1.0F, 0.0F, 0.0F};
    cube.angular_velocity = {0.0F, 0.5F, 0.0F};
    require(domain->create_box(cube), "cube creation");

    const auto body = rigid_body::debug_snapshot(*domain, cube);
    require(body.has_value(), "body snapshot");
    require(body->participant == cube.participant, "body participant");
    require(body->material == cube.material, "body material");
    require(body->motion == rigid_body::MotionType::dynamic_body, "body motion type");
    require(body->motion_quality == rigid_body::MotionQuality::continuous, "body motion quality");
    require(body->box_half_extents.has_value(), "box extents available");
    require(body->box_half_extents->x == 0.75F, "box extent x");
    require(body->linear_velocity.x == 1.0F, "body velocity");

    rigid_body::CharacterDesc character;
    character.id = rigid_body::CharacterId{7};
    character.position = {1.0F, 0.0F, 1.0F};
    character.height = 1.9F;
    character.radius = 0.35F;
    require(domain->create_character(character), "character creation");

    const auto character_snapshot = rigid_body::debug_snapshot(*domain, character);
    require(character_snapshot.has_value(), "character snapshot");
    require(character_snapshot->id == character.id, "character id");
    require(character_snapshot->height == character.height, "character height");
    require(character_snapshot->radius == character.radius, "character radius");

    rigid_body::DistanceConstraintDesc distance;
    distance.id = rigid_body::ConstraintId{50};
    distance.first = floor.participant;
    distance.second = cube.participant;
    distance.min_distance = 0.25F;
    distance.max_distance = 4.0F;
    require(domain->create_constraint(distance), "constraint creation");

    const auto constraint = rigid_body::debug_snapshot(*domain, distance);
    require(constraint.has_value(), "constraint snapshot");
    require(constraint->id == distance.id, "constraint id");
    require(constraint->first == floor.participant, "constraint first participant");
    require(constraint->second == cube.participant, "constraint second participant");
    require(constraint->type == rigid_body::ConstraintType::distance, "constraint type");

    return 0;
}
