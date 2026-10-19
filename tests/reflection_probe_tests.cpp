#include <vice/runtime/scene.hpp>
#include <vice/world/reflection_probe.hpp>

#include <cassert>
#include <cmath>

namespace {
vice::world::PersistentId id(const std::uint8_t tail) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[15] = tail;
    return vice::world::PersistentId{bytes};
}
}

int main() {
    using namespace vice;
    runtime::RuntimeReflectionProbe first{
        .entity = id(1),
        .probe = world::ReflectionProbe{
            .half_extents = {5.0F, 5.0F, 5.0F},
            .blend_distance = 2.0F,
            .priority = 1,
            .enabled = true,
        },
        .environment = world::ResourceId{id(11)},
        .position = {0.0F, 0.0F, 0.0F},
    };
    runtime::RuntimeReflectionProbe second = first;
    second.entity = id(2);
    second.environment = world::ResourceId{id(12)};
    second.position = {2.0F, 0.0F, 0.0F};

    const std::array probes{first, second};
    const auto middle = runtime::select_runtime_reflection_probes({1.0F, 0.0F, 0.0F}, probes);
    assert(middle.local_environments[0].has_value());
    assert(middle.local_environments[1].has_value());
    assert(middle.local_weights[0] > 0.0F);
    assert(middle.local_weights[1] > 0.0F);
    assert(std::abs(middle.local_weights[0] + middle.local_weights[1] + middle.global_weight - 1.0F) < 0.0001F);

    const auto boundary = runtime::select_runtime_reflection_probes({4.5F, 0.0F, 0.0F}, std::span{&first, 1U});
    assert(boundary.local_weights[0] > 0.0F && boundary.local_weights[0] < 1.0F);
    assert(boundary.global_weight > 0.0F && boundary.global_weight < 1.0F);

    second.probe.priority = 4;
    const std::array prioritized{first, second};
    const auto selected = runtime::select_runtime_reflection_probes({1.0F, 0.0F, 0.0F}, prioritized);
    assert(selected.local_environments[0] == std::optional{second.environment});
    assert(!selected.local_environments[1].has_value());
    return 0;
}
