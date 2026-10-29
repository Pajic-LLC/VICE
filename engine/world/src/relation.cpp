#include <algorithm>
#include <atomic>
#include <limits>
#include <locale>
#include <sstream>
#include <vice/world/world.hpp>
namespace vice::world {
static_assert(max_world_relation_payloads == max_world_component_payloads);
bool World::advance_relation_revision() noexcept {
    static std::atomic<std::uint64_t> next{1};
    auto n = next.load(std::memory_order_relaxed);
    for (;;) {
        if (n == std::numeric_limits<std::uint64_t>::max())
            return false;
        if (next.compare_exchange_weak(n, n + 1, std::memory_order_relaxed)) {
            relation_revision_ = n;
            return true;
        }
    }
}
std::optional<RelationKey> World::relation_key(Relation r) const noexcept {
    const auto subject = persistent_id(r.subject), object = persistent_id(r.object);
    if (!subject || !object || r.kind.is_nil())
        return {};
    return RelationKey{*subject, r.kind, *object};
}
bool World::has_relation(RelationKey key) const noexcept { return relation_index_.contains(key); }
bool World::add_relation(RelationKey key) {
    const auto s = find(key.subject), o = find(key.object);
    return s && o && add_relation(Relation{*s, key.kind, *o});
}
bool World::add_relation(Relation r) {
    const auto key = relation_key(r);
    if (!key || relations_.size() >= max_world_relations || relation_index_.contains(*key) ||
        !advance_relation_revision())
        return false;
    relation_index_.emplace(*key, relations_.size());
    relations_.push_back(r);
    relation_endpoints_[key->subject].insert(*key);
    relation_endpoints_[key->object].insert(*key);
    return true;
}
bool World::remove_relation(const Relation &r) {
    const auto key = relation_key(r);
    return key && remove_relation(*key);
}
bool World::remove_relation(RelationKey key) {
    if (!has_relation(key) || !advance_relation_revision())
        return false;
    remove_relation_record(key);
    return true;
}
void World::remove_relation_record(RelationKey key) {
    const auto state = relation_states_.find(key);
    if (state != relation_states_.end()) {
        clear_payloads(state->second, relation_payload_count_);
        relation_states_.erase(state);
    }
    const auto position = relation_index_.at(key);
    const auto last = relations_.size() - 1;
    if (position != last) {
        const auto moved = *relation_key(relations_.back());
        relations_[position] = relations_.back();
        relation_index_.at(moved) = position;
    }
    relations_.pop_back();
    relation_index_.erase(key);
    for (const auto endpoint : {key.subject, key.object}) {
        const auto entry = relation_endpoints_.find(endpoint);
        if (entry == relation_endpoints_.end())
            continue;
        entry->second.erase(key);
        if (entry->second.empty())
            relation_endpoints_.erase(entry);
    }
}
bool World::remove_entity_relations(PersistentId endpoint) {
    auto entry = relation_endpoints_.find(endpoint);
    if (entry == relation_endpoints_.end())
        return true;
    if (!advance_relation_revision())
        return false;
    while ((entry = relation_endpoints_.find(endpoint)) != relation_endpoints_.end())
        remove_relation_record(*entry->second.begin());
    return true;
}
std::span<const Relation> World::relations() const noexcept { return relations_; }
std::vector<RelationKey> World::relations_for(PersistentId endpoint, RelationDirection direction,
                                              PersistentId kind) const {
    std::vector<RelationKey> out;
    if (direction != RelationDirection::outgoing && direction != RelationDirection::incoming &&
        direction != RelationDirection::both)
        return out;
    const auto entry = relation_endpoints_.find(endpoint);
    if (entry == relation_endpoints_.end())
        return out;
    for (const auto &key : entry->second)
        if ((kind.is_nil() || key.kind == kind) &&
            ((direction != RelationDirection::incoming && key.subject == endpoint) ||
             (direction != RelationDirection::outgoing && key.object == endpoint)))
            out.push_back(key);

    return out;
}
std::optional<RelationPageSlice>
World::relation_page(PersistentId endpoint, RelationDirection direction, PersistentId kind,
                     std::optional<RelationKey> after, std::size_t limit) const {
    if (!find(endpoint) || !limit || limit > 256 || (after && !after->valid()) ||
        (direction != RelationDirection::outgoing && direction != RelationDirection::incoming &&
         direction != RelationDirection::both))
        return {};
    RelationPageSlice page;
    page.after = after;
    const auto entry = relation_endpoints_.find(endpoint);
    if (entry == relation_endpoints_.end())
        return page;
    auto it = after ? entry->second.upper_bound(*after) : entry->second.begin();
    std::size_t examined = 0;
    while (it != entry->second.end() && page.keys.size() < limit && examined++ < 256) {
        const auto key = *it++;
        page.after = key;
        if ((kind.is_nil() || key.kind == kind) &&
            ((direction != RelationDirection::incoming && key.subject == endpoint) ||
             (direction != RelationDirection::outgoing && key.object == endpoint)))
            page.keys.push_back(key);
    }
    page.has_more = it != entry->second.end();
    return page;
}
bool World::set_relation_component(RelationKey key, ComponentPayload payload) {
    if (!has_relation(key))
        return false;
    const auto old = relation_states_.find(key);
    const std::vector<ComponentPayload> empty;
    if (!validate_payload_upsert(old == relation_states_.end() ? empty : old->second, payload,
                                 relation_payload_count_) ||
        !advance_relation_revision())
        return false;
    return upsert_payload(relation_states_[key], std::move(payload), relation_payload_count_);
}
bool World::remove_relation_component(RelationKey key, ComponentTypeId type) {
    const auto old = relation_states_.find(key);
    if (old == relation_states_.end())
        return false;
    if (std::none_of(old->second.begin(), old->second.end(),
                     [&](const auto &p) { return p.type == type; }) ||
        !advance_relation_revision())
        return false;
    if (!erase_payload(old->second, type, relation_payload_count_))
        return false;
    if (old->second.empty())
        relation_states_.erase(old);
    return true;
}
std::optional<ComponentPayload> World::relation_component(RelationKey key,
                                                          ComponentTypeId type) const {
    const auto entry = relation_states_.find(key);
    if (entry == relation_states_.end())
        return {};
    const auto &data = entry->second;
    const auto found =
        std::lower_bound(data.begin(), data.end(), type, [](const auto &a, ComponentTypeId b) {
            return a.type.value.bytes() < b.value.bytes();
        });
    return found != data.end() && found->type == type ? std::optional{*found} : std::nullopt;
}
std::vector<ComponentPayload> World::relation_components(RelationKey key) const {
    const auto entry = relation_states_.find(key);
    return entry == relation_states_.end() ? std::vector<ComponentPayload>{} : entry->second;
}
std::string serialize_relation_states(const std::vector<RelationState> &records) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << '[';
    bool comma = false;
    for (const auto &r : records) {
        if (comma)
            out << ',';
        comma = true;
        out << "{\"subject\":\"" << r.key.subject.to_string() << "\",\"kind\":\""
            << r.key.kind.to_string() << "\",\"object\":\"" << r.key.object.to_string()
            << "\",\"component_payloads\":" << serialize_component_payloads(r.components) << '}';
    }
    out << ']';
    return out.str();
}
} // namespace vice::world
