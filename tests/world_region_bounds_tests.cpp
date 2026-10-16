#include <vice/world/world.hpp>

#include <cassert>

int main() {
    using namespace vice;

    world::World world;

    const world::RegionId region_a{20};
    const world::RegionId region_b{30};
    const world::RegionId overlap_low{10};
    const world::RegionId unbounded{40};

    const world::RegionBounds unit_bounds{
        .min = world::LocalPosition{0.0F, -50.0F, -50.0F},
        .max = world::LocalPosition{100.0F, 50.0F, 50.0F},
    };

    assert(world.register_region(world::RegionDesc{
        .id = region_a,
        .simulation_island = world::SimulationIslandId{1},
        .origin = world::GlobalPosition{1'000'000'000.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{0, 0, 0},
        .fidelity = world::RegionFidelity::full,
        .bounds = unit_bounds,
    }));
    assert(world.register_region(world::RegionDesc{
        .id = region_b,
        .simulation_island = world::SimulationIslandId{2},
        .origin = world::GlobalPosition{1'000'000'100.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{1, 0, 0},
        .fidelity = world::RegionFidelity::full,
        .bounds = unit_bounds,
    }));
    assert(world.register_region(world::RegionDesc{
        .id = unbounded,
        .simulation_island = world::SimulationIslandId{4},
        .origin = world::GlobalPosition{0.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{9, 9, 9},
        .fidelity = world::RegionFidelity::full,
    }));

    assert(world.region_containing(world::GlobalPosition{1'000'000'000.0, 0.0, 0.0}) ==
        region_a);
    assert(world.region_containing(world::GlobalPosition{1'000'000'099.999, 0.0, 0.0}) ==
        region_a);
    assert(world.region_containing(world::GlobalPosition{1'000'000'100.0, 0.0, 0.0}) ==
        region_b);
    assert(!world.region_containing(world::GlobalPosition{500.0, 0.0, 0.0}).has_value());

    assert(world.register_region(world::RegionDesc{
        .id = overlap_low,
        .simulation_island = world::SimulationIslandId{3},
        .origin = world::GlobalPosition{1'000'000'120.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{2, 0, 0},
        .fidelity = world::RegionFidelity::reduced,
        .bounds = world::RegionBounds{
            .min = world::LocalPosition{0.0F, -10.0F, -10.0F},
            .max = world::LocalPosition{20.0F, 10.0F, 10.0F},
        },
    }));
    assert(world.region_containing(world::GlobalPosition{1'000'000'125.0, 0.0, 0.0}) ==
        overlap_low);

    assert(!world.register_region(world::RegionDesc{
        .id = world::RegionId{50},
        .simulation_island = world::SimulationIslandId{5},
        .origin = world::GlobalPosition{},
        .streaming_cell = world::StreamingCellId{},
        .fidelity = world::RegionFidelity::full,
        .bounds = world::RegionBounds{
            .min = world::LocalPosition{1.0F, 0.0F, 0.0F},
            .max = world::LocalPosition{1.0F, 2.0F, 2.0F},
        },
    }));

    const auto snapshot = world.snapshot();
    world::World restored;
    assert(restored.restore(snapshot));
    const auto restored_a = restored.region(region_a);
    assert(restored_a.has_value());
    assert(restored_a->bounds.has_value());
    assert(*restored_a->bounds == unit_bounds);
    assert(restored.region_containing(world::GlobalPosition{1'000'000'100.0, 0.0, 0.0}) ==
        region_b);

    return 0;
}
