#include <vice/world/world.hpp>
#include <atomic>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_map>
namespace vice::world {
namespace {
math::Vec3 rotate_offset(AuthoredRotation q,math::Vec3 v) {
    const auto m=math::rotation({q.x,q.y,q.z,q.w});
    return {m(0,0)*v.x+m(0,1)*v.y+m(0,2)*v.z,m(1,0)*v.x+m(1,1)*v.y+m(1,2)*v.z,m(2,0)*v.x+m(2,1)*v.y+m(2,2)*v.z};
}
}
bool World::advance_attachment_revision() noexcept {
    static std::atomic<std::uint64_t> next{1};auto v=next.load(std::memory_order_relaxed);
    for(;;){if(v==std::numeric_limits<std::uint64_t>::max())return false;
        if(next.compare_exchange_weak(v,v+1,std::memory_order_relaxed)){attachment_revision_=v;return true;}}
}
bool validate_character_attachments(const WorldSnapshot& snapshot,std::string* diagnostics) {
    if(std::none_of(snapshot.entities.begin(),snapshot.entities.end(),[](const auto& e){return e.character_attachment.has_value();}))return true;
    std::unordered_map<PersistentId,const SnapshotEntity*,PersistentIdHash> entities;
    for(const auto& e:snapshot.entities)entities.emplace(e.persistent_id,&e);
    for(const auto& e:snapshot.entities)if(e.character_attachment) {
        const auto& a=*e.character_attachment;const auto parent=entities.find(a.carrier);
        if(!valid(a) || !e.character || !e.spatial_state || a.carrier==e.persistent_id || parent==entities.end() ||
            !parent->second->rigid_box || parent->second->rigid_box->trigger || !parent->second->spatial_state) {
            if(diagnostics)*diagnostics="Character attachment requires a capsule and a distinct solid rigid carrier.";return false;
        }
    }
    return true;
}
bool World::has_attached_characters(EntityId carrier) const noexcept {
    if(!alive(carrier))return false;
    for(const auto& s:slots_)if(s.alive && s.character_attachment && s.character_attachment->carrier==slots_[carrier.index].persistent_id)return true;
    return false;
}
std::optional<CharacterAttachment> World::character_attachment(EntityId e) const noexcept {return alive(e)?slots_[e.index].character_attachment:std::nullopt;}
bool World::sync_character_attachment(EntityId e) {
    if(!alive(e) || !slots_[e.index].character_attachment || !slots_[e.index].character)return false;
    const auto a=*slots_[e.index].character_attachment;const auto parent=find(a.carrier);
    if(!parent)return false;
    const auto spatial=spatial_state(*parent);const auto body=rigid_box(*parent);
    if(!spatial || !body || body->trigger)return false;
    const auto transform=authored_transform(*parent).value_or(AuthoredTransform{});
    const auto offset=rotate_offset(transform.rotation,a.offset);
    const SpatialState pose{{spatial->global_position.x+offset.x,spatial->global_position.y+offset.y,spatial->global_position.z+offset.z},spatial->region};
    if(!finite(pose.global_position))return false;
    auto c=*slots_[e.index].character;
    const auto angular=math::cross(body->angular_velocity,offset);
    c.velocity={body->linear_velocity.x+angular.x,body->linear_velocity.y+angular.y,body->linear_velocity.z+angular.z};
    c.horizontal_velocity={};c.pending_jump_speed.reset();
    if(!valid(c))return false;
    return set_spatial_state(e,pose) && set_character(e,c);
}
bool World::set_character_attachment(EntityId e,const CharacterAttachment& a) {
    if(!alive(e) || !slots_[e.index].character || !slots_[e.index].spatial_state || !valid(a))return false;
    const auto parent=find(a.carrier);
    if(!parent || *parent==e || !rigid_box(*parent) || rigid_box(*parent)->trigger || !spatial_state(*parent))return false;
    const auto before=slots_[e.index].character_attachment;
    if(before!=a && !advance_attachment_revision())return false;
    slots_[e.index].character_attachment=a;
    if(sync_character_attachment(e))return true;
    slots_[e.index].character_attachment=before;return false;
}
bool World::clear_character_attachment(EntityId e) {
    if(!alive(e))return false;
    if(slots_[e.index].character_attachment){if(!advance_attachment_revision())return false;slots_[e.index].character_attachment.reset();}
    return true;
}
std::string serialize_character_attachment(const std::optional<CharacterAttachment>& a) {
    if(!a)return "null";
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);
    out<<"{\"carrier\":\""<<a->carrier.to_string()<<"\",\"offset\":["<<a->offset.x<<','<<a->offset.y<<','<<a->offset.z<<"]}";return out.str();
}
}
