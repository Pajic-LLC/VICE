#include <vice/world/world.hpp>
#include <algorithm>
#include <iomanip>
#include <locale>
#include <limits>
#include <atomic>
#include <sstream>
#include <unordered_set>
namespace vice::world {
bool World::advance_material_revision() noexcept {
    // A generation identifies material state across World replacement/copies,
    // not only the number of writes performed on one World instance.
    static std::atomic<std::uint64_t> next{1};
    auto value=next.load(std::memory_order_relaxed);
    for(;;) {
        if(value==std::numeric_limits<std::uint64_t>::max())return false;
        if(next.compare_exchange_weak(value,value+1,std::memory_order_relaxed)) {
            material_revision_=value;return true;
        }
    }
}
bool validate_materials(const WorldSnapshot& s,std::string* diagnostics) {
    if(diagnostics) diagnostics->clear();
    const auto fail=[&](const char* message){if(diagnostics)*diagnostics=message;return false;};
    if(s.materials.size()>max_world_materials) return fail("World material catalog exceeds 4096 entries.");
    std::unordered_map<PersistentId,const MaterialDefinition*,PersistentIdHash> materials;
    for(const auto& m:s.materials) {
        if(!valid(m) || !materials.emplace(m.id,&m).second) return fail("Invalid or duplicate World material.");
        if(m.render_material.valid() && std::find(s.resources.begin(),s.resources.end(),m.render_material)==s.resources.end())
            return fail("World material render resource is not registered.");
    }
    for(const auto& e:s.entities) {
        if(e.material.is_nil()) continue;
        const auto found=materials.find(e.material);
        if(found==materials.end()) return fail("Entity references an unknown World material.");
        if(found->second->render_material.valid()) for(const auto& ref:e.resource_references)
            if(ref.role==ResourceRole::render_material && ref.resource!=found->second->render_material)
                return fail("Entity render material conflicts with its shared material.");
    }
    return true;
}
bool World::set_material(const MaterialDefinition& definition) {
    if(!valid(definition) || (definition.render_material.valid() && !find_resource(definition.render_material))) return false;
    const auto old=materials_.find(definition.id);
    if(old!=materials_.end() && old->second==definition) return true;
    if(old==materials_.end() && materials_.size()>=max_world_materials) return false;
    if(definition.render_material.valid()) for(const auto& s:slots_) {
        if(!s.alive || s.material!=definition.id) continue;
        for(const auto& ref:s.resource_references) if(ref.role==ResourceRole::render_material && ref.resource!=definition.render_material) return false;
    }
    if(!advance_material_revision())return false;
    materials_.insert_or_assign(definition.id,definition);return true;
}
bool World::remove_material(PersistentId id) {
    if(!materials_.contains(id)) return false;
    for(const auto& s:slots_) if(s.alive && s.material==id) return false;
    if(!advance_material_revision())return false;
    materials_.erase(id);return true;
}
std::optional<MaterialDefinition> World::material(PersistentId id) const {
    const auto found=materials_.find(id);return found==materials_.end()?std::nullopt:std::optional{found->second};
}
bool World::set_entity_material(EntityId entity,PersistentId id) {
    if(!alive(entity)) return false;
    if(slots_[entity.index].material==id) return true;
    if(!id.is_nil()) {
        const auto m=materials_.find(id);if(m==materials_.end())return false;
        if(m->second.render_material.valid()) for(const auto& ref:slots_[entity.index].resource_references)
            if(ref.role==ResourceRole::render_material && ref.resource!=m->second.render_material) return false;
    }
    if(!advance_material_revision())return false;
    slots_[entity.index].material=id;return true;
}
PersistentId World::entity_material(EntityId entity) const noexcept {
    return alive(entity)?slots_[entity.index].material:PersistentId{};
}
std::optional<MaterialSurface> World::material_surface(EntityId entity) const noexcept {
    if(!alive(entity))return std::nullopt;
    const auto& s=slots_[entity.index];
    MaterialSurface result;result.id=s.material;
    if(!s.material.is_nil()) {
        const auto m=materials_.find(s.material);if(m==materials_.end())return std::nullopt;
        result.density=m->second.density;result.friction=m->second.friction;result.restitution=m->second.restitution;
        result.render_material=m->second.render_material;
    } else if(s.rigid_box) {result.friction=s.rigid_box->friction;result.restitution=s.rigid_box->restitution;}
    else return std::nullopt;
    if(const auto render=resource_reference(entity,ResourceRole::render_material))result.render_material=*render;
    if(s.rigid_box) {
        const auto scale=s.authored_transform.value_or(AuthoredTransform{}).scale;
        const auto h=s.rigid_box->half_extents;
        result.volume=8.0*static_cast<double>(h.x)*scale.x*static_cast<double>(h.y)*scale.y*static_cast<double>(h.z)*scale.z;
        if(s.rigid_box->motion==RigidMotion::dynamic_body)result.mass=result.volume*result.density;
        if(!std::isfinite(result.volume) || !std::isfinite(result.mass))return std::nullopt;
    }
    return result;
}
std::string serialize_material_definition(const MaterialDefinition& m) {
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);
    // std::quoted does not JSON-escape control characters; validation excludes them.
    out<<"{\"id\":\""<<m.id.to_string()<<"\",\"name\":"<<std::quoted(m.name)
       <<",\"density\":"<<m.density<<",\"friction\":"<<m.friction<<",\"restitution\":"<<m.restitution
       <<",\"render_material\":"<<(m.render_material.valid()?"\""+m.render_material.value.to_string()+"\"":"null")<<'}';return out.str();
}
std::string serialize_material_surface(const std::optional<MaterialSurface>& s) {
    if(!s)return "null";
    const auto id=[](PersistentId x){return x.is_nil()?std::string{"null"}:"\""+x.to_string()+"\"";};
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
    out<<"{\"id\":"<<id(s->id)<<",\"render_material\":"<<id(s->render_material.value)<<",\"density\":"<<s->density
       <<",\"friction\":"<<s->friction<<",\"restitution\":"<<s->restitution<<",\"volume\":"<<s->volume<<",\"mass\":"<<s->mass<<'}';return out.str();
}
}
