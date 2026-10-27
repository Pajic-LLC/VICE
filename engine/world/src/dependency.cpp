#include <vice/world/world.hpp>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_set>
namespace vice::world {
bool validate_dependencies(const WorldSnapshot& snapshot, std::string* diagnostics) {
    if (diagnostics) diagnostics->clear();
    if (std::none_of(snapshot.entities.begin(), snapshot.entities.end(), [](const auto& e) { return e.dependency.has_value(); })) return true;
    std::unordered_map<PersistentId, const SnapshotEntity*, PersistentIdHash> entities;
    for (const auto& e : snapshot.entities) entities.emplace(e.persistent_id, &e);
    for (const auto& owner : snapshot.entities) {
        if (!owner.dependency) continue;
        const auto fail = [&](const char* reason) {
            if (diagnostics) *diagnostics = "Dependency " + owner.persistent_id.to_string() + ": " + reason;
            return false;
        };
        const auto& d = *owner.dependency;
        if (d.provider == owner.persistent_id || d.gate == owner.persistent_id) return fail("self reference is not allowed.");
        if (!d.provider.is_nil()) {
            const auto provider = entities.find(d.provider);
            if (provider == entities.end() || !provider->second->dependency) return fail("provider requires a dependency component.");
        }
        if (!d.gate.is_nil()) {
            const auto gate = entities.find(d.gate);
            if (gate == entities.end() || !gate->second->distance_link) return fail("gate requires a distance link component.");
        }
        std::unordered_set<PersistentId, PersistentIdHash> visited;
        auto current = owner.persistent_id;
        while (!current.is_nil()) {
            if (!visited.insert(current).second) return fail("provider cycle is not allowed.");
            if (visited.size() > max_dependency_depth) return fail("provider chain exceeds 256 nodes.");
            const auto found = entities.find(current);
            if (found == entities.end() || !found->second->dependency) return fail("provider requires a dependency component.");
            current = found->second->dependency->provider;
        }
    }
    return true;
}
void World::remove_dependency_references(EntityId entity) {
    const auto& d = slots_[entity.index].dependency;
    if (!d) return;
    const auto remove = [entity](auto& users, PersistentId id) {
        const auto found = users.find(id);
        if (found == users.end()) return;
        std::erase(found->second, entity);
        if (found->second.empty()) users.erase(found);
    };
    remove(dependency_provider_users_, d->provider);
    remove(dependency_gate_users_, d->gate);
}
void World::install_dependency(EntityId entity, const Dependency& d) {
    remove_dependency_references(entity);
    slots_[entity.index].dependency = d;
    if (!d.provider.is_nil()) dependency_provider_users_[d.provider].push_back(entity);
    if (!d.gate.is_nil()) dependency_gate_users_[d.gate].push_back(entity);
}
bool World::set_dependency(EntityId entity, const Dependency& d) {
    if (!alive(entity)) return false;
    if (slots_[entity.index].dependency == d) return true;
    auto candidate = snapshot();
    const auto id = slots_[entity.index].persistent_id;
    for (auto& e : candidate.entities) if (e.persistent_id == id) e.dependency = d;
    if (!validate_dependencies(candidate)) return false;
    auto before=observe_causal_dependents(entity,true);
    if(!causal_room(before.size()+1)) return false;
    install_dependency(entity, d);
    CausalEvent event;event.kind=CausalKind::dependency_configured;event.origin=CausalOrigin::authoring;
    event.subject=id;event.first=d.provider;event.second=d.gate;event.value=d.enabled;event.region=causal_region(entity);
    finish_causal_change(std::move(before),event);
    return true;
}
bool World::clear_dependency(EntityId entity) {
    if (!alive(entity) || dependency_provider_users_.contains(slots_[entity.index].persistent_id)) return false;
    const auto previous=dependency(entity);
    if(!previous) return true;
    auto before=observe_causal_dependents(entity,true);
    if(!causal_room(before.size()+1)) return false;
    CausalEvent event;event.kind=CausalKind::dependency_removed;event.origin=CausalOrigin::authoring;event.subject=*persistent_id(entity);
    event.first=previous->provider;event.second=previous->gate;event.region=causal_region(entity);
    remove_dependency_references(entity);
    slots_[entity.index].dependency.reset();
    finish_causal_change(std::move(before),event);
    return true;
}
std::optional<Dependency> World::dependency(EntityId entity) const noexcept {
    return alive(entity) ? slots_[entity.index].dependency : std::nullopt;
}
bool World::dependency_available(EntityId entity) const noexcept {
    if (!alive(entity)) return false;
    auto current = entity;
    for (std::size_t depth = 0; depth < max_dependency_depth; ++depth) {
        const auto d = dependency(current);
        if (!d) return depth == 0;
        if (!d->enabled) return false;
        if (!d->gate.is_nil()) {
            const auto gate = find(d->gate);
            const auto link = gate ? distance_link(*gate) : std::nullopt;
            if (!link || link->broken) return false;
        }
        if (d->provider.is_nil()) return true;
        const auto next = find(d->provider);
        if (!next) return false;
        current = *next;
    }
    return false;
}
std::optional<DependencyState> World::dependency_state(EntityId entity) const {
    const auto initial = dependency(entity);
    if (!initial) return std::nullopt;
    DependencyState result; result.dependency = *initial;
    auto current = entity;
    for (std::size_t depth = 0; depth < max_dependency_depth; ++depth) {
        const auto d = dependency(current);
        if (!d) return std::nullopt;
        result.path.push_back(*persistent_id(current));
        const auto failed = [&](DependencyReason reason, PersistentId cause) {
            result.available = false; result.root_reason = reason; result.root_cause = cause;
            result.reason = current == entity ? reason : DependencyReason::upstream_unavailable;
        };
        if (!d->enabled) { failed(DependencyReason::disabled, result.path.back()); return result; }
        if (!d->gate.is_nil()) {
            const auto gate = find(d->gate);
            const auto link = gate ? distance_link(*gate) : std::nullopt;
            if (!link) return std::nullopt;
            if (link->broken) {
                failed(DependencyReason::link_broken, d->gate);
                result.path.push_back(d->gate);
                result.break_impulse = link->last_break_impulse; result.break_step = link->break_step;
                return result;
            }
        }
        if (d->provider.is_nil()) return result;
        const auto next = find(d->provider);
        if (!next) return std::nullopt;
        current = *next;
    }
    return std::nullopt;
}
std::string serialize_dependency_state(const std::optional<DependencyState>& state) {
    if (!state) return "null";
    const auto id = [](PersistentId value) { return value.is_nil() ? std::string{"null"} : "\"" + value.to_string() + "\""; };
    const auto& s = *state;
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "{\"provider\":" << id(s.dependency.provider) << ",\"gate\":" << id(s.dependency.gate)
        << ",\"enabled\":" << (s.dependency.enabled ? "true" : "false")
        << ",\"available\":" << (s.available ? "true" : "false")
        << ",\"reason\":\"" << dependency_reason_name(s.reason) << "\",\"root_reason\":\"" << dependency_reason_name(s.root_reason)
        << "\",\"root_cause\":" << id(s.root_cause) << ",\"break_impulse\":" << s.break_impulse
        << ",\"break_step\":" << s.break_step << ",\"path\":[";
    for (std::size_t i = 0; i < s.path.size(); ++i) { if (i) out << ','; out << id(s.path[i]); }
    out << "]}"; return out.str();
}
}
