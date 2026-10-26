#include <vice/runtime/debug.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "VICE runtime debug test failed: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] vice::world::PersistentId persistent_id(const std::uint8_t value) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[0] = value;
    bytes[6] = 0x40U;
    bytes[8] = 0x80U;
    bytes[15] = static_cast<std::uint8_t>(value ^ 0x5aU);
    return vice::world::PersistentId{bytes};
}

} // namespace

int main() {
    using namespace vice;

    const auto selected_id = persistent_id(0x31U);
    const auto related_id = persistent_id(0x32U);
    const auto relation_kind = persistent_id(0x70U);
    const world::ResourceId mesh_resource{persistent_id(0x50U)};

    world::World world;
    require(
        world.register_region(world::RegionDesc{
            .id = world::RegionId{7U},
            .simulation_island = world::SimulationIslandId{11U},
            .origin = world::GlobalPosition{
                1000000000000.25,
                -2000000000000.5,
                3000000000000.75,
            },
            .streaming_cell = world::StreamingCellId{123, -456, 789},
            .fidelity = world::RegionFidelity::reduced,
        }),
        "register reduced-fidelity region"
    );

    const auto selected = world.create_entity(selected_id);
    const auto related = world.create_entity(related_id);
    require(selected.has_value(), "create selected entity with stable persistent ID");
    require(related.has_value(), "create related entity with stable persistent ID");
    require(
        world.set_spatial_state(
            *selected,
            world::SpatialState{
                .global_position = world::GlobalPosition{
                    1000000000000.5,
                    -2000000000000.75,
                    3000000000001.0,
                },
                .region = world::RegionId{7U},
            }
        ),
        "set selected entity large-world spatial state"
    );
    require(
        world.set_authored_transform(
            *selected,
            world::AuthoredTransform{
                .rotation = world::AuthoredRotation{0.0F, 0.0F, 0.0F, 1.0F},
                .scale = world::AuthoredScale{2.0F, 3.0F, 4.0F},
            }
        ),
        "set selected entity authored transform"
    );
    require(
        world.set_camera(
            *selected,
            world::Camera{
                .primary = true,
            }
        ),
        "set selected entity camera component"
    );
    require(world.create_resource(mesh_resource).has_value(), "create referenced resource");
    require(
        world.set_resource_reference(
            *selected,
            world::ResourceReference{
                .role = world::ResourceRole::render_mesh,
                .resource = mesh_resource,
            }
        ),
        "set selected entity resource reference"
    );
    require(
        world.add_relation(world::Relation{
            .subject = *selected,
            .kind = relation_kind,
            .object = *related,
        }),
        "add outgoing relation"
    );
    require(
        world.add_relation(world::Relation{
            .subject = *related,
            .kind = relation_kind,
            .object = *selected,
        }),
        "add incoming relation"
    );

    const auto before = world.snapshot();
    const auto debug = runtime::sample_runtime_debug_entity(world, selected_id);
    const auto after = world.snapshot();

    require(debug.state == runtime::RuntimeDebugEntityState::available, "selected entity available");
    require(debug.requested_id == selected_id, "requested persistent ID preserved");
    require(debug.spatial.has_value(), "spatial state exposed");
    require(
        debug.spatial->global_position == world::GlobalPosition{
            1000000000000.5,
            -2000000000000.75,
            3000000000001.0,
        },
        "large-world coordinates remain authoritative doubles"
    );
    require(debug.authored_transform.has_value(), "authored transform exposed");
    require(debug.authored_transform->scale == world::AuthoredScale{2.0F, 3.0F, 4.0F},
        "authored scale preserved");
    require(debug.region.has_value(), "active region exposed");
    require(debug.region->id == world::RegionId{7U}, "region identity preserved");
    require(debug.region->simulation_island == world::SimulationIslandId{11U},
        "simulation island preserved");
    require(debug.region->fidelity == world::RegionFidelity::reduced,
        "reduced fidelity exposed explicitly");
    require(debug.region->streaming_cell == world::StreamingCellId{123, -456, 789},
        "streaming cell exposed explicitly");
    require(debug.camera, "camera presence exposed");
    require(!debug.light, "missing light not invented");
    require(!debug.reflection_probe, "missing reflection probe not invented");
    require(debug.resource_reference_count == 1U, "resource reference count exposed");
    require(debug.outgoing_relation_count == 1U, "outgoing relation count exposed");
    require(debug.incoming_relation_count == 1U, "incoming relation count exposed");

    require(before.entities.size() == after.entities.size(), "debug sampling does not change entities");
    require(before.resources.size() == after.resources.size(), "debug sampling does not change resources");
    require(before.regions.size() == after.regions.size(), "debug sampling does not change regions");
    require(before.relations.size() == after.relations.size(), "debug sampling does not change relations");
    require(after.entities.front().persistent_id == before.entities.front().persistent_id,
        "debug sampling does not replace entity identity");
    require(after.entities.front().spatial_state == before.entities.front().spatial_state,
        "debug sampling does not mutate spatial state");

    const auto json = runtime::serialize_runtime_debug_entity_snapshot(debug, "session-debug-17");
    require(json.starts_with("{\"schema\":\"vice.dev.runtime.debug.v1\""), "versioned debug schema");
    require(json.find("\"session_id\":\"session-debug-17\"") != std::string::npos,
        "session identity serialized");
    require(json.find("\"state\":\"available\"") != std::string::npos,
        "availability serialized");
    require(json.find(selected_id.to_string()) != std::string::npos,
        "persistent entity identity serialized");
    require(json.find("1000000000000.5") != std::string::npos,
        "large-world x precision serialized");
    require(json.find("-2000000000000.75") != std::string::npos,
        "large-world y precision serialized");
    require(json.find("3000000000001") != std::string::npos,
        "large-world z precision serialized");
    require(json.find("\"fidelity\":\"reduced\"") != std::string::npos,
        "fidelity serialized");
    require(json.find("\"streaming_cell\":[123,-456,789]") != std::string::npos,
        "streaming cell serialized");
    require(json.find("\"outgoing\":1,\"incoming\":1") != std::string::npos,
        "relation counts serialized");
    require(json.find("MTL") == std::string::npos, "debug snapshot has no Metal-native values");
    require(json.find("ID3D12") == std::string::npos, "debug snapshot has no D3D12-native values");
    require(
        runtime::serialize_runtime_debug_entity_snapshot(debug, "session-debug-17") == json,
        "debug serialization deterministic"
    );

    const auto missing = runtime::sample_runtime_debug_entity(world, persistent_id(0x33U));
    require(missing.state == runtime::RuntimeDebugEntityState::not_found,
        "missing entity returns structured not-found state");
    require(!missing.spatial.has_value(), "missing entity does not invent spatial state");

    const auto invalid = runtime::sample_runtime_debug_entity(world, world::PersistentId{});
    require(invalid.state == runtime::RuntimeDebugEntityState::invalid_request,
        "nil persistent ID returns structured invalid-request state");
    require(
        runtime::serialize_runtime_debug_entity_snapshot(invalid, "session-debug-17").find(
            "\"entity_id\":null"
        ) != std::string::npos,
        "invalid selection serializes null identity"
    );

    return 0;
}
