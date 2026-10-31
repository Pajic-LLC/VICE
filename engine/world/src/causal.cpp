#include <vice/world/world.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <unordered_set>
namespace vice::world {
const char* causal_kind_name(CausalKind v) noexcept {
    switch(v) {
    case CausalKind::world_event:
        return "world_event";
    case CausalKind::capture_started:return "capture_started";
    case CausalKind::distance_link_changed:return "distance_link_changed";
    case CausalKind::distance_link_removed:return "distance_link_removed";
    case CausalKind::constraint_broken:return "constraint_broken";
    case CausalKind::dependency_configured:return "dependency_configured";
    case CausalKind::dependency_removed:return "dependency_removed";
    case CausalKind::dependency_enabled:return "dependency_enabled";
    case CausalKind::dependency_state_changed:return "dependency_state_changed";
    } return "invalid";
}
const char* causal_origin_name(CausalOrigin v) noexcept {
    switch(v) {
    case CausalOrigin::runtime:return "runtime"; case CausalOrigin::authoring:return "authoring";
    case CausalOrigin::physics:return "physics"; case CausalOrigin::gameplay:return "gameplay";
    case CausalOrigin::derived:return "derived";
    } return "invalid";
}
namespace {
bool valid_event(const CausalEvent& e) {
    if (e.id.is_nil() || e.capture_id.is_nil() || e.sequence == 0 ||
        e.sequence == std::numeric_limits<std::uint64_t>::max() ||
        e.parent_sequence >= e.sequence || (e.parent_sequence == 0) != e.parent_id.is_nil() ||
        e.id == e.parent_id || static_cast<unsigned>(e.kind) > 8 ||
        static_cast<unsigned>(e.origin) > 4 || static_cast<unsigned>(e.reason) > 3 ||
        static_cast<unsigned>(e.root_reason) > 3 || static_cast<unsigned>(e.quantity_kind) > 1 ||
        !std::isfinite(e.quantity) || e.quantity < 0)
        return false;
    if (e.kind == CausalKind::world_event) {
        if (!e.data || e.data->parents.size() > world_event_parent_capacity || e.subject.is_nil() ||
            !e.root_entity.is_nil() || !e.first_material.is_nil() || !e.second_material.is_nil() ||
            e.value || e.reason != DependencyReason::available ||
            e.root_reason != DependencyReason::available ||
            e.quantity_kind != CausalQuantity::none || e.quantity != 0 || e.solver_step)
            return false;
        if (e.data->schedule) {
            const auto &s = *e.data->schedule;
            if (s.id.is_nil() || !s.count || s.occurrence < s.count || s.due_ticks > e.world_ticks)
                return false;
        }
        const auto &ps = e.data->parents;
        if (ps.empty())
            return e.parent_id.is_nil() && !e.parent_sequence;
        if (e.parent_id != ps[0].id || e.parent_sequence != ps[0].sequence)
            return false;
        std::uint64_t previous = 0;
        std::unordered_set<PersistentId, PersistentIdHash> ids;
        for (const auto &p : ps) {
            if (p.history.is_nil() || p.id.is_nil() || p.id == e.id || p.sequence <= previous ||
                p.sequence >= e.sequence || !ids.insert(p.id).second)
                return false;
            previous = p.sequence;
        }
        return true;
    }
    if (e.data)
        return false;
    if(e.kind!=CausalKind::constraint_broken && (!e.first_material.is_nil() || !e.second_material.is_nil()))return false;
    if(e.kind==CausalKind::capture_started) return e.capture_id==e.id && e.origin==CausalOrigin::runtime && e.subject.is_nil() && e.first.is_nil() && e.second.is_nil() &&
        e.root_entity.is_nil() && !e.value && !e.parent_sequence && !e.region.valid() && e.reason==DependencyReason::available &&
        e.root_reason==DependencyReason::available && e.quantity_kind==CausalQuantity::none && e.quantity==0 && e.solver_step==0;
    if(e.subject.is_nil() || e.first==e.subject || e.second==e.subject) return false;
    if(e.kind==CausalKind::dependency_state_changed) {
        if(e.origin!=CausalOrigin::derived || !e.parent_sequence) return false;
        if(e.value) return e.reason==DependencyReason::available && e.root_reason==DependencyReason::available && e.root_entity.is_nil() &&
            e.quantity_kind==CausalQuantity::none && e.quantity==0 && e.solver_step==0;
        if(e.root_entity.is_nil() || (e.root_reason!=DependencyReason::disabled && e.root_reason!=DependencyReason::link_broken)) return false;
        if(e.reason==DependencyReason::disabled && (e.root_entity!=e.subject || e.root_reason!=DependencyReason::disabled)) return false;
        if(e.reason==DependencyReason::link_broken && (e.second.is_nil() || e.root_entity!=e.second || e.root_reason!=DependencyReason::link_broken)) return false;
        if(e.reason==DependencyReason::upstream_unavailable && e.first.is_nil()) return false;
        if(e.reason==DependencyReason::available) return false;
        return e.quantity_kind==CausalQuantity::none && e.quantity==0 && e.solver_step==0;
    }
    if(e.parent_sequence || !e.root_entity.is_nil() || e.reason!=DependencyReason::available || e.root_reason!=DependencyReason::available) return false;
    if(e.kind==CausalKind::constraint_broken) return e.origin==CausalOrigin::physics && !e.first.is_nil() && !e.second.is_nil() && e.first!=e.second &&
        e.value && e.quantity_kind==CausalQuantity::linear_impulse && e.quantity>0 && e.solver_step>0 && e.region.valid();
    if(e.quantity_kind!=CausalQuantity::none || e.quantity!=0 || e.solver_step!=0) return false;
    if(e.kind==CausalKind::dependency_enabled) return e.origin==CausalOrigin::gameplay;
    if(e.origin!=CausalOrigin::authoring) return false;
    if(e.kind==CausalKind::distance_link_changed || e.kind==CausalKind::distance_link_removed)
        return !e.first.is_nil() && !e.second.is_nil() && e.first!=e.second && (e.kind!=CausalKind::distance_link_removed || !e.value);
    return (e.kind==CausalKind::dependency_configured || e.kind==CausalKind::dependency_removed) && (e.kind!=CausalKind::dependency_removed || !e.value);
}
bool same_state(const DependencyState& a,const DependencyState& b) {
    return a.available==b.available && a.reason==b.reason && a.root_reason==b.root_reason && a.root_cause==b.root_cause;
}
}
bool validate_causal_history(const CausalHistory& h,std::string* diagnostics) {
    if(diagnostics) diagnostics->clear();
    const auto fail=[&](const char* message){if(diagnostics)*diagnostics=std::string{"Causal history: "}+message;return false;};
    if(h.next_sequence==0 || h.events.size()>causal_history_capacity || h.events.size()>h.next_sequence-1) return fail("invalid range or capacity.");
    if(h.id.is_nil()) return h.next_sequence==1 && h.events.empty() ? true:fail("records require a history identity.");
    if(h.events.empty() && h.next_sequence!=1) return fail("empty history has an invalid next sequence.");
    const auto first=h.next_sequence-static_cast<std::uint64_t>(h.events.size());
    std::unordered_set<PersistentId,PersistentIdHash> ids;
    for(std::size_t i=0;i<h.events.size();++i) {
        const auto& e=h.events[i];
        if ((i && e.world_ticks < h.events[i - 1].world_ticks) || !valid_event(e) ||
            e.sequence != first + i || !ids.insert(e.id).second)
            return fail("invalid event, duplicate identity or noncontiguous sequence.");
        if(e.parent_sequence>=first && e.parent_sequence && h.events[static_cast<std::size_t>(e.parent_sequence-first)].id!=e.parent_id)
            return fail("retained parent identity does not match.");
    }
    for(const auto& e:h.events) {
        if (e.data)
            for (const auto &p : e.data->parents) {
                if (p.history != h.id)
                    return fail("typed parent lineage mismatch.");
                if (p.sequence >= first &&
                    h.events[static_cast<std::size_t>(p.sequence - first)].id != p.id)
                    return fail("typed retained parent identity mismatch.");
            }

        if(ids.contains(e.capture_id)) {
            const auto marker=std::find_if(h.events.begin(),h.events.end(),[&](const auto& x){return x.id==e.capture_id;});
            if(marker->kind!=CausalKind::capture_started || marker->sequence>e.sequence) return fail("invalid capture segment reference.");
        } else if(first==1) return fail("capture segment marker is missing.");
        if(e.parent_sequence>=first && e.parent_sequence) {
            const auto& p=h.events[static_cast<std::size_t>(e.parent_sequence-first)];
            if(e.kind==CausalKind::dependency_state_changed && p.capture_id!=e.capture_id) return fail("propagation crossed capture segments.");
        }
    }
    return true;
}
bool World::causal_room(std::size_t count) const noexcept {
    return !capturing_causality_ || count<=std::numeric_limits<std::uint64_t>::max()-next_causal_sequence_;
}
World::CausalKey World::append_causal(CausalEvent event,CausalKey parent) {
    if(!capturing_causality_ || next_causal_sequence_==std::numeric_limits<std::uint64_t>::max()) return {};
    event.id=PersistentId::random(); event.capture_id=event.kind==CausalKind::capture_started?event.id:capture_id_; event.sequence=next_causal_sequence_;
    event.parent_sequence=parent.sequence; event.parent_id=parent.id;
    event.world_ticks = clock_.ticks;
    const auto ref = CausalKey{event.sequence, event.id};
    if (event.data)
        ++component_users_[event.data->payload.type];
    retain_quantity_receipt(event);
    causal_events_.push_back(std::move(event));
    ++next_causal_sequence_;
    if (causal_events_.size() > causal_history_capacity) {
        release_event_schema(causal_events_.front());
        causal_events_.pop_front();
    }
    return ref;
}
bool World::begin_causal_recording(bool new_segment) {
    if(capturing_causality_ && !new_segment) return true;
    if(next_causal_sequence_==std::numeric_limits<std::uint64_t>::max()) return false;
    if(causal_id_.is_nil()) causal_id_=PersistentId::random();
    capturing_causality_=true;
    capture_id_=append_causal(CausalEvent{},{}).id;
    return true;
}
CausalInfo World::causal_info() const noexcept {
    return {causal_id_,capture_id_,next_causal_sequence_-static_cast<std::uint64_t>(causal_events_.size()),next_causal_sequence_,causal_events_.size(),capturing_causality_};
}
CausalHistory World::causal_snapshot() const { return {causal_id_,next_causal_sequence_,{causal_events_.begin(),causal_events_.end()}}; }
std::optional<CausalEvent> World::causal_event(std::uint64_t sequence) const {
    const auto first=next_causal_sequence_-static_cast<std::uint64_t>(causal_events_.size());
    if(sequence<first || sequence>=next_causal_sequence_) return std::nullopt;
    return causal_events_[static_cast<std::size_t>(sequence-first)];
}
World::CausalBefore World::observe_causal_dependents(EntityId entity,bool include_self) const {
    CausalBefore result;
    if(!capturing_causality_) return result;
    std::vector<EntityId> queue;
    std::unordered_set<PersistentId,PersistentIdHash> visited;
    const auto add=[&](EntityId e) { if(alive(e) && visited.insert(*persistent_id(e)).second) queue.push_back(e); };
    if(include_self) add(entity);
    else {
        const auto found=dependency_gate_users_.find(*persistent_id(entity));
        if(found!=dependency_gate_users_.end()) for(const auto e:found->second) add(e);
    }
    for(std::size_t i=0;i<queue.size();++i) {
        const auto found=dependency_provider_users_.find(*persistent_id(queue[i]));
        if(found!=dependency_provider_users_.end()) for(const auto e:found->second) add(e);
    }
    for(const auto e:queue) result.emplace_back(e,dependency_state(e));
    return result;
}
RegionId World::causal_region(EntityId entity,PersistentId root) const noexcept {
    if(const auto s=spatial_state(entity)) return s->region;
    const auto target=root.is_nil() ? std::optional{entity}:find(root);
    if(target) {
        if(const auto s=spatial_state(*target)) return s->region;
        if(const auto joint=swing_twist_joint(*target)) {
            const auto first=find(joint->first);const auto s=first ? spatial_state(*first):std::nullopt;
            if(s) return s->region;
        }
        if(const auto l=distance_link(*target)) {
            const auto first=find(l->first); const auto s=first ? spatial_state(*first):std::nullopt;
            if(s) return s->region;
        }
    }
    return {};
}
void World::finish_causal_change(CausalBefore before,CausalEvent producer) {
    const auto root=append_causal(producer,{});
    if(!root.sequence) return;
    const auto depth=[&](EntityId e) { std::size_t n=0; while(n<max_dependency_depth) {const auto d=dependency(e);if(!d || d->provider.is_nil()) break;const auto p=find(d->provider);if(!p)break;e=*p;++n;}return n;};
    std::sort(before.begin(),before.end(),[&](const auto& a,const auto& b){const auto x=depth(a.first),y=depth(b.first);return x!=y ? x<y:persistent_id(a.first)->bytes()<persistent_id(b.first)->bytes();});
    std::unordered_map<PersistentId,CausalKey,PersistentIdHash> changed;
    for(const auto& [entity,old]:before) {
        const auto current=dependency_state(entity);
        if(!current || (old && same_state(*old,*current))) continue;
        const auto& s=*current;
        CausalEvent event;
        event.kind=CausalKind::dependency_state_changed;event.origin=CausalOrigin::derived;event.subject=*persistent_id(entity);
        event.first=s.dependency.provider;event.second=s.dependency.gate;event.root_entity=s.root_cause;
        event.value=s.available;event.reason=s.reason;event.root_reason=s.root_reason;
        event.region=causal_region(entity,s.root_cause);
        auto parent=root;
        if(s.available || s.reason==DependencyReason::upstream_unavailable) {
            const auto provider=changed.find(s.dependency.provider);if(provider!=changed.end())parent=provider->second;
        }
        changed[event.subject]=append_causal(event,parent);
    }
}
bool World::set_dependency_enabled(EntityId entity,bool enabled) {
    const auto old=dependency(entity);
    if(!old) return false;
    if(old->enabled==enabled) return true;
    auto before=observe_causal_dependents(entity,true);
    if(!causal_room(before.size()+1)) return false;
    slots_[entity.index].dependency->enabled=enabled;
    CausalEvent e;e.kind=CausalKind::dependency_enabled;e.origin=CausalOrigin::gameplay;e.subject=*persistent_id(entity);
    e.first=old->provider;e.second=old->gate;e.value=enabled;e.region=causal_region(entity);
    finish_causal_change(std::move(before),e);return true;
}
CausalContext World::causal_context(PersistentId subject,std::size_t latest,std::size_t budget) const {
    CausalContext result;result.info=causal_info();result.truncated=result.info.first_sequence>1;
    latest=std::clamp(latest,std::size_t{1},causal_query_max_events);budget=std::clamp(budget,std::size_t{1},causal_query_max_events);
    std::vector<std::uint64_t> roots;
    for (auto i = causal_events_.rbegin(); i != causal_events_.rend(); ++i)
        if (subject.is_nil() || i->subject == subject ||
            (i->data && (i->first == subject || i->second == subject))) {
            if (roots.size() == latest) {
                result.truncated = true;
                break;
            }
            roots.push_back(i->sequence);
        }
    std::set<std::uint64_t> selected;
    for (const auto root : roots) {
        std::vector<std::uint64_t> pending{root};
        for (std::size_t i = 0; i < pending.size(); ++i) {
            const auto sequence = pending[i];
            if (!sequence || selected.contains(sequence))
                continue;
            if (selected.size() == budget) {
                result.truncated = true;
                break;
            }
            const auto e = causal_event(sequence);
            if (!e) {
                result.truncated = true;
                continue;
            }
            selected.insert(sequence);
            if (e->data) {
                for (const auto &p : e->data->parents)
                    pending.push_back(p.sequence);
            } else if (e->parent_sequence)
                pending.push_back(e->parent_sequence);
        }
    }
    for(const auto sequence:selected) result.events.push_back(*causal_event(sequence));
    return result;
}
std::string serialize_causal_context(const CausalContext& c) {
    const auto id=[](PersistentId v){return v.is_nil()?std::string{"null"}:"\""+v.to_string()+"\"";};
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<float>::max_digits10);
    out<<"{\"history_id\":"<<id(c.info.id)<<",\"capture_id\":"<<id(c.info.capture_id)<<",\"first_sequence\":"<<c.info.first_sequence
       <<",\"next_sequence\":"<<c.info.next_sequence<<",\"retained_count\":"<<c.info.retained_count<<",\"capacity\":"<<causal_history_capacity
       <<",\"capturing\":"<<(c.info.capturing?"true":"false")<<",\"truncated\":"<<(c.truncated?"true":"false")<<",\"events\":[";
    for(std::size_t i=0;i<c.events.size();++i) {
        const auto& e=c.events[i];if(i)out<<',';
        const char* parent="none";
        if(e.parent_sequence) parent=e.parent_sequence<c.info.first_sequence?"pruned":
            std::any_of(c.events.begin(),c.events.end(),[&](const auto& p){return p.id==e.parent_id;})?"included":"retained";
        out << "{\"id\":" << id(e.id) << ",\"capture_id\":" << id(e.capture_id)
            << ",\"sequence\":" << e.sequence << ",\"parent_id\":" << id(e.parent_id)
            << ",\"parent_sequence\":" << e.parent_sequence << ",\"parent_state\":\"" << parent
            << "\",\"kind\":\"" << causal_kind_name(e.kind) << "\",\"origin\":\""
            << causal_origin_name(e.origin) << "\",\"subject\":" << id(e.subject)
            << ",\"first\":" << id(e.first) << ",\"second\":" << id(e.second)
            << ",\"root_entity\":" << id(e.root_entity)
            << ",\"first_material\":" << id(e.first_material)
            << ",\"second_material\":" << id(e.second_material)
            << ",\"value\":" << (e.value ? "true" : "false") << ",\"reason\":\""
            << dependency_reason_name(e.reason) << "\",\"root_reason\":\""
            << dependency_reason_name(e.root_reason) << "\",\"quantity_kind\":\""
            << (e.quantity_kind == CausalQuantity::linear_impulse ? "linear_impulse" : "none")
            << "\",\"quantity\":" << e.quantity << ",\"solver_step\":" << e.solver_step
            << ",\"region\":" << e.region.value << ",\"world_ticks\":\"" << e.world_ticks
            << "\",\"payload\":"
            << (e.data ? serialize_component_payloads({e.data->payload}) : "[]")
            << ",\"parents\":[";
        if (e.data)
            for (std::size_t j = 0; j < e.data->parents.size(); ++j) {
                if (j)
                    out << ',';
                const auto &p = e.data->parents[j];
                out << "{\"history\":" << id(p.history) << ",\"id\":" << id(p.id)
                    << ",\"sequence\":" << p.sequence << ",\"state\":\""
                    << (p.sequence < c.info.first_sequence ? "pruned"
                        : std::any_of(
                              c.events.begin(), c.events.end(),
                              [&](const auto &x) { return x.id == p.id; })
                            ? "included"
                            : "retained")
                    << "\"}";
            }
        out << "]";
        if (e.data && e.data->schedule) {
            const auto &s = *e.data->schedule;
            out << ",\"schedule\":{\"id\":\"" << s.id.to_string() << "\",\"due_ticks\":\""
                << s.due_ticks << "\",\"occurrence\":\"" << s.occurrence << "\",\"count\":\""
                << s.count << "\"}";
        } else
            out << ",\"schedule\":null";
        out << "}";
    } out<<"]}";return out.str();
}
}
