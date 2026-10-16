#include <vice/rigid_body/material_asset.hpp>
#include <vice/tools/rigid_material_cooker.hpp>

#include <cassert>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view source_a = R"(vice-rigid-material-source 1
material 20000000-0000-4000-8000-000000000001 10 "terrain_concrete"
property 20000000-0000-4000-8000-000000000001 "rigid_body" "friction" real 0.81
property 20000000-0000-4000-8000-000000000001 "rigid_body" "restitution" real 0.01
material 20000000-0000-4000-8000-000000000002 20 "cube_alloy"
property 20000000-0000-4000-8000-000000000002 "rigid_body" "friction" real 0.24
property 20000000-0000-4000-8000-000000000002 "rigid_body" "restitution" real 0.08
interaction 20000000-0000-4000-8000-000000000002 20000000-0000-4000-8000-000000000001 0.18 0.03 "cube_on_terrain"
)";

constexpr std::string_view source_b = R"(vice-rigid-material-source 1
material 20000000-0000-4000-8000-000000000002 20 "cube_alloy"
property 20000000-0000-4000-8000-000000000002 "rigid_body" "restitution" real 0.08
property 20000000-0000-4000-8000-000000000002 "rigid_body" "friction" real 0.24
material 20000000-0000-4000-8000-000000000001 10 "terrain_concrete"
property 20000000-0000-4000-8000-000000000001 "rigid_body" "restitution" real 0.01
property 20000000-0000-4000-8000-000000000001 "rigid_body" "friction" real 0.81
interaction 20000000-0000-4000-8000-000000000002 20000000-0000-4000-8000-000000000001 0.18 0.03 "cube_on_terrain"
)";

} // namespace

int main() {
    using namespace vice;

    const auto cooked_a = tools::rigid_material_cooker::cook_source_manifest(source_a);
    const auto cooked_b = tools::rigid_material_cooker::cook_source_manifest(source_b);
    assert(cooked_a);
    assert(cooked_b);
    assert(cooked_a.bytes == cooked_b.bytes);

    const auto decoded = rigid_body::decode_rigid_material_asset(cooked_a.bytes);
    assert(decoded.asset.has_value());
    assert(decoded.asset->materials.size() == 2U);
    assert(decoded.asset->interactions.size() == 1U);
    assert(decoded.asset->materials[0].name == "terrain_concrete");
    assert(decoded.asset->materials[0].runtime_id == simulation::MaterialId{10});
    assert(decoded.asset->materials[1].name == "cube_alloy");
    assert(decoded.asset->materials[1].runtime_id == simulation::MaterialId{20});
    assert(decoded.asset->interactions[0].interaction.label == "cube_on_terrain");
    assert(decoded.asset->interactions[0].interaction.friction == 0.18F);
    assert(decoded.asset->interactions[0].interaction.restitution == 0.03F);

    auto corrupt = cooked_a.bytes;
    corrupt.back() ^= std::byte{0x01};
    assert(!rigid_body::decode_rigid_material_asset(corrupt).asset.has_value());

    assert(!tools::rigid_material_cooker::cook_source_manifest(
        "vice-rigid-material-source 1\n"
        "material nope 1 \"bad\"\n"
    ));

    auto domain = rigid_body::Domain::create(simulation::DomainId{500}, "material.asset.test");
    assert(domain != nullptr);
    std::string diagnostics;
    assert(rigid_body::register_rigid_material_asset(*domain, *decoded.asset, &diagnostics));
    assert(diagnostics.empty());
    assert(domain->material_count() == 2U);
    assert(domain->material_interaction_count() == 1U);

    rigid_body::BoxBodyDesc floor;
    floor.participant = simulation::ParticipantId{1};
    floor.material = simulation::MaterialId{10};
    floor.motion = rigid_body::MotionType::static_body;
    floor.half_extents = math::Vec3{5.0F, 0.5F, 5.0F};
    floor.position = math::Vec3{0.0F, -0.5F, 0.0F};
    assert(domain->create_box(floor));

    rigid_body::BoxBodyDesc cube;
    cube.participant = simulation::ParticipantId{2};
    cube.material = simulation::MaterialId{20};
    cube.motion = rigid_body::MotionType::dynamic_body;
    cube.position = math::Vec3{0.0F, 2.0F, 0.0F};
    assert(domain->create_box(cube));

    simulation::Coordinator coordinator{1.0 / 120.0};
    assert(coordinator.register_domain(*domain));
    std::optional<rigid_body::ContactEvent> contact;
    for (int step = 0; step < 300 && !contact.has_value(); ++step) {
        coordinator.step();
        for (const auto& event : domain->contact_events()) {
            if (event.kind == rigid_body::ContactKind::collision &&
                event.phase == rigid_body::ContactPhase::began &&
                event.first == floor.participant && event.second == cube.participant) {
                contact = event;
                break;
            }
        }
    }

    assert(contact.has_value());
    assert(contact->first_material == simulation::MaterialId{10});
    assert(contact->second_material == simulation::MaterialId{20});
    assert(contact->material_interaction == "cube_on_terrain");
    assert(contact->combined_friction == 0.18F);
    assert(contact->combined_restitution == 0.03F);

    assert(domain->destroy_body(cube.participant));
    assert(domain->destroy_body(floor.participant));
    assert(coordinator.unregister_domain(domain->id()));
    return 0;
}
