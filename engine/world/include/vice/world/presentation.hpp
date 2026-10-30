#pragma once
#include <cmath>
#include <optional>
#include <string>
#include <vice/world/resource.hpp>
namespace vice::world {
struct AnimationPlayer {
    ResourceId rig{};
    std::uint32_t clip = 0;
    double time = 0;
    float rate = 1;
    bool playing = true, looping = true;
    std::uint32_t blend_clip = 0;
    double blend_time = 0;
    float blend_weight = 0;
    friend bool operator==(const AnimationPlayer &, const AnimationPlayer &) = default;
};
struct RigPart {
    PersistentId root{};
    std::uint32_t node = 0;
    friend bool operator==(const RigPart &, const RigPart &) = default;
};
struct AudioEmitter {
    ResourceId sound{};
    double time = 0;
    float gain = 0.1F, pitch = 1, reference_distance = 1, max_distance = 50;
    bool playing = true, looping = true;
    friend bool operator==(const AudioEmitter &, const AudioEmitter &) = default;
};
inline bool valid(const AnimationPlayer &p) noexcept {
    return p.rig.valid() && p.clip < 64 && std::isfinite(p.time) && p.time >= 0 && p.time <= 1e9 &&
           std::isfinite(p.rate) && p.rate >= 0 && p.rate <= 8 && p.blend_clip < 64 && std::isfinite(p.blend_time) &&
           p.blend_time >= 0 && p.blend_time <= 1e9 && std::isfinite(p.blend_weight) &&
           p.blend_weight >= 0 && p.blend_weight <= 1;
}
inline bool valid(const RigPart &p) noexcept { return !p.root.is_nil() && p.node < 128; }
inline bool valid(const AudioEmitter &p) noexcept {
    return p.sound.valid() && std::isfinite(p.time) && p.time >= 0 && p.time <= 1e9 &&
           std::isfinite(p.gain) && p.gain >= 0 && p.gain <= 1 && std::isfinite(p.pitch) &&
           p.pitch >= 0.25F && p.pitch <= 4 && std::isfinite(p.reference_distance) &&
           p.reference_distance > 0 && std::isfinite(p.max_distance) &&
           p.max_distance >= p.reference_distance && p.max_distance <= 10000;
}
[[nodiscard]] std::string serialize_animation(const std::optional<AnimationPlayer> &p);
[[nodiscard]] std::string serialize_rig_part(const std::optional<RigPart> &p);
[[nodiscard]] std::string serialize_audio_emitter(const std::optional<AudioEmitter> &p);
} // namespace vice::world
