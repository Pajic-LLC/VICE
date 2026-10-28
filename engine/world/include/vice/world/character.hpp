#pragma once
#include <vice/math/math.hpp>
#include <vice/world/persistent_id.hpp>
#include <cmath>
#include <optional>
#include <string>
namespace vice::world {
// A character carried by a solid rigid body has no independently stepped capsule.
struct CharacterAttachment {
    PersistentId carrier{};
    math::Vec3 offset{}; // carrier-local foot point, physical meters
    friend bool operator==(const CharacterAttachment& a,const CharacterAttachment& b) noexcept {
        return a.carrier==b.carrier && a.offset.x==b.offset.x && a.offset.y==b.offset.y && a.offset.z==b.offset.z;
    }
};
[[nodiscard]] inline bool valid(const CharacterAttachment& a) noexcept {
    return !a.carrier.is_nil() && std::isfinite(a.offset.x) && std::isfinite(a.offset.y) && std::isfinite(a.offset.z) &&
        std::hypot(a.offset.x,a.offset.y,a.offset.z)<=100;
}
[[nodiscard]] std::string serialize_character_attachment(const std::optional<CharacterAttachment>& attachment);
// Y-up capsule. Spatial position is feet; dimensions are physical meters.
// AuthoredTransform is visual only and does not rescale or tilt the controller.
struct Character {
    float height = 1.8F;
    float radius = 0.3F;
    float mass = 70;
    float max_strength = 100;
    float max_slope_angle = 0.872664626F;
    float step_height = 0.4F;
    float stick_to_floor_distance = 0.5F;
    math::Vec3 velocity{};
    math::Vec3 horizontal_velocity{};
    std::optional<float> pending_jump_speed;
    friend bool operator==(const Character& a, const Character& b) noexcept {
        const auto same = [](math::Vec3 x, math::Vec3 y) { return x.x == y.x && x.y == y.y && x.z == y.z; };
        return a.height == b.height && a.radius == b.radius && a.mass == b.mass && a.max_strength == b.max_strength &&
            a.max_slope_angle == b.max_slope_angle && a.step_height == b.step_height && a.stick_to_floor_distance == b.stick_to_floor_distance &&
            same(a.velocity,b.velocity) && same(a.horizontal_velocity,b.horizontal_velocity) && a.pending_jump_speed == b.pending_jump_speed;
    }
};
[[nodiscard]] inline bool valid(const Character& c) noexcept {
    const auto positive = [](float x) { return std::isfinite(x) && x > 0; };
    const auto nonnegative = [](float x) { return std::isfinite(x) && x >= 0; };
    const auto finite = [](math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    return positive(c.height) && positive(c.radius) && c.height > 2*c.radius && positive(c.mass) &&
        nonnegative(c.max_strength) && nonnegative(c.max_slope_angle) && c.max_slope_angle <= 1.5707963267948966F &&
        nonnegative(c.step_height) && nonnegative(c.stick_to_floor_distance) && finite(c.velocity) &&
        finite(c.horizontal_velocity) && c.horizontal_velocity.y == 0 && (!c.pending_jump_speed || positive(*c.pending_jump_speed));
}
[[nodiscard]] std::string serialize_character(const std::optional<Character>& character);
}
