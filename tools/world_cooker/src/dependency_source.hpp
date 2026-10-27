#pragma once
#include <vice/world/dependency.hpp>
#include <istream>
#include <ostream>
namespace vice::tools::world_cooker {
inline bool read_dependency(std::istream& input, world::Dependency& d) {
    std::string provider, gate, enabled;
    if (!(input >> provider >> gate >> enabled) || (enabled != "enabled" && enabled != "disabled")) return false;
    const auto read = [](const std::string& text, world::PersistentId& id) {
        if (text == "none") { id = {}; return true; }
        const auto parsed = world::PersistentId::parse(text);
        if (!parsed || parsed->is_nil()) return false;
        id = *parsed; return true;
    };
    d.enabled = enabled == "enabled";
    return read(provider, d.provider) && read(gate, d.gate);
}
inline void write_dependency(std::ostream& output, const world::Dependency& d) {
    output << (d.provider.is_nil() ? "none" : d.provider.to_string()) << ' '
        << (d.gate.is_nil() ? "none" : d.gate.to_string()) << ' ' << (d.enabled ? "enabled" : "disabled");
}
}
