#include <vice/rigid_body/rigid_body.hpp>
#include <vice/simulation/simulation.hpp>

#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

namespace {

bool near(const float left, const float right, const float tolerance = 0.08F) {
    return std::abs(left - right) <= tolerance;
}

vice::simulation::Material rigid_material(const double friction) {
    vice::simulation::Material material;
    assert(material.set_property(
        std::string{vice::rigid_body::material_property_owner},
        std::string{vice::rigid_body::material_friction_property},
        friction
    ));
    return material;
}

} // namespace

int main() {
    using namespace vice;

    auto domain = rigid_body::Domain::create(
        simulation::DomainId{600},
        "rigid.heightfield"
    );
    assert(domain != nullptr);

    simulation::Coordinator coordinator{1.0 / 120.0};
    assert(coordinator.register_domain(*domain));

    rigid_body::HeightFieldShapeDesc invalid_small;
    invalid_small.sample_count = 3;
    invalid_small.samples.assign(9U, 0.0F);
    assert(!domain->register_shape(rigid_body::ShapeId{90}, invalid_small));

    rigid_body::HeightFieldShapeDesc invalid_count;
    invalid_count.sample_count = 4;
    invalid_count.samples.assign(15U, 0.0F);
    assert(!domain->register_shape(rigid_body::ShapeId{91}, invalid_count));

    rigid_body::HeightFieldShapeDesc invalid_spacing;
    invalid_spacing.sample_count = 4;
    invalid_spacing.sample_spacing_x = 0.0F;
    invalid_spacing.samples.assign(16U, 0.0F);
    assert(!domain->register_shape(rigid_body::ShapeId{92}, invalid_spacing));

    rigid_body::HeightFieldShapeDesc invalid_sample;
    invalid_sample.sample_count = 4;
    invalid_sample.samples.assign(16U, 0.0F);
    invalid_sample.samples[3] = std::numeric_limits<float>::quiet_NaN();
    assert(!domain->register_shape(rigid_body::ShapeId{93}, invalid_sample));

    rigid_body::HeightFieldShapeDesc all_holes;
    all_holes.sample_count = 4;
    all_holes.samples.assign(16U, rigid_body::heightfield_hole_sample);
    assert(!domain->register_shape(rigid_body::ShapeId{94}, all_holes));

    const rigid_body::ShapeId terrain_shape{100};
    rigid_body::HeightFieldShapeDesc terrain;
    terrain.sample_count = 4;
    terrain.offset = math::Vec3{-3.0F, 1.0F, -3.0F};
    terrain.sample_spacing_x = 2.0F;
    terrain.sample_spacing_z = 2.0F;
    terrain.vertical_scale = 2.0F;
    terrain.samples.assign(16U, 2.0F);
    terrain.samples[0] = rigid_body::heightfield_hole_sample;
    terrain.samples[1] = rigid_body::heightfield_hole_sample;
    terrain.samples[4] = rigid_body::heightfield_hole_sample;
    terrain.samples[5] = rigid_body::heightfield_hole_sample;
    assert(domain->register_shape(terrain_shape, terrain));

    const rigid_body::ShapeId sphere_shape{101};
    assert(domain->register_shape(
        sphere_shape,
        rigid_body::SphereShapeDesc{.radius = 0.5F}
    ));

    const simulation::MaterialId ground_material{10};
    const simulation::MaterialId ball_material{20};
    assert(domain->register_material(ground_material, rigid_material(0.9)));
    assert(domain->register_material(ball_material, rigid_material(0.4)));

    rigid_body::MaterialInteraction interaction;
    interaction.label = "terrain_contact";
    assert(domain->set_material_interaction(
        ground_material,
        ball_material,
        interaction
    ));

    const simulation::ParticipantId terrain_participant{10};
    rigid_body::BodyDesc terrain_body;
    terrain_body.participant = terrain_participant;
    terrain_body.material = ground_material;
    terrain_body.shape = terrain_shape;
    terrain_body.motion = rigid_body::MotionType::static_body;
    assert(domain->create_body(terrain_body));

    rigid_body::BodyDesc dynamic_terrain = terrain_body;
    dynamic_terrain.participant = simulation::ParticipantId{11};
    dynamic_terrain.motion = rigid_body::MotionType::dynamic_body;
    assert(!domain->create_body(dynamic_terrain));

    const auto solid_ray = domain->raycast(rigid_body::RaycastDesc{
        .origin = math::Vec3{2.0F, 12.0F, 2.0F},
        .direction = math::Vec3{0.0F, -1.0F, 0.0F},
        .max_distance = 20.0F,
    });
    assert(solid_ray.has_value());
    assert(solid_ray->participant == terrain_participant);
    assert(near(solid_ray->point.y, 5.0F));
    assert(solid_ray->normal.y > 0.9F);

    const auto hole_ray = domain->raycast(rigid_body::RaycastDesc{
        .origin = math::Vec3{-2.0F, 12.0F, -2.0F},
        .direction = math::Vec3{0.0F, -1.0F, 0.0F},
        .max_distance = 20.0F,
    });
    assert(!hole_ray.has_value());

    rigid_body::QueryFilter solid_only;
    solid_only.include_triggers = false;

    const auto overlaps = domain->overlap_shape(
        rigid_body::ShapePose{
            .shape = sphere_shape,
            .position = math::Vec3{2.0F, 5.2F, 2.0F},
        },
        solid_only
    );
    assert(overlaps.size() == 1U);
    assert(overlaps.front().participant == terrain_participant);

    const auto sweep = domain->sweep_shape(
        rigid_body::SweepDesc{
            .pose = rigid_body::ShapePose{
                .shape = sphere_shape,
                .position = math::Vec3{2.0F, 10.0F, 2.0F},
            },
            .direction = math::Vec3{0.0F, -1.0F, 0.0F},
            .max_distance = 10.0F,
        },
        solid_only
    );
    assert(sweep.has_value());
    assert(sweep->participant == terrain_participant);
    assert(sweep->distance > 4.0F && sweep->distance < 5.5F);

    assert(domain->overlap_shape(rigid_body::ShapePose{
        .shape = terrain_shape,
        .position = math::Vec3{},
    }).empty());
    assert(!domain->sweep_shape(rigid_body::SweepDesc{
        .pose = rigid_body::ShapePose{
            .shape = terrain_shape,
            .position = math::Vec3{},
        },
        .direction = math::Vec3{0.0F, -1.0F, 0.0F},
        .max_distance = 1.0F,
    }).has_value());

    const simulation::ParticipantId ball_participant{20};
    rigid_body::BodyDesc ball;
    ball.participant = ball_participant;
    ball.material = ball_material;
    ball.shape = sphere_shape;
    ball.motion = rigid_body::MotionType::dynamic_body;
    ball.position = math::Vec3{2.0F, 8.0F, 2.0F};
    assert(domain->create_body(ball));

    std::optional<rigid_body::ContactEvent> collision;
    for (int step = 0; step < 360 && !collision.has_value(); ++step) {
        coordinator.step();
        for (const auto& event : domain->contact_events()) {
            if (event.kind == rigid_body::ContactKind::collision &&
                event.phase == rigid_body::ContactPhase::began &&
                event.first == terrain_participant &&
                event.second == ball_participant) {
                collision = event;
                break;
            }
        }
    }
    assert(collision.has_value());
    assert(collision->first_material == ground_material);
    assert(collision->second_material == ball_material);
    assert(collision->material_interaction == "terrain_contact");

    for (int step = 0; step < 360; ++step) {
        coordinator.step();
    }
    const auto settled = domain->body_state(ball_participant);
    assert(settled.has_value());
    assert(settled->position.y > 5.35F && settled->position.y < 5.7F);

    assert(domain->destroy_body(ball_participant));
    assert(domain->destroy_body(terrain_participant));

    const simulation::ParticipantId trigger_participant{30};
    rigid_body::BodyDesc trigger_terrain = terrain_body;
    trigger_terrain.participant = trigger_participant;
    trigger_terrain.trigger = true;
    assert(domain->create_body(trigger_terrain));

    const simulation::ParticipantId visitor_participant{40};
    rigid_body::BodyDesc visitor = ball;
    visitor.participant = visitor_participant;
    visitor.position = math::Vec3{2.0F, 8.0F, 2.0F};
    assert(domain->create_body(visitor));

    bool saw_trigger = false;
    for (int step = 0; step < 360 && !saw_trigger; ++step) {
        coordinator.step();
        for (const auto& event : domain->contact_events()) {
            if (event.kind == rigid_body::ContactKind::trigger &&
                event.phase == rigid_body::ContactPhase::began &&
                event.first == trigger_participant &&
                event.second == visitor_participant) {
                saw_trigger = true;
                assert(event.first_material == ground_material);
                assert(event.second_material == ball_material);
                assert(event.material_interaction == "terrain_contact");
            }
        }
    }
    assert(saw_trigger);

    for (int step = 0; step < 180; ++step) {
        coordinator.step();
    }
    const auto visitor_state = domain->body_state(visitor_participant);
    assert(visitor_state.has_value());
    assert(visitor_state->position.y < 4.0F);

    assert(!domain->unregister_shape(terrain_shape));
    assert(domain->destroy_body(visitor_participant));
    assert(domain->destroy_body(trigger_participant));
    assert(domain->unregister_shape(terrain_shape));
    assert(domain->unregister_shape(sphere_shape));
    assert(domain->remove_material_interaction(ground_material, ball_material));
    assert(domain->unregister_material(ball_material));
    assert(domain->unregister_material(ground_material));
    assert(coordinator.unregister_domain(domain->id()));

    return 0;
}
