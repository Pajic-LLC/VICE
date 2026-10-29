#include <algorithm>
#include <charconv>
#include <limits>
#include <locale>
#include <sstream>
#include <vice/world/world.hpp>
namespace vice::world {
namespace {
template <class T> bool number(std::istream &in, T &out) {
    std::string text;
    if (!(in >> text))
        return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
bool identifier(std::istream &in, PersistentId &id, bool optional = false) {
    std::string text;
    if (!(in >> text))
        return false;
    if (optional && text == "none") {
        id = {};
        return true;
    }
    const auto parsed = PersistentId::parse(text);
    if (!parsed || parsed->is_nil())
        return false;
    id = *parsed;
    return true;
}
const char *failure_name(ScheduleFailure f) {
    switch (f) {
    case ScheduleFailure::none:
        return "none";
    case ScheduleFailure::event_rejected:
        return "event_rejected";
    case ScheduleFailure::time_overflow:
        return "time_overflow";
    case ScheduleFailure::occurrence_overflow:
        return "occurrence_overflow";
    }
    return "invalid";
}
const char *result_name(WorldEventResult r) {
    switch (r) {
    case WorldEventResult::success:
        return "success";
    case WorldEventResult::invalid_argument:
        return "invalid_argument";
    case WorldEventResult::missing_entity:
        return "missing_entity";
    case WorldEventResult::missing_schema:
        return "missing_schema";
    case WorldEventResult::schema_conflict:
        return "schema_conflict";
    case WorldEventResult::history_mismatch:
        return "history_mismatch";
    case WorldEventResult::parent_pruned:
        return "parent_pruned";
    case WorldEventResult::invalid_parent:
        return "invalid_parent";
    case WorldEventResult::sequence_exhausted:
        return "sequence_exhausted";
    }
    return "invalid";
}
std::string identifier(PersistentId id) { return id.is_nil() ? "none" : id.to_string(); }
} // namespace
ScheduleCreation World::schedule_event(WorldEventRequest event, std::uint64_t due,
                                       std::uint64_t interval, ScheduleCatchUp catch_up,
                                       PersistentId id) {
    WorldSchedule value;
    value.id = id.is_nil() ? PersistentId::random() : id;
    value.due_ticks = due;
    value.interval_ticks = interval;
    value.catch_up = catch_up;
    value.event = std::move(event);
    return install_schedule(std::move(value), false);
}
ScheduleCreation World::install_schedule(WorldSchedule s, bool restoring) {
    const auto fail = [](ScheduleResult result,
                         WorldEventResult reason = WorldEventResult::success) {
        return ScheduleCreation{result, reason, {}};
    };
    if (s.id.is_nil() || static_cast<unsigned>(s.catch_up) > 1 ||
        static_cast<unsigned>(s.failure) > 3 ||
        static_cast<unsigned>(s.event_result) >
            static_cast<unsigned>(WorldEventResult::sequence_exhausted) ||
        s.event.data.schedule || (!s.interval_ticks && s.occurrence))
        return fail(ScheduleResult::invalid_argument);
    if (!restoring && (s.occurrence || s.failure != ScheduleFailure::none ||
                       s.event_result != WorldEventResult::success))
        return fail(ScheduleResult::invalid_argument);
    if ((s.failure == ScheduleFailure::event_rejected) ==
        (s.event_result == WorldEventResult::success))
        return fail(ScheduleResult::invalid_argument);
    if (s.failure == ScheduleFailure::occurrence_overflow && !s.interval_ticks)
        return fail(ScheduleResult::invalid_argument);
    if (s.failure == ScheduleFailure::time_overflow &&
        (!s.interval_ticks || !s.occurrence ||
         s.interval_ticks <= std::numeric_limits<std::uint64_t>::max() - s.due_ticks))
        return fail(ScheduleResult::invalid_argument);
    if (schedules_.contains(s.id))
        return fail(ScheduleResult::duplicate_id);
    if (schedules_.size() >= world_schedule_capacity)
        return fail(ScheduleResult::capacity_exceeded);
    const auto result = validate_event_request(s.event, !restoring, restoring);
    if (result != WorldEventResult::success)
        return fail(ScheduleResult::event_rejected, result);
    auto &fields = s.event.data.payload.fields;
    std::sort(fields.begin(), fields.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    for (auto &f : fields)
        if (auto d = std::get_if<double>(&f.value); d && *d == 0)
            *d = 0;
    auto &parents = s.event.data.parents;
    std::sort(parents.begin(), parents.end(),
              [](const auto &a, const auto &b) { return a.sequence < b.sequence; });
    const auto id = s.id;
    if (s.failure == ScheduleFailure::none)
        schedule_due_.insert({s.due_ticks, id});
    ++component_users_[s.event.data.payload.type];
    schedules_.emplace(id, std::move(s));
    return {ScheduleResult::success, WorldEventResult::success, id};
}
bool World::cancel_schedule(PersistentId id) {
    const auto found = schedules_.find(id);
    if (found == schedules_.end())
        return false;
    const auto &s = found->second;
    schedule_due_.erase({s.due_ticks, id});
    const auto use = component_users_.find(s.event.data.payload.type);
    if (use != component_users_.end() && --use->second == 0)
        component_users_.erase(use);
    schedules_.erase(found);
    return true;
}
std::optional<WorldSchedule> World::schedule(PersistentId id) const {
    const auto found = schedules_.find(id);
    return found == schedules_.end() ? std::nullopt : std::optional{found->second};
}
SchedulerInfo World::scheduler_info() const noexcept {
    SchedulerInfo out;
    out.count = schedules_.size();
    out.pending = schedule_due_.size();
    out.failed = out.count - out.pending;
    if (!schedule_due_.empty()) {
        out.next_due = schedule_due_.begin()->first;
        out.due_remaining = *out.next_due <= clock_.ticks;
    }
    return out;
}
SchedulePage World::schedule_page(PersistentId after, std::size_t limit) const {
    SchedulePage out;
    limit = std::clamp(limit, std::size_t{1}, world_schedule_query_limit);
    auto i = after.is_nil() ? schedules_.begin() : schedules_.upper_bound(after);
    for (; i != schedules_.end() && out.entries.size() < limit; ++i)
        out.entries.push_back(i->second);
    out.has_more = i != schedules_.end();
    return out;
}
ScheduleContext World::schedule_context(PersistentId endpoint, std::size_t budget) const {
    ScheduleContext out;
    out.info = scheduler_info();
    budget = std::clamp(budget, std::size_t{1}, world_schedule_query_limit);
    for (const auto &[id, s] : schedules_) {
        (void)id;
        if (!endpoint.is_nil() && s.event.subject != endpoint && s.event.target != endpoint &&
            s.event.object != endpoint)
            continue;
        if (out.entries.size() == budget) {
            out.truncated = true;
            break;
        }
        out.entries.push_back(s);
    }
    return out;
}
ScheduleDispatch World::dispatch_schedules(std::size_t budget) {
    ScheduleDispatch out;
    budget = std::min(budget, world_schedule_dispatch_budget);
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    while (out.processed < budget && !schedule_due_.empty() &&
           schedule_due_.begin()->first <= clock_.ticks) {
        const auto key = *schedule_due_.begin();
        schedule_due_.erase(schedule_due_.begin());
        auto &s = schedules_.find(key.second)->second;
        ++out.processed;
        std::uint64_t count = 1;
        if (s.interval_ticks && s.catch_up == ScheduleCatchUp::coalesce) {
            const auto skipped = (clock_.ticks - s.due_ticks) / s.interval_ticks;
            if (skipped == maximum) {
                s.failure = ScheduleFailure::occurrence_overflow;
                ++out.failed;
                continue;
            }
            count = skipped + 1;
        }
        if (count > maximum - s.occurrence) {
            s.failure = ScheduleFailure::occurrence_overflow;
            ++out.failed;
            continue;
        }
        const auto due = s.due_ticks + (count - 1) * s.interval_ticks;
        auto event = s.event;
        event.data.schedule = ScheduleProvenance{s.id, due, s.occurrence + count, count};
        const auto result = emit_world_event_impl(std::move(event), CausalOrigin::gameplay, true);
        if (!result) {
            s.failure = ScheduleFailure::event_rejected;
            s.event_result = result.result;
            ++out.failed;
            continue;
        }
        ++out.fired;
        s.occurrence += count;
        if (!s.interval_ticks) {
            cancel_schedule(s.id);
            continue;
        }
        if (s.interval_ticks > maximum - due) {
            s.due_ticks = due;
            s.failure = ScheduleFailure::time_overflow;
            ++out.failed;
            continue;
        }
        s.due_ticks = due + s.interval_ticks;
        schedule_due_.insert({s.due_ticks, s.id});
    }
    out.due_remaining = scheduler_info().due_remaining;
    return out;
}
bool read_world_schedule(std::istream &in, WorldSchedule &out) {
    WorldSchedule s;
    unsigned catch_up = 0, failure = 0, result = 0, retained = 0;
    if (!identifier(in, s.id) || !number(in, s.due_ticks) || !number(in, s.interval_ticks) ||
        !number(in, s.occurrence) || !number(in, catch_up) || !number(in, failure) ||
        !number(in, result) || !number(in, retained) || retained > 1 ||
        !identifier(in, s.event.subject) || !identifier(in, s.event.target, true) ||
        !identifier(in, s.event.object, true) || !number(in, s.event.region.value) ||
        !read_world_event_data(in, s.event.data))
        return false;
    s.catch_up = static_cast<ScheduleCatchUp>(catch_up);
    s.failure = static_cast<ScheduleFailure>(failure);
    s.event_result = static_cast<WorldEventResult>(result);
    s.event.retained = retained != 0;
    out = std::move(s);
    return true;
}
void write_world_schedule(std::ostream &out, const WorldSchedule &s) {
    out.imbue(std::locale::classic());
    out << s.id.to_string() << ' ' << s.due_ticks << ' ' << s.interval_ticks << ' ' << s.occurrence
        << ' ' << static_cast<unsigned>(s.catch_up) << ' ' << static_cast<unsigned>(s.failure)
        << ' ' << static_cast<unsigned>(s.event_result) << ' ' << (s.event.retained ? 1 : 0) << ' '
        << s.event.subject.to_string() << ' ' << identifier(s.event.target) << ' '
        << identifier(s.event.object) << ' ' << s.event.region.value << ' ';
    write_world_event_data(out, s.event.data);
}
std::string serialize_scheduler_info(const SchedulerInfo &s) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"count\":" << s.count << ",\"pending\":" << s.pending << ",\"failed\":" << s.failed
        << ",\"capacity\":" << world_schedule_capacity
        << ",\"dispatch_budget\":" << world_schedule_dispatch_budget
        << ",\"next_due\":" << (s.next_due ? "\"" + std::to_string(*s.next_due) + "\"" : "null")
        << ",\"due_remaining\":" << (s.due_remaining ? "true" : "false") << '}';
    return out.str();
}
std::string serialize_world_schedule(const WorldSchedule &s) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"id\":\"" << s.id.to_string() << "\",\"due_ticks\":\"" << s.due_ticks
        << "\",\"interval_ticks\":\"" << s.interval_ticks << "\",\"occurrence\":\"" << s.occurrence
        << "\",\"catch_up\":\"" << (s.catch_up == ScheduleCatchUp::replay ? "replay" : "coalesce")
        << "\",\"state\":\"" << (s.failure == ScheduleFailure::none ? "pending" : "failed")
        << "\",\"failure\":" << static_cast<unsigned>(s.failure) << ",\"failure_name\":\""
        << failure_name(s.failure)
        << "\",\"event_result\":" << static_cast<unsigned>(s.event_result)
        << ",\"event_result_name\":\"" << result_name(s.event_result) << "\"" << ",\"subject\":\""
        << s.event.subject.to_string() << "\",\"target\":\"" << s.event.target.to_string()
        << "\",\"object\":\"" << s.event.object.to_string()
        << "\",\"region\":" << s.event.region.value
        << ",\"retained\":" << (s.event.retained ? "true" : "false")
        << ",\"payload\":" << serialize_component_payloads({s.event.data.payload})
        << ",\"parents\":[";
    for (std::size_t i = 0; i < s.event.data.parents.size(); ++i) {
        if (i)
            out << ',';
        const auto &p = s.event.data.parents[i];
        out << "{\"history\":\"" << p.history.to_string() << "\",\"id\":\"" << p.id.to_string()
            << "\",\"sequence\":" << p.sequence << '}';
    }
    out << "]}";
    return out.str();
}
std::string serialize_schedule_context(const ScheduleContext &c) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"summary\":" << serialize_scheduler_info(c.info)
        << ",\"truncated\":" << (c.truncated ? "true" : "false") << ",\"entries\":[";
    for (std::size_t i = 0; i < c.entries.size(); ++i) {
        if (i)
            out << ',';
        out << serialize_world_schedule(c.entries[i]);
    }
    out << "]}";
    return out.str();
}
} // namespace vice::world
