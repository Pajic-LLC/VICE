#pragma once
#include <istream>
#include <ostream>
#include <vice/world/presentation.hpp>
namespace vice::world {
inline bool read_animation(std::istream &in, AnimationPlayer &p) {
    std::string id;
    unsigned playing = 0, looping = 0;
    if (!(in >> id >> p.clip >> p.time >> p.rate >> playing >> looping) || playing > 1 ||
        looping > 1)
        return false;
    const auto parsed = PersistentId::parse(id);
    if (!parsed)
        return false;
    p.rig = {*parsed};
    p.playing = playing != 0;
    p.looping = looping != 0;
    return valid(p);
}
inline void write_animation(std::ostream &o, const AnimationPlayer &p) {
    o << p.rig.value.to_string() << ' ' << p.clip << ' ' << p.time << ' ' << p.rate << ' '
      << p.playing << ' ' << p.looping;
}
inline bool read_rig_part(std::istream &in, RigPart &p) {
    std::string id;
    if (!(in >> id >> p.node))
        return false;
    const auto parsed = PersistentId::parse(id);
    if (!parsed)
        return false;
    p.root = *parsed;
    return valid(p);
}
inline void write_rig_part(std::ostream &o, const RigPart &p) {
    o << p.root.to_string() << ' ' << p.node;
}
inline bool read_audio_emitter(std::istream &in, AudioEmitter &p) {
    std::string id;
    unsigned playing = 0, looping = 0;
    if (!(in >> id >> p.time >> p.gain >> p.pitch >> p.reference_distance >> p.max_distance >>
          playing >> looping) ||
        playing > 1 || looping > 1)
        return false;
    const auto parsed = PersistentId::parse(id);
    if (!parsed)
        return false;
    p.sound = {*parsed};
    p.playing = playing != 0;
    p.looping = looping != 0;
    return valid(p);
}
inline void write_audio_emitter(std::ostream &o, const AudioEmitter &p) {
    o << p.sound.value.to_string() << ' ' << p.time << ' ' << p.gain << ' ' << p.pitch << ' '
      << p.reference_distance << ' ' << p.max_distance << ' ' << p.playing << ' ' << p.looping;
}
} // namespace vice::world
