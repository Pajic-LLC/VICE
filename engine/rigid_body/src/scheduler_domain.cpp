#include <vice/rigid_body/rigid_body.hpp>

#include "jolt_job_system.hpp"

#include <utility>

namespace vice::rigid_body {

std::unique_ptr<Domain> Domain::create(
    const simulation::DomainId id,
    std::string name,
    jobs::Scheduler& scheduler,
    const DomainConfig config
) {
    detail::SchedulerBinding binding{scheduler};
    return Domain::create(id, std::move(name), config);
}

} // namespace vice::rigid_body
