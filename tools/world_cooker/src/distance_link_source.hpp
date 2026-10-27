#pragma once
#include <vice/world/distance_link.hpp>
#include <charconv>
#include <istream>
#include <ostream>
#include <string>
namespace vice::tools::world_cooker {
inline bool read_distance_link(std::istream& input, world::DistanceLink& link) {
    std::string first, second, state, step;
    if (!(input >> first >> second >> link.min_distance >> link.max_distance >> link.frequency_hz >>
        link.damping_ratio >> link.break_threshold >> state >> link.last_break_impulse >> step)) return false;
    const auto a = world::PersistentId::parse(first), b = world::PersistentId::parse(second);
    if (!a || !b || (state != "intact" && state != "broken")) return false;
    const auto parsed = std::from_chars(step.data(), step.data() + step.size(), link.break_step);
    if (parsed.ec != std::errc{} || parsed.ptr != step.data() + step.size()) return false;
    link.first = *a; link.second = *b; link.broken = state == "broken";
    return world::valid(link);
}
inline void write_distance_link(std::ostream& output, const world::DistanceLink& link) {
    output << link.first.to_string() << ' ' << link.second.to_string() << ' ' << link.min_distance << ' ' << link.max_distance
        << ' ' << link.frequency_hz << ' ' << link.damping_ratio << ' ' << link.break_threshold << ' '
        << (link.broken ? "broken" : "intact") << ' ' << link.last_break_impulse << ' ' << link.break_step;
}
}
