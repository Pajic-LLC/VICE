#include <vice/rigid_body/rigid_body.hpp>
#include <vice/simulation/simulation.hpp>

#include <cassert>
#include <cmath>
#include <optional>
#include <span>
#include <string>

namespace {

bool near(const float left, const float right, const float tolerance = 0.002F) {
    return std::abs(left - right) <= tolerance;
}

const vice::rigid_body::ContactEvent* find_event(
    const std::span<const vice::rigid_body::ContactEvent> events,
    const vice::rigid_body::ContactKind kind,
    const vice::rigid_body::ContactPhase phase,
    const vice::simulation::ParticipantId first,
    const vice::simulation::ParticipantId second
) {
    for (const auto& event : events) {
        if (event.kind == kind && event.phase == phase &&
            event.first == first && event.second == second) {
            return &event;
        }
    }
    return nullptr;
}

vice::simulation::Material material_with_rigid_properties(
    const double friction,
    const double restitution
) {
    vice::simulation::Material material;
    assert(material.set_property(
        std::string{vice::rigid_body::material_property_owner},
        std::string{vice::rigid_body::material_friction_property},
        friction
    ));
    assert(material.set_property(
        std::string{vice::rigid_body::material_property_owner},
        std::string{vice::rigid_body::material_restitution_property},
        restitution
    ));
    return material;
}

} // namespace

int main() {
    using namespace vice;

    {
        auto domain = rigid_body::Domain::create(
            simulation::DomainId{100},
            "rigid.contacts"
        );
        assert(domain != nullptr);

        simulation::Coordinator coordinator{1.0 / 120.0};
        assert(coordinator.register_domain(*domain));

        const simulation::MaterialId concrete_id{10};
        const simulation::MaterialId steel_id{20};
        auto concrete = material_with_rigid_properties(0.81, 0.0);
        auto steel = material_with_rigid_properties(0.25, 0.0);

        simulation::Material invalid_material;
        assert(invalid_material.set_property(
            std::string{rigid_body::material_property_owner},
            std::string{rigid_body::material_friction_property},
            std::string{"not-a-number"}
        ));
        assert(!domain->register_material(simulation::MaterialId{99}, invalid_material));

        assert(domain->register_material(concrete_id, concrete));
        assert(domain->register_material(steel_id, steel));
        assert(!domain->register_material(concrete_id, concrete));
        assert(domain->material_count() == 2U);

        const simulation::ParticipantId floor_participant{10};
        rigid_body::BoxBodyDesc floor;
        floor.participant = floor_participant;
        floor.material = concrete_id;
        floor.motion = rigid_body::MotionType::static_body;
        floor.half_extents = math::Vec3{5.0F, 0.5F, 5.0F};
        floor.position = math::Vec3{0.0F, -0.5F, 0.0F};
        assert(domain->create_box(floor));

        rigid_body::BoxBodyDesc unknown_material_body;
        unknown_material_body.participant = simulation::ParticipantId{11};
        unknown_material_body.material = simulation::MaterialId{777};
        assert(!domain->create_box(unknown_material_body));

        const simulation::ParticipantId box_participant{20};
        rigid_body::BoxBodyDesc box;
        box.participant = box_participant;
        box.material = steel_id;
        box.motion = rigid_body::MotionType::dynamic_body;
        box.position = math::Vec3{0.0F, 2.0F, 0.0F};
        assert(domain->create_box(box));
        assert(!domain->unregister_material(concrete_id));
        assert(!domain->unregister_material(steel_id));

        std::optional<rigid_body::ContactEvent> began;
        for (int step = 0; step < 300 && !began.has_value(); ++step) {
            coordinator.step();
            if (const auto* event = find_event(
                    domain->contact_events(),
                    rigid_body::ContactKind::collision,
                    rigid_body::ContactPhase::began,
                    floor_participant,
                    box_participant
                )) {
                began = *event;
            }
        }

        assert(began.has_value());
        assert(began->first_material == concrete_id);
        assert(began->second_material == steel_id);
        assert(near(began->combined_friction, 0.45F));
        assert(near(began->combined_restitution, 0.0F));
        assert(began->closing_speed > 0.0F);
        assert(began->estimated_normal_impulse > 0.0F);
        assert(std::isfinite(began->point.x));
        assert(std::isfinite(began->point.y));
        assert(std::isfinite(began->point.z));
        assert(std::isfinite(began->normal.x));
        assert(std::isfinite(began->normal.y));
        assert(std::isfinite(began->normal.z));
        assert(began->material_interaction.empty());

        rigid_body::MaterialInteraction interaction;
        interaction.friction = 0.12F;
        interaction.restitution = 0.0F;
        interaction.label = "steel_on_concrete";
        assert(domain->set_material_interaction(steel_id, concrete_id, interaction));
        assert(domain->material_interaction_count() == 1U);

        std::optional<rigid_body::ContactEvent> persisted;
        for (int step = 0; step < 60 && !persisted.has_value(); ++step) {
            coordinator.step();
            if (const auto* event = find_event(
                    domain->contact_events(),
                    rigid_body::ContactKind::collision,
                    rigid_body::ContactPhase::persisted,
                    floor_participant,
                    box_participant
                )) {
                persisted = *event;
            }
        }

        assert(persisted.has_value());
        assert(near(persisted->combined_friction, 0.12F));
        assert(near(persisted->combined_restitution, 0.0F));
        assert(persisted->material_interaction == "steel_on_concrete");

        rigid_body::MaterialInteraction updated_interaction;
        updated_interaction.label = "hard_surface_contact";
        assert(domain->set_material_interaction(
            concrete_id,
            steel_id,
            updated_interaction
        ));
        assert(domain->material_interaction_count() == 1U);
        assert(domain->remove_material_interaction(steel_id, concrete_id));
        assert(!domain->remove_material_interaction(steel_id, concrete_id));
        assert(domain->material_interaction_count() == 0U);

        assert(domain->destroy_body(box_participant));
        assert(domain->destroy_body(floor_participant));
        assert(domain->unregister_material(steel_id));
        assert(domain->unregister_material(concrete_id));
        assert(domain->material_count() == 0U);
        assert(coordinator.unregister_domain(domain->id()));
    }

    {
        auto domain = rigid_body::Domain::create(
            simulation::DomainId{200},
            "rigid.triggers"
        );
        assert(domain != nullptr);

        simulation::Coordinator coordinator{1.0 / 120.0};
        assert(coordinator.register_domain(*domain));

        const simulation::MaterialId trigger_material_id{30};
        const simulation::MaterialId visitor_material_id{40};
        auto trigger_material = material_with_rigid_properties(0.6, 0.0);
        auto visitor_material = material_with_rigid_properties(0.3, 0.0);
        assert(domain->register_material(trigger_material_id, trigger_material));
        assert(domain->register_material(visitor_material_id, visitor_material));

        rigid_body::MaterialInteraction interaction;
        interaction.label = "checkpoint";
        assert(domain->set_material_interaction(
            trigger_material_id,
            visitor_material_id,
            interaction
        ));

        const simulation::ParticipantId trigger_participant{30};
        rigid_body::BoxBodyDesc trigger;
        trigger.participant = trigger_participant;
        trigger.material = trigger_material_id;
        trigger.motion = rigid_body::MotionType::static_body;
        trigger.half_extents = math::Vec3{2.0F, 0.25F, 2.0F};
        trigger.position = math::Vec3{0.0F, 1.0F, 0.0F};
        trigger.trigger = true;
        assert(domain->create_box(trigger));

        const simulation::ParticipantId visitor_participant{40};
        rigid_body::BoxBodyDesc visitor;
        visitor.participant = visitor_participant;
        visitor.material = visitor_material_id;
        visitor.motion = rigid_body::MotionType::dynamic_body;
        visitor.half_extents = math::Vec3{0.25F, 0.25F, 0.25F};
        visitor.position = math::Vec3{0.0F, 4.0F, 0.0F};
        assert(domain->create_box(visitor));

        bool saw_began = false;
        bool saw_persisted = false;
        bool saw_ended = false;

        for (int step = 0; step < 360 && !saw_ended; ++step) {
            coordinator.step();
            for (const auto& event : domain->contact_events()) {
                if (event.first != trigger_participant ||
                    event.second != visitor_participant ||
                    event.kind != rigid_body::ContactKind::trigger) {
                    continue;
                }

                assert(event.first_material == trigger_material_id);
                assert(event.second_material == visitor_material_id);
                assert(event.material_interaction == "checkpoint");
                assert(near(event.estimated_normal_impulse, 0.0F));

                if (event.phase == rigid_body::ContactPhase::began) {
                    saw_began = true;
                } else if (event.phase == rigid_body::ContactPhase::persisted) {
                    saw_persisted = true;
                } else if (event.phase == rigid_body::ContactPhase::ended) {
                    saw_ended = true;
                }
            }
        }

        assert(saw_began);
        assert(saw_persisted);
        assert(saw_ended);

        const auto visitor_state = domain->body_state(visitor_participant);
        assert(visitor_state.has_value());
        assert(visitor_state->position.y < 0.0F);

        assert(domain->destroy_body(visitor_participant));
        assert(domain->destroy_body(trigger_participant));
        assert(coordinator.unregister_domain(domain->id()));
    }

    return 0;
}
