#pragma once
#include <vice/world/causal.hpp>
#include <charconv>
#include <istream>
#include <ostream>
namespace vice::tools::world_cooker {
inline bool read_causal_u64(std::istream& in,std::uint64_t& value) {
    std::string text;if(!(in>>text))return false;const auto r=std::from_chars(text.data(),text.data()+text.size(),value);
    return r.ec==std::errc{} && r.ptr==text.data()+text.size();
}
inline bool read_causal_id(std::istream& in,world::PersistentId& id) {
    std::string text;if(!(in>>text))return false;if(text=="none"){id={};return true;}
    const auto p=world::PersistentId::parse(text);if(!p || p->is_nil())return false;id=*p;return true;
}
inline bool read_causal_event(std::istream& in,world::CausalEvent& e) {
    std::string kind,origin,reason,root,quantity;std::uint64_t value=0;
    if(!read_causal_id(in,e.id) || !read_causal_id(in,e.parent_id) || !read_causal_id(in,e.capture_id) || !read_causal_u64(in,e.sequence) || !read_causal_u64(in,e.parent_sequence) ||
        !(in>>kind>>origin) || !read_causal_id(in,e.subject) || !read_causal_id(in,e.first) || !read_causal_id(in,e.second) || !read_causal_id(in,e.root_entity) ||
        !read_causal_u64(in,value) || value>1 || !(in>>reason>>root>>quantity>>e.quantity) || !read_causal_u64(in,e.solver_step) || !read_causal_u64(in,e.region.value))return false;
    e.kind=static_cast<world::CausalKind>(255);e.origin=static_cast<world::CausalOrigin>(255);
    e.reason=e.root_reason=static_cast<world::DependencyReason>(255);e.quantity_kind=static_cast<world::CausalQuantity>(255);e.value=value!=0;
    for(unsigned n=0;n<8;++n)if(kind==world::causal_kind_name(static_cast<world::CausalKind>(n)))e.kind=static_cast<world::CausalKind>(n);
    for(unsigned n=0;n<5;++n)if(origin==world::causal_origin_name(static_cast<world::CausalOrigin>(n)))e.origin=static_cast<world::CausalOrigin>(n);
    for(unsigned n=0;n<4;++n){if(reason==world::dependency_reason_name(static_cast<world::DependencyReason>(n)))e.reason=static_cast<world::DependencyReason>(n);if(root==world::dependency_reason_name(static_cast<world::DependencyReason>(n)))e.root_reason=static_cast<world::DependencyReason>(n);}
    if(quantity=="none")e.quantity_kind=world::CausalQuantity::none;else if(quantity=="linear_impulse")e.quantity_kind=world::CausalQuantity::linear_impulse;
    return true;
}
inline void write_causal_history(std::ostream& out,const world::CausalHistory& h) {
    if(h.id.is_nil())return;
    const auto id=[](world::PersistentId v){return v.is_nil()?std::string{"none"}:v.to_string();};
    out<<"causal_history "<<id(h.id)<<' '<<h.next_sequence<<'\n';
    for(const auto& e:h.events)out<<"causal_event "<<id(e.id)<<' '<<id(e.parent_id)<<' '<<id(e.capture_id)<<' '<<e.sequence<<' '<<e.parent_sequence<<' '
        <<world::causal_kind_name(e.kind)<<' '<<world::causal_origin_name(e.origin)<<' '<<id(e.subject)<<' '<<id(e.first)<<' '<<id(e.second)<<' '<<id(e.root_entity)<<' '
        <<(e.value?1:0)<<' '<<world::dependency_reason_name(e.reason)<<' '<<world::dependency_reason_name(e.root_reason)<<' '
        <<(e.quantity_kind==world::CausalQuantity::linear_impulse?"linear_impulse":"none")<<' '<<e.quantity<<' '<<e.solver_step<<' '<<e.region.value<<'\n';
}
}
