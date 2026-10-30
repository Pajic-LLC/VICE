#include <algorithm>
#include <atomic>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vice/world/world.hpp>
namespace vice::world {
namespace {
bool uniform(const std::optional<AuthoredTransform> &t) {
    return !t || (t->scale.x == t->scale.y && t->scale.x == t->scale.z);
}
} // namespace
bool World::advance_presentation_revision() noexcept {
    static std::atomic<std::uint64_t> next{1};
    auto v = next.load(std::memory_order_relaxed);
    for (;;) {
        if (v == std::numeric_limits<std::uint64_t>::max())
            return false;
        if (next.compare_exchange_weak(v, v + 1, std::memory_order_relaxed)) {
            presentation_revision_ = v;
            return true;
        }
    }
}
bool validate_presentation(const WorldSnapshot &s, std::string *d) {
    if (std::none_of(s.entities.begin(), s.entities.end(),
                     [](const auto &e) { return e.animation || e.rig_part || e.audio_emitter; }))
        return true;
    const auto fail = [&](const char *v) {
        if (d)
            *d = v;
        return false;
    };
    std::unordered_map<PersistentId, const SnapshotEntity *, PersistentIdHash> roots;
    for (const auto &e : s.entities)
        if (e.animation) {
            if (!valid(*e.animation) || !e.spatial_state || e.rig_part ||
                !uniform(e.authored_transform) ||
                std::find(s.resources.begin(), s.resources.end(), e.animation->rig) ==
                    s.resources.end())
                return fail("Invalid animation root/resource or nonuniform root scale.");
            roots.emplace(e.persistent_id, &e);
        }
    std::unordered_map<PersistentId, std::unordered_set<std::uint32_t>, PersistentIdHash> nodes;
    for (const auto &e : s.entities) {
        if (e.rig_part &&
            (!valid(*e.rig_part) || !e.spatial_state || e.rigid_box || e.character || e.animation ||
             e.rig_part->root == e.persistent_id || !roots.contains(e.rig_part->root) ||
             !nodes[e.rig_part->root].insert(e.rig_part->node).second))
            return fail("Invalid or duplicate rig-part binding; parts must be nonphysical spatial "
                        "entities.");
        if (e.audio_emitter && (!valid(*e.audio_emitter) || !e.spatial_state ||
                                std::find(s.resources.begin(), s.resources.end(),
                                          e.audio_emitter->sound) == s.resources.end()))
            return fail("Invalid audio emitter or sound resource.");
    }
    return true;
}
bool World::has_rig_parts(EntityId e) const noexcept {
    if (!alive(e))
        return false;
    if (has_physical_rig_bindings(*persistent_id(e)))
        return true;
    for (const auto &s : slots_)
        if (s.alive && s.rig_part && s.rig_part->root == slots_[e.index].persistent_id)
            return true;
    return false;
}
bool World::set_animation(EntityId e, const AnimationPlayer &p) {
    if (!alive(e) || !valid(p) || !find_resource(p.rig) || !slots_[e.index].spatial_state ||
        slots_[e.index].rig_part || !uniform(slots_[e.index].authored_transform))
        return false;
    auto &old = slots_[e.index].animation;
    if (old == p)
        return true;
    if ((!old || old->rig != p.rig) && !advance_presentation_revision())
        return false;
    old = p;
    return true;
}
bool World::clear_animation(EntityId e) {
    if (!alive(e) || has_rig_parts(e))
        return false;
    auto &p = slots_[e.index].animation;
    if (p) {
        if (!advance_presentation_revision())
            return false;
        p.reset();
    }
    return true;
}
std::optional<AnimationPlayer> World::animation(EntityId e) const noexcept {
    return alive(e) ? slots_[e.index].animation : std::nullopt;
}
bool World::set_rig_part(EntityId e, const RigPart &p) {
    if (!alive(e) || !valid(p) || !slots_[e.index].spatial_state || slots_[e.index].rigid_box ||
        slots_[e.index].character || slots_[e.index].animation)
        return false;
    const auto root = find(p.root);
    if (!root || *root == e || !animation(*root))
        return false;
    for (const auto &s : slots_)
        if (s.alive && s.persistent_id != slots_[e.index].persistent_id && s.rig_part == p)
            return false;
    auto &old = slots_[e.index].rig_part;
    if (old == p)
        return true;
    if (!advance_presentation_revision())
        return false;
    old = p;
    return true;
}
bool World::clear_rig_part(EntityId e) {
    if (!alive(e))
        return false;
    auto &p = slots_[e.index].rig_part;
    if (p) {
        if (!advance_presentation_revision())
            return false;
        p.reset();
    }
    return true;
}
std::optional<RigPart> World::rig_part(EntityId e) const noexcept {
    return alive(e) ? slots_[e.index].rig_part : std::nullopt;
}
bool World::set_audio_emitter(EntityId e, const AudioEmitter &p) {
    if (!alive(e) || !valid(p) || !find_resource(p.sound) || !slots_[e.index].spatial_state)
        return false;
    auto &old = slots_[e.index].audio_emitter;
    if (old == p)
        return true;
    if ((!old || old->sound != p.sound) && !advance_presentation_revision())
        return false;
    old = p;
    return true;
}
bool World::clear_audio_emitter(EntityId e) {
    if (!alive(e))
        return false;
    auto &p = slots_[e.index].audio_emitter;
    if (p) {
        if (!advance_presentation_revision())
            return false;
        p.reset();
    }
    return true;
}
std::optional<AudioEmitter> World::audio_emitter(EntityId e) const noexcept {
    return alive(e) ? slots_[e.index].audio_emitter : std::nullopt;
}
std::string serialize_animation(const std::optional<AnimationPlayer> &p) {
    if (!p)
        return "null";
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::setprecision(17) << "{\"rig\":\"" << p->rig.value.to_string()
      << "\",\"clip\":" << p->clip << ",\"time\":" << p->time << ",\"rate\":" << p->rate
      << ",\"playing\":" << (p->playing ? "true" : "false")
      << ",\"looping\":" << (p->looping ? "true" : "false")
      << ",\"blend_clip\":" << p->blend_clip << ",\"blend_time\":" << p->blend_time
      << ",\"blend_weight\":" << p->blend_weight << '}';
    return o.str();
}
std::string serialize_rig_part(const std::optional<RigPart> &p) {
    if (!p)
        return "null";
    return "{\"root\":\"" + p->root.to_string() + "\",\"node\":" + std::to_string(p->node) + '}';
}
std::string serialize_audio_emitter(const std::optional<AudioEmitter> &p) {
    if (!p)
        return "null";
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::setprecision(17) << "{\"sound\":\"" << p->sound.value.to_string()
      << "\",\"time\":" << p->time << ",\"gain\":" << p->gain << ",\"pitch\":" << p->pitch
      << ",\"reference_distance\":" << p->reference_distance
      << ",\"max_distance\":" << p->max_distance
      << ",\"playing\":" << (p->playing ? "true" : "false")
      << ",\"looping\":" << (p->looping ? "true" : "false") << '}';
    return o.str();
}
} // namespace vice::world
