#include <algorithm>
#include <charconv>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_set>
#include <vice/world/world.hpp>
namespace vice::world {
namespace {
template <class T> bool number(std::istream &in, T &n) {
    std::string s;
    if (!(in >> s))
        return false;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}
bool read_id(std::istream &in, PersistentId &id) {
    std::string s;
    if (!(in >> s))
        return false;
    const auto p = PersistentId::parse(s);
    if (!p || p->is_nil())
        return false;
    id = *p;
    return true;
}
} // namespace
bool read_world_event_data(std::istream &in, WorldEventData &data) {
    WorldEventData next;
    std::uint32_t count = 0;
    if (!read_component_payload(in, next.payload) || !number(in, count) ||
        count > world_event_parent_capacity)
        return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        EventRef ref;
        if (!read_id(in, ref.history) || !read_id(in, ref.id) || !number(in, ref.sequence))
            return false;
        next.parents.push_back(ref);
    }
    in >> std::ws;
    if (in.peek() == 's') {
        std::string marker;
        ScheduleProvenance schedule;
        if (!(in >> marker) || marker != "scheduled" || !read_id(in, schedule.id) ||
            !number(in, schedule.due_ticks) || !number(in, schedule.occurrence) ||
            !number(in, schedule.count))
            return false;
        next.schedule = schedule;
    }
    data = std::move(next);
    return true;
}
void write_world_event_data(std::ostream &out, const WorldEventData &data) {
    write_component_payload(out, data.payload);
    out << ' ' << data.parents.size();
    for (const auto &p : data.parents)
        out << ' ' << p.history.to_string() << ' ' << p.id.to_string() << ' ' << p.sequence;
    if (data.schedule) {
        const auto &s = *data.schedule;
        out << " scheduled " << s.id.to_string() << ' ' << s.due_ticks << ' ' << s.occurrence << ' '
            << s.count;
    }
}
void World::release_event_schema(const CausalEvent &e) {
    if (e.data) {
        if (e.data->payload.type == quantity_receipt_type()) {
            const auto &fs = e.data->payload.fields;
            const auto found =
                std::find_if(fs.begin(), fs.end(), [](const auto &f) { return f.id == 1; });
            if (found != fs.end())
                if (const auto *id = std::get_if<PersistentId>(&found->value))
                    release_quantity_type({*id});
        }
        const auto type = e.data->payload.type;
        const auto found = component_users_.find(type);
        if (found != component_users_.end() && --found->second == 0)
            component_users_.erase(found);
    }
}
void World::prune_world_signals() {
    while (!signals_.empty() && clock_.ticks >= signals_.front().world_ticks &&
           clock_.ticks - signals_.front().world_ticks >= world_signal_lifetime_ticks) {
        release_event_schema(signals_.front());
        signals_.pop_front();
    }
}
void World::clear_world_signals() {
    for (const auto &s : signals_)
        release_event_schema(s);
    signals_.clear();
}
SignalInfo World::signal_info() const noexcept {
    return {signal_stream_, next_signal_sequence_ - static_cast<std::uint64_t>(signals_.size()),
            next_signal_sequence_, signals_.size()};
}
std::optional<CausalEvent> World::world_signal(std::uint64_t sequence) const {
    const auto first = next_signal_sequence_ - static_cast<std::uint64_t>(signals_.size());
    if (sequence < first || sequence >= next_signal_sequence_)
        return {};
    return signals_[static_cast<std::size_t>(sequence - first)];
}
SignalContext World::signal_context(PersistentId subject, std::size_t budget) const {
    SignalContext out;
    out.info = signal_info();
    out.truncated = out.info.first_sequence > 1;
    out.causal_first_sequence = causal_info().first_sequence;
    budget = std::clamp(budget, std::size_t{1}, causal_query_max_events);
    for (auto i = signals_.rbegin(); i != signals_.rend(); ++i)
        if (subject.is_nil() || i->subject == subject || i->first == subject ||
            i->second == subject) {
            if (out.events.size() == budget) {
                out.truncated = true;
                break;
            }
            out.events.push_back(*i);
        }
    std::reverse(out.events.begin(), out.events.end());
    return out;
}
WorldEventResult World::validate_event_request(const WorldEventRequest &r, bool live,
                                               bool allow_pruned) const {
    using R = WorldEventResult;
    if (r.subject.is_nil() || r.data.parents.size() > world_event_parent_capacity)
        return R::invalid_argument;
    if (live && (!find(r.subject) || (!r.target.is_nil() && !find(r.target)) ||
                 (!r.object.is_nil() && !find(r.object))))
        return R::missing_entity;
    if (live && r.region.valid() && !region(r.region))
        return R::invalid_argument;
    const auto type = component_types_.find(r.data.payload.type);
    if (type == component_types_.end())
        return R::missing_schema;
    if (type->second.version != r.data.payload.version)
        return R::schema_conflict;
    if (!valid_component_payload(r.data.payload, type->second))
        return R::invalid_argument;
    const auto info = causal_info();
    auto parents = r.data.parents;
    std::sort(parents.begin(), parents.end(),
              [](const auto &a, const auto &b) { return a.sequence < b.sequence; });
    std::unordered_set<PersistentId, PersistentIdHash> seen;
    std::uint64_t previous = 0;
    for (const auto &p : parents) {
        if (p.history != info.id || info.id.is_nil())
            return R::history_mismatch;
        if (!p.sequence || p.id.is_nil() || p.sequence <= previous ||
            p.sequence >= info.next_sequence || !seen.insert(p.id).second)
            return R::invalid_parent;
        previous = p.sequence;
        if (p.sequence < info.first_sequence) {
            if (!allow_pruned)
                return R::parent_pruned;
            continue;
        }
        const auto e = causal_event(p.sequence);
        if (!e || e->id != p.id)
            return R::invalid_parent;
    }
    return R::success;
}
WorldEventEmission World::emit_world_event(WorldEventRequest r, CausalOrigin origin) {
    return emit_world_event_impl(std::move(r), origin, false);
}
WorldEventEmission World::emit_world_event_impl(WorldEventRequest r, CausalOrigin origin,
                                                bool scheduled, bool quantity_receipt) {
    const auto fail = [](WorldEventResult code) { return WorldEventEmission{code, {}}; };
    if (static_cast<unsigned>(origin) > 4 || (!scheduled && r.data.schedule) ||
        (!quantity_receipt && r.data.payload.type == quantity_receipt_type()))
        return fail(WorldEventResult::invalid_argument);
    const auto validation = validate_event_request(r, true, scheduled);
    if (validation != WorldEventResult::success)
        return fail(validation);
    std::sort(r.data.payload.fields.begin(), r.data.payload.fields.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    for (auto &f : r.data.payload.fields)
        if (auto n = std::get_if<double>(&f.value); n && *n == 0)
            *n = 0;
    std::sort(r.data.parents.begin(), r.data.parents.end(),
              [](const auto &a, const auto &b) { return a.sequence < b.sequence; });
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (r.retained) {
        const auto needed = capturing_causality_ ? 1U : 2U;
        if (needed > maximum - next_causal_sequence_)
            return fail(WorldEventResult::sequence_exhausted);
    } else if (next_signal_sequence_ == maximum)
        return fail(WorldEventResult::sequence_exhausted);
    CausalEvent event;
    event.kind = CausalKind::world_event;
    event.origin = origin;
    event.subject = r.subject;
    event.first = r.target;
    event.second = r.object;
    event.region = r.region.valid() ? r.region : causal_region(*find(r.subject));
    event.world_ticks = clock_.ticks;
    event.data = std::move(r.data);
    CausalKey parent;
    if (!event.data->parents.empty()) {
        parent = {event.data->parents[0].sequence, event.data->parents[0].id};
    }
    if (r.retained) {
        if (!capturing_causality_ && !begin_causal_recording())
            return fail(WorldEventResult::sequence_exhausted);
        const auto ref = append_causal(std::move(event), parent);
        return {WorldEventResult::success, {causal_id_, ref.id, ref.sequence}};
    }
    if (signal_stream_.is_nil())
        signal_stream_ = PersistentId::random();
    event.id = PersistentId::random();
    event.sequence = next_signal_sequence_++;
    event.parent_id = parent.id;
    event.parent_sequence = parent.sequence;
    const auto ref = EventRef{signal_stream_, event.id, event.sequence};
    ++component_users_[event.data->payload.type];
    signals_.push_back(std::move(event));
    if (signals_.size() > world_signal_capacity) {
        release_event_schema(signals_.front());
        signals_.pop_front();
    }
    return {WorldEventResult::success, ref};
}
std::string serialize_signal_context(const SignalContext &c) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"stream\":\"" << c.info.stream.to_string()
        << "\",\"first_sequence\":" << c.info.first_sequence
        << ",\"next_sequence\":" << c.info.next_sequence
        << ",\"retained_count\":" << c.info.retained_count
        << ",\"capacity\":" << world_signal_capacity
        << ",\"lifetime_ticks\":" << world_signal_lifetime_ticks
        << ",\"truncated\":" << (c.truncated ? "true" : "false") << ",\"events\":[";
    for (std::size_t i = 0; i < c.events.size(); ++i) {
        if (i)
            out << ',';
        const auto &e = c.events[i];
        out << "{\"id\":\"" << e.id.to_string() << "\",\"sequence\":" << e.sequence
            << ",\"world_ticks\":\"" << e.world_ticks << "\",\"subject\":\""
            << e.subject.to_string() << "\",\"target\":\"" << e.first.to_string()
            << "\",\"object\":\"" << e.second.to_string() << "\",\"origin\":\""
            << causal_origin_name(e.origin) << "\",\"region\":" << e.region.value
            << ",\"payload\":" << serialize_component_payloads({e.data->payload})
            << ",\"parents\":[";
        for (std::size_t j = 0; j < e.data->parents.size(); ++j) {
            if (j)
                out << ',';
            const auto &p = e.data->parents[j];
            out << "{\"history\":\"" << p.history.to_string() << "\",\"id\":\"" << p.id.to_string()
                << "\",\"sequence\":" << p.sequence << ",\"state\":\""
                << (p.sequence < c.causal_first_sequence ? "pruned" : "retained") << "\"}";
        }
        out << "]";
        if (e.data->schedule) {
            const auto &s = *e.data->schedule;
            out << ",\"schedule\":{\"id\":\"" << s.id.to_string() << "\",\"due_ticks\":\""
                << s.due_ticks << "\",\"occurrence\":\"" << s.occurrence << "\",\"count\":\""
                << s.count << "\"}";
        } else
            out << ",\"schedule\":null";
        out << "}";
    }
    out << "]}";
    return out.str();
}
} // namespace vice::world
