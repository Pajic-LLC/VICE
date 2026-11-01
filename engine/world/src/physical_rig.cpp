#include <algorithm>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vice/world/world.hpp>
namespace vice::world {
namespace {
bool unit_scale(const std::optional<AuthoredTransform> &t) {
    return !t || (t->scale.x == 1 && t->scale.y == 1 && t->scale.z == 1);
}
} // namespace
bool validate_physical_rigs(const WorldSnapshot &s, std::string *d) {
    if (s.physical_rig_bindings.empty())
        return true;
    const auto fail = [&](const char *m) {
        if (d)
            *d = m;
        return false;
    };
    if (s.physical_rig_bindings.size() > 4096)
        return fail("Physical rig binding capacity exceeded.");
    std::unordered_map<PersistentId, const SnapshotEntity *, PersistentIdHash> entities;
    for (const auto &e : s.entities)
        entities.emplace(e.persistent_id, &e);
    std::unordered_set<PersistentId, PersistentIdHash> bodies;
    std::unordered_map<PersistentId, std::unordered_set<std::uint32_t>, PersistentIdHash> nodes;
    for (const auto &record : s.physical_rig_bindings) {
        const auto &b = record.binding;
        const auto body = entities.find(record.body), root = entities.find(b.root);
        if (!valid(b) || !bodies.insert(record.body).second || body == entities.end() ||
            root == entities.end() || !body->second->rigid_box || !body->second->spatial_state ||
            !root->second->spatial_state || !root->second->animation ||
            body->second->spatial_state->region != root->second->spatial_state->region ||
            !unit_scale(root->second->authored_transform) || !nodes[b.root].insert(b.node).second)
            return fail("Invalid or duplicate physical rig body/root/node, region or root scale.");
    }
    return true;
}
bool World::has_physical_rig_bindings(PersistentId root) const noexcept {
    return physical_rig_users_.contains(root);
}
std::optional<PhysicalRigBinding> World::physical_rig_binding(EntityId e) const noexcept {
    if (!alive(e))
        return {};
    const auto i = physical_rigs_.find(*persistent_id(e));
    return i == physical_rigs_.end() ? std::nullopt : std::optional{i->second};
}
std::vector<OwnedPhysicalRigBinding> World::physical_rig_bindings(PersistentId root) const {
    std::vector<OwnedPhysicalRigBinding> result;
    const auto users = physical_rig_users_.find(root);
    if (users == physical_rig_users_.end())
        return result;
    result.reserve(users->second.size());
    for (auto e : users->second) {
        const auto id = *persistent_id(e);
        result.push_back({id, physical_rigs_.at(id)});
    }
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.binding.node < b.binding.node; });
    return result;
}
bool World::set_physical_rig_binding(EntityId e, const PhysicalRigBinding &b) {
    if (!alive(e) || !valid(b) || !rigid_box(e))
        return false;
    const auto root = find(b.root);
    const auto body_position = spatial_state(e);
    const auto root_position = root ? spatial_state(*root) : std::nullopt;
    if (!root || !animation(*root) || !body_position || !root_position ||
        body_position->region != root_position->region || !unit_scale(authored_transform(*root)))
        return false;
    const auto owner = *persistent_id(e);
    const auto old = physical_rig_binding(e);
    if (old == b)
        return true;
    if (!old && physical_rigs_.size() >= 4096)
        return false;
    const auto users = physical_rig_users_.find(b.root);
    if (users != physical_rig_users_.end())
        for (auto other : users->second)
            if (other != e && physical_rig_binding(other)->node == b.node)
                return false;
    if (!advance_presentation_revision())
        return false;
    if (old) {
        auto i = physical_rig_users_.find(old->root);
        std::erase(i->second, e);
        if (i->second.empty())
            physical_rig_users_.erase(i);
    }
    physical_rigs_.insert_or_assign(owner, b);
    physical_rig_users_[b.root].push_back(e);
    return true;
}
bool World::clear_physical_rig_binding(EntityId e) {
    if (!alive(e))
        return false;
    const auto old = physical_rig_binding(e);
    if (!old)
        return true;
    if (!advance_presentation_revision())
        return false;
    auto i = physical_rig_users_.find(old->root);
    std::erase(i->second, e);
    if (i->second.empty())
        physical_rig_users_.erase(i);
    physical_rigs_.erase(*persistent_id(e));
    return true;
}
bool World::physical_rig_region_allowed(EntityId e, RegionId region) const {
    if (const auto b = physical_rig_binding(e); b && find(b->root) != e) {
        const auto root = find(b->root);
        if (!root || !spatial_state(*root) || spatial_state(*root)->region != region)
            return false;
    }
    if (const auto users = physical_rig_users_.find(*persistent_id(e));
        users != physical_rig_users_.end())
        for (auto body : users->second)
            if (body != e && (!spatial_state(body) || spatial_state(body)->region != region))
                return false;
    return true;
}
} // namespace vice::world

namespace vice::world {
std::string serialize_physical_rig(const std::optional<PhysicalRigBinding> &p) {
    if (!p)
        return "null";
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o.precision(9);
    const auto &b = *p;
    o << "{\"root\":\"" << b.root.to_string() << "\",\"node\":" << b.node << ",\"translation\":["
      << b.translation[0] << ',' << b.translation[1] << ',' << b.translation[2]
      << "],\"rotation\":[" << b.rotation[0] << ',' << b.rotation[1] << ',' << b.rotation[2] << ','
      << b.rotation[3] << "]}";
    return o.str();
}
} // namespace vice::world
