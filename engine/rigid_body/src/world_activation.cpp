#include <vice/rigid_body/world_bridge.hpp>

namespace vice::rigid_body {

bool WorldBridge::activate_region(
    world::World& world,
    simulation::Coordinator& coordinator,
    const world::RegionId region,
    Domain& domain
) {
    const auto region_desc = world.region(region);
    if (!region_desc.has_value()) {
        return false;
    }

    if (auto* existing = find_region(region); existing != nullptr) {
        if (existing->domain != &domain || existing->island != region_desc->simulation_island ||
            !coordinator.registered(domain.id())) {
            return false;
        }
        domain.set_fidelity(simulation::Fidelity::full);
        return world.set_region_fidelity(region, world::RegionFidelity::full);
    }

    const auto previous_world_fidelity = region_desc->fidelity;
    const auto previous_domain_fidelity = domain.fidelity();
    const bool coordinator_was_registered = coordinator.registered(domain.id());

    if (!coordinator_was_registered && !coordinator.register_domain(domain)) {
        return false;
    }

    domain.set_fidelity(simulation::Fidelity::full);
    if (!world.set_region_fidelity(region, world::RegionFidelity::full)) {
        domain.set_fidelity(previous_domain_fidelity);
        if (!coordinator_was_registered) {
            (void)coordinator.unregister_domain(domain.id());
        }
        return false;
    }

    if (!register_region(world, coordinator, region, domain)) {
        (void)world.set_region_fidelity(region, previous_world_fidelity);
        domain.set_fidelity(previous_domain_fidelity);
        if (!coordinator_was_registered) {
            (void)coordinator.unregister_domain(domain.id());
        }
        return false;
    }

    return true;
}

} // namespace vice::rigid_body
