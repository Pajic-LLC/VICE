#pragma once
#include <vice/world/material.hpp>
#include "causal_source.hpp"
#include <iomanip>
namespace vice::tools::world_cooker {
inline bool read_material(std::istream& in,world::MaterialDefinition& m) {
    return read_causal_id(in,m.id) && bool(in>>std::quoted(m.name)>>m.density>>m.friction>>m.restitution) &&
        read_causal_id(in,m.render_material.value) && world::valid(m);
}
inline void write_material(std::ostream& out,const world::MaterialDefinition& m) {
    out<<m.id.to_string()<<' '<<std::quoted(m.name)<<' '<<m.density<<' '<<m.friction<<' '<<m.restitution<<' '
       <<(m.render_material.valid()?m.render_material.value.to_string():"none");
}
}
