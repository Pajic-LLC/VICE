#include <vice/runtime/scene.hpp>
#include <vice/world/reflection_probe.hpp>

#include <array>
#include <cassert>
#include <cmath>

namespace {
vice::world::PersistentId id(const std::uint8_t tail) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[15] = tail;
    return vice::world::PersistentId{bytes};
}

bool near(const float left, const float right, const float tolerance = 0.0003F) {
    return std::abs(left - right) <= tolerance;
}

float length(const vice::math::Vec3 value) {
    return std::sqrt(vice::math::dot(value, value));
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
        .position = {0.0, 0.0, 0.0},
    };
    runtime::RuntimeReflectionProbe second = first;
    second.entity = id(2);
    second.environment = world::ResourceId{id(12)};
    second.position = {2.0, 0.0, 0.0};

    const std::array probes{first, second};
    const auto middle = runtime::select_runtime_reflection_probes({1.0, 0.0, 0.0}, probes);
    assert(middle.local_environments[0].has_value());
    assert(middle.local_environments[1].has_value());
    assert(middle.local_probe_indices[0].has_value());
    assert(middle.local_probe_indices[1].has_value());
    assert(middle.local_weights[0] > 0.0F);
    assert(middle.local_weights[1] > 0.0F);
    assert(std::abs(middle.local_weights[0] + middle.local_weights[1] + middle.global_weight - 1.0F) < 0.0001F);

    const auto boundary = runtime::select_runtime_reflection_probes({4.5, 0.0, 0.0}, std::span{&first, 1U});
    assert(boundary.local_weights[0] > 0.0F && boundary.local_weights[0] < 1.0F);
    assert(boundary.global_weight > 0.0F && boundary.global_weight < 1.0F);

    second.probe.priority = 4;
    const std::array prioritized{first, second};
    const auto selected = runtime::select_runtime_reflection_probes({1.0, 0.0, 0.0}, prioritized);
    assert(selected.local_environments[0] == std::optional{second.environment});
    assert(selected.local_probe_indices[0] == std::optional<std::size_t>{1U});
    assert(!selected.local_environments[1].has_value());
    assert(!selected.local_probe_indices[1].has_value());

    // At the capture center box projection is identical to the original ray.
    const math::Vec3 diagonal = math::normalize({1.0F, 0.0F, 1.0F});
    const auto centered = runtime::parallax_correct_reflection_direction({0.0, 0.0, 0.0}, diagonal, first);
    assert(near(centered.x, diagonal.x));
    assert(near(centered.y, diagonal.y));
    assert(near(centered.z, diagonal.z));

    // Moving toward +X shifts the hit point toward +X relative to the capture center.
    const auto projected = runtime::parallax_correct_reflection_direction({1.0, 0.0, 0.0}, diagonal, first);
    assert(projected.x > projected.z);
    assert(near(length(projected), 1.0F));

    // Probe scale participates in the projection box.
    auto scaled = first;
    scaled.authored.scale = {2.0F, 1.0F, 1.0F};
    const auto scaled_projection = runtime::parallax_correct_reflection_direction({1.0, 0.0, 0.0}, diagonal, scaled);
    assert(scaled_projection.x < projected.x);
    assert(near(length(scaled_projection), 1.0F));

    // Rotation is respected and the result remains normalized.
    auto rotated = first;
    rotated.authored.rotation = {0.0F, 0.70710678F, 0.0F, 0.70710678F};
    rotated.probe.half_extents = {8.0F, 4.0F, 2.0F};
    const auto rotated_projection = runtime::parallax_correct_reflection_direction(
        {0.5, 0.0, 0.0},
        math::normalize({0.2F, 0.0F, 1.0F}),
        rotated
    );
    assert(near(length(rotated_projection), 1.0F));

    // Large-world coordinates are subtracted in double precision before projection.
    auto far_probe = first;
    far_probe.position = {1'000'000'000.0, -2'000'000'000.0, 500'000'000.0};
    const auto far_projection = runtime::parallax_correct_reflection_direction(
        {1'000'000'001.0, -2'000'000'000.0, 500'000'000.0},
        diagonal,
        far_probe
    );
    assert(near(far_projection.x, projected.x));
    assert(near(far_projection.y, projected.y));
    assert(near(far_projection.z, projected.z));

    // Outside-volume or degenerate directions fail closed to the original ray.
    const math::Vec3 straight = {0.0F, 1.0F, 0.0F};
    const auto outside = runtime::parallax_correct_reflection_direction({20.0, 0.0, 0.0}, straight, first);
    assert(near(outside.x, straight.x));
    assert(near(outside.y, straight.y));
    assert(near(outside.z, straight.z));

    return 0;
}
