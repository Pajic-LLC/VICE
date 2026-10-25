#pragma once

#include <cstdint>

namespace vice::runtime {

// Portable hit payload shared by hybrid ray effects. The position is always in the
// current camera-relative Runtime ray frame. instance_id_plus_one addresses the
// authoritative Runtime scene instance, with zero reserved as the miss sentinel.
struct RuntimeRaySceneHit {
    float world_position[3]{};
    std::uint32_t instance_id_plus_one = 0U;
};

static_assert(sizeof(RuntimeRaySceneHit) == 16U);

} // namespace vice::runtime
