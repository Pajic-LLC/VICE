#include <vice/runtime/resource_residency.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "runtime resource residency test failed: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] vice::world::ResourceId resource(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    require(parsed.has_value(), "fixed UUID parses");
    return vice::world::ResourceId{*parsed};
}

} // namespace

int main() {
    using namespace vice;

    const auto low_id = resource("90000000-0000-4000-8000-000000000001");
    const auto high_id = resource("90000000-0000-4000-8000-000000000002");
    const auto reduced_id = resource("90000000-0000-4000-8000-000000000003");

    {
        const runtime::RuntimeResourceResidencyCandidate candidates[]{
            {.resource = reduced_id, .bytes = 5U, .fidelity = world::RegionFidelity::reduced},
            {.resource = high_id, .bytes = 5U, .fidelity = world::RegionFidelity::full},
            {.resource = low_id, .bytes = 5U, .fidelity = world::RegionFidelity::full},
        };
        const auto plan = runtime::build_runtime_resource_residency_plan(candidates, 10U);
        require(static_cast<bool>(plan), "priority plan builds");
        require(plan.plan.resident_bytes == 10U, "priority plan fills exact budget");
        require(plan.plan.resources.size() == 2U, "two resources admitted");
        require(plan.plan.resident(low_id), "lower full-fidelity stable ID admitted");
        require(plan.plan.resident(high_id), "higher full-fidelity stable ID admitted");
        require(!plan.plan.resident(reduced_id), "reduced-fidelity resource loses under pressure");
    }

    {
        const runtime::RuntimeResourceResidencyCandidate candidates[]{
            {.resource = high_id, .bytes = 6U, .fidelity = world::RegionFidelity::full},
            {.resource = low_id, .bytes = 6U, .fidelity = world::RegionFidelity::full},
        };
        const auto plan = runtime::build_runtime_resource_residency_plan(candidates, 6U);
        require(static_cast<bool>(plan), "stable-tie plan builds");
        require(plan.plan.resources.size() == 1U, "one equal-priority resource admitted");
        require(plan.plan.resident(low_id), "stable ID deterministically breaks equal-priority tie");
        require(!plan.plan.resident(high_id), "higher stable ID loses equal-priority tie");
    }

    {
        const runtime::RuntimeResourceResidencyCandidate candidates[]{
            {.resource = low_id, .bytes = 4U, .fidelity = world::RegionFidelity::reduced},
            {.resource = low_id, .bytes = 4U, .fidelity = world::RegionFidelity::full},
        };
        const auto plan = runtime::build_runtime_resource_residency_plan(candidates, 4U);
        require(static_cast<bool>(plan), "shared-resource plan builds");
        require(plan.plan.resources.size() == 1U, "shared resource charged once");
        require(plan.plan.resident_bytes == 4U, "shared resource uses one copy of bytes");
    }

    {
        const runtime::RuntimeResourceResidencyCandidate candidates[]{
            {.resource = low_id, .bytes = 4U, .fidelity = world::RegionFidelity::full},
        };
        const auto plan = runtime::build_runtime_resource_residency_plan(candidates, 0U);
        require(static_cast<bool>(plan), "zero-budget plan builds");
        require(plan.plan.resources.empty(), "zero budget admits nothing");
        require(plan.plan.resident_bytes == 0U, "zero budget charges nothing");
    }

    {
        const runtime::RuntimeResourceResidencyCandidate candidates[]{
            {.resource = low_id, .bytes = 4U, .fidelity = world::RegionFidelity::full},
            {.resource = low_id, .bytes = 5U, .fidelity = world::RegionFidelity::full},
        };
        const auto plan = runtime::build_runtime_resource_residency_plan(candidates, 10U);
        require(!static_cast<bool>(plan), "conflicting shared-resource byte sizes are rejected");
    }

    return 0;
}
