#include <vice/world/world.hpp>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
namespace vice::world {
bool validate_characters(const WorldSnapshot& snapshot, std::string* diagnostics) {
    if (diagnostics) diagnostics->clear();
    for (const auto& e : snapshot.entities) {
        if (e.character && (!e.spatial_state || e.rigid_box || !valid(*e.character))) {
            if (diagnostics) *diagnostics = "Character " + e.persistent_id.to_string() + ": requires spatial state, valid capsule/motion settings and no rigid box.";
            return false;
        }
    }
    return true;
}
bool World::set_character(EntityId entity, const Character& c) {
    if (!alive(entity) || !slots_[entity.index].spatial_state || slots_[entity.index].rigid_box || !valid(c)) return false;
    if (slots_[entity.index].character == c) return true;
    slots_[entity.index].character = c; ++character_revision_; return true;
}
bool World::clear_character(EntityId entity) {
    if (!alive(entity)) return false;
    auto& c = slots_[entity.index].character;
    if (c) { c.reset(); ++character_revision_; }
    return true;
}
std::optional<Character> World::character(EntityId entity) const noexcept {
    return alive(entity) ? slots_[entity.index].character : std::nullopt;
}
std::string serialize_character(const std::optional<Character>& character) {
    if (!character) return "null";
    const auto& c = *character;
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(std::numeric_limits<float>::max_digits10);
    const auto vector = [&](math::Vec3 v) { out << '[' << v.x << ',' << v.y << ',' << v.z << ']'; };
    out << "{\"height\":" << c.height << ",\"radius\":" << c.radius << ",\"mass\":" << c.mass
        << ",\"max_strength\":" << c.max_strength << ",\"max_slope_angle\":" << c.max_slope_angle
        << ",\"step_height\":" << c.step_height << ",\"stick_to_floor_distance\":" << c.stick_to_floor_distance
        << ",\"velocity\":"; vector(c.velocity); out << ",\"horizontal_velocity\":"; vector(c.horizontal_velocity);
    out << ",\"pending_jump_speed\":";
    if (c.pending_jump_speed) out << *c.pending_jump_speed; else out << "null";
    out << '}'; return out.str();
}
}
