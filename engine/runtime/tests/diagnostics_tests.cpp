#include <vice/runtime/diagnostics.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "runtime diagnostics test failed: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    using namespace vice;

    world::World world;
    require(world.create_entity().valid(), "create first entity");
    require(world.create_entity().valid(), "create second entity");
    require(world.create_resource().valid(), "create resource");

    require(
        world.register_region(world::RegionDesc{
            .id = world::RegionId{1U},
            .simulation_island = world::SimulationIslandId{10U},
            .origin = world::GlobalPosition{1000000000000.25, -2000000000000.5, 3000000000000.75},
            .streaming_cell = world::StreamingCellId{1, 2, 3},
            .fidelity = world::RegionFidelity::full,
        }),
        "register full-fidelity region"
    );
    require(
        world.register_region(world::RegionDesc{
            .id = world::RegionId{2U},
            .simulation_island = world::SimulationIslandId{20U},
            .origin = world::GlobalPosition{-4000000000000.25, 5000000000000.5, -6000000000000.75},
            .streaming_cell = world::StreamingCellId{-1, -2, -3},
            .fidelity = world::RegionFidelity::reduced,
        }),
        "register reduced-fidelity region"
    );
    require(
        world.register_region(world::RegionDesc{
            .id = world::RegionId{3U},
            .simulation_island = world::SimulationIslandId{30U},
            .origin = world::GlobalPosition{7000000000000.25, 8000000000000.5, 9000000000000.75},
            .streaming_cell = world::StreamingCellId{4, 5, 6},
            .fidelity = world::RegionFidelity::inactive,
        }),
        "register inactive region"
    );

    const auto summary = runtime::summarize_runtime_world(world);
    require(summary.entities == 2U, "entity count comes from authoritative World");
    require(summary.resources == 1U, "resource count comes from authoritative World");
    require(summary.relations == 0U, "relation count comes from authoritative World");
    require(summary.regions == 3U, "region count comes from authoritative World");
    require(summary.full_regions == 1U, "full fidelity count");
    require(summary.reduced_regions == 1U, "reduced fidelity count");
    require(summary.inactive_regions == 1U, "inactive fidelity count");

    const auto renderer_unavailable = runtime::runtime_renderer_diagnostics(nullptr);
    require(!renderer_unavailable.available(), "missing renderer is explicitly unavailable");
    require(!renderer_unavailable.directional_shadow.has_value(), "no invented shadow telemetry");
    require(!renderer_unavailable.ray_tracing.has_value(), "no invented ray tracing telemetry");

    return 0;
}
