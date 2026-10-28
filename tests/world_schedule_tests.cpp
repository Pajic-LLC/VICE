#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>
namespace {
using namespace vice;
void require(bool ok, const char *s) {
    if (!ok) {
        std::cerr << "Scheduler: " << s << '\n';
        std::exit(1);
    }
}
world::PersistentId id(unsigned n) {
    world::PersistentId::Bytes b{};
    b[0] = 0xbc;
    b[6] = 0x40;
    b[8] = 0x80;
    b[12] = static_cast<std::uint8_t>(n >> 24);
    b[13] = static_cast<std::uint8_t>(n >> 16);
    b[14] = static_cast<std::uint8_t>(n >> 8);
    b[15] = static_cast<std::uint8_t>(n);
    return world::PersistentId{b};
}
world::WorldEventRequest event(bool retained = true) {
    world::WorldEventRequest e;
    e.subject = id(1);
    e.target = id(2);
    e.retained = retained;
    e.data.payload = {{id(100)}, 1, {{1, std::int64_t{42}}}};
    return e;
}
void populate(world::World &w) {
    require(w.create_entity(id(1)).has_value() && w.create_entity(id(2)).has_value() &&
                w.register_component_type({{id(100)},
                                           "ExampleTimedFact",
                                           1,
                                           {{1, world::ComponentValueKind::integer, "quantity"}}}),
            "populate");
}
std::vector<std::byte> bytes(const world::World &w) {
    const auto e = world::encode_world_snapshot(w.snapshot());
    require(static_cast<bool>(e), "encode");
    return e.bytes;
}
void rehash(std::vector<std::byte> &b) {
    std::uint64_t h = 14695981039346656037ULL;
    for (std::size_t i = 40; i < b.size(); ++i) {
        h ^= std::to_integer<unsigned char>(b[i]);
        h *= 1099511628211ULL;
    }
    for (unsigned i = 0; i < 8; ++i)
        b[32 + i] = static_cast<std::byte>(h >> (8 * i));
}
} // namespace
int main() {
    using namespace vice;
    using C = world::ScheduleCatchUp;
    using F = world::ScheduleFailure;
    using R = world::ScheduleResult;
    world::World w;
    populate(w);
    require(static_cast<bool>(w.schedule_event(event(), 10, 0, C::coalesce, id(20))) &&
                static_cast<bool>(w.schedule_event(event(), 10, 0, C::coalesce, id(10))),
            "same due fixed IDs");
    require(!w.unregister_component_type({id(100)}) && w.step_world_clock(9) &&
                w.causal_info().retained_count == 0,
            "future indexed and schema held");
    require(w.step_world_clock(1) && w.scheduler_info().count == 0, "one-shot removal");
    const auto a = w.causal_event(2), b = w.causal_event(3);
    require(a && b && a->data->schedule->id == id(10) && b->data->schedule->id == id(20) &&
                a->world_ticks == 10 && a->data->schedule->count == 1,
            "UUID tie order and provenance");
    auto e = event();
    e.data.parents = {{w.causal_info().id, b->id, b->sequence}};
    require(static_cast<bool>(w.schedule_event(e, 20, 10, C::replay, id(30))) &&
                static_cast<bool>(w.schedule_event(e, 20, 10, C::coalesce, id(40))),
            "recurring policies");
    const auto saved = bytes(w);
    world::World resumed;
    require(resumed.restore(*world::decode_world_snapshot(saved).snapshot) &&
                bytes(resumed) == saved && resumed.step_world_clock(90) && w.step_world_clock(90),
            "save before due and resume");
    require(w.schedule(id(30))->occurrence == 9 && w.schedule(id(40))->occurrence == 9 &&
                w.schedule(id(40))->due_ticks == 110,
            "recurrence anchored to original cadence");
    const auto history = w.causal_snapshot();
    require(std::count_if(history.events.begin(), history.events.end(),
                          [](const auto &r) {
                              return r.data && r.data->schedule && r.data->schedule->id == id(30);
                          }) == 9,
            "replay every occurrence");
    const auto coalesced =
        std::find_if(history.events.begin(), history.events.end(), [](const auto &r) {
            return r.data && r.data->schedule && r.data->schedule->id == id(40);
        });
    require(coalesced != history.events.end() && coalesced->data->schedule->count == 9 &&
                coalesced->data->schedule->occurrence == 9 &&
                coalesced->data->schedule->due_ticks == 100 && coalesced->world_ticks == 100,
            "coalescing explicit count and actual firing time");
    require(world::encode_world_snapshot(w.snapshot()).bytes == bytes(w),
            "deterministic serialization");
    const auto source = tools::world_cooker::serialize_source_manifest(w.snapshot());
    const auto parsed = tools::world_cooker::parse_source_manifest(source);
    require(parsed.snapshot &&
                tools::world_cooker::serialize_source_manifest(*parsed.snapshot) == source,
            "source preserves schedules and provenance");
    require(w.configure_world_clock(true, 1000, true) && w.advance_world_clock(.25) &&
                w.schedule(id(30))->occurrence == 9,
            "clock pause leaves work pending");
    const auto before = bytes(w);
    auto bad = w.snapshot();
    bad.schedules.push_back(bad.schedules.front());
    require(!w.restore(bad) && bytes(w) == before, "duplicate schedule restore atomic");
    bad = w.snapshot();
    bad.schedules.front().catch_up = static_cast<C>(99);
    require(!w.restore(bad) && bytes(w) == before, "invalid enum restore");
    bad = w.snapshot();
    bad.schedules.front().event.data.payload.version = 2;
    require(!w.restore(bad) && bytes(w) == before, "schema restore failure");
    bad = w.snapshot();
    bad.causal_history.events.back().data->schedule->due_ticks = 101;
    require(!w.restore(bad) && bytes(w) == before, "future firing provenance rejected");
    require(w.schedule_event(event(), 3, 0, C::coalesce, id(30)).result == R::duplicate_id,
            "duplicate creation rejects");
    e = event();
    e.subject = id(900);
    require(w.schedule_event(e, 4).result == R::event_rejected && bytes(w) == before,
            "missing target creation atomic");
    require(w.cancel_schedule(id(30)) && !w.cancel_schedule(id(30)) && w.cancel_schedule(id(40)),
            "cancel recurring exactly once");
    world::World deleted;
    populate(deleted);
    require(static_cast<bool>(deleted.schedule_event(event(), 1, 0, C::coalesce, id(10))) &&
                deleted.destroy_entity(*deleted.find(id(2))) && deleted.step_world_clock(1),
            "delete before due");
    require(deleted.schedule(id(10))->failure == F::event_rejected &&
                deleted.schedule(id(10))->event_result == world::WorldEventResult::missing_entity &&
                deleted.scheduler_info().pending == 0 && deleted.scheduler_info().failed == 1,
            "failed work retained without retry spin");
    world::World restored;
    require(restored.restore(deleted.snapshot()) &&
                restored.schedule(id(10))->failure == F::event_rejected,
            "failed work survives missing owner restore");
    require(!deleted.unregister_component_type({id(100)}) && deleted.cancel_schedule(id(10)) &&
                deleted.unregister_component_type({id(100)}),
            "failed work schema release");
    world::World pruning;
    populate(pruning);
    const auto root = pruning.emit_world_event(event());
    e = event();
    e.data.parents = {root.ref};
    require(static_cast<bool>(pruning.schedule_event(e, 10, 0, C::coalesce, id(10))),
            "capture cause at scheduling");
    for (unsigned i = 0; i < 1100; ++i)
        require(static_cast<bool>(pruning.emit_world_event(event())), "prune scheduling cause");
    require(pruning.emit_world_event(e).result == world::WorldEventResult::parent_pruned &&
                pruning.step_world_clock(10) &&
                pruning.causal_event(pruning.causal_info().next_sequence - 1)->parent_id ==
                    root.ref.id,
            "scheduler preserves previously validated pruned evidence");
    world::World budget;
    populate(budget);
    require(static_cast<bool>(budget.schedule_event(event(), 1, 1, C::replay, id(10))) &&
                budget.step_world_clock(1000),
            "large replay jump");
    require(budget.schedule(id(10))->occurrence == 256 && budget.scheduler_info().due_remaining,
            "automatic dispatch bounded");
    const auto zero = budget.dispatch_schedules(0);
    require(!zero.processed && zero.due_remaining && budget.schedule(id(10))->occurrence == 256,
            "zero native budget does no work");
    const auto pending = bytes(budget);
    world::World backlog;
    require(backlog.restore(*world::decode_world_snapshot(pending).snapshot) &&
                backlog.schedule(id(10))->occurrence == 256,
            "restore never pumps backlog");
    auto pump = backlog.dispatch_schedules(1);
    require(pump.processed == 1 && pump.fired == 1 && pump.due_remaining, "explicit bounded pump");
    while (backlog.scheduler_info().due_remaining)
        (void)backlog.dispatch_schedules();
    require(backlog.schedule(id(10))->occurrence == 1000 &&
                backlog.schedule(id(10))->due_ticks == 1001,
            "backlog drains deterministically");
    world::World overflow;
    populate(overflow);
    require(static_cast<bool>(overflow.schedule_event(event(), 0, 1, C::coalesce, id(10))) &&
                overflow.step_world_clock(std::numeric_limits<std::uint64_t>::max()) &&
                overflow.schedule(id(10))->failure == F::occurrence_overflow,
            "coalesced count overflow cannot wrap");
    world::World final;
    populate(final);
    require(static_cast<bool>(final.schedule_event(
                event(), std::numeric_limits<std::uint64_t>::max(), 1, C::replay, id(10))) &&
                final.step_world_clock(std::numeric_limits<std::uint64_t>::max()) &&
                final.schedule(id(10))->occurrence == 1 &&
                final.schedule(id(10))->failure == F::time_overflow,
            "final valid occurrence fires before cadence exhaustion");
    require(restored.restore(final.snapshot()), "overflow failure persists");
    world::World signal;
    populate(signal);
    require(static_cast<bool>(signal.schedule_event(event(false), 2)) &&
                signal.step_world_clock(2) && signal.world_signal(1)->data->schedule &&
                signal.scheduler_info().count == 0,
            "scheduled transient signal");
    require(restored.restore(signal.snapshot()) && restored.signal_info().retained_count == 0,
            "signal not replayed after load");
    world::World remote;
    populate(remote);
    world::RegionDesc first;
    first.id = {1};
    first.simulation_island = {10};
    first.origin = {1e12, 0, 0};
    first.fidelity = world::RegionFidelity::inactive;
    auto second = first;
    second.id = {2};
    second.origin.x += 100;
    second.fidelity = world::RegionFidelity::reduced;
    require(remote.register_region(first) && remote.register_region(second) &&
                remote.set_spatial_state(*remote.find(id(1)), {{1e12, 5, 0}, {1}}) &&
                static_cast<bool>(remote.schedule_event(event(), 5, 0, C::coalesce, id(10))) &&
                remote.handoff_spatial(*remote.find(id(1)), {2}) && remote.step_world_clock(5),
            "scheduled work in inactive/reduced regions after handoff");
    require(remote.causal_event(remote.causal_info().next_sequence - 1)->region.value == 2 &&
                remote.spatial_state(*remote.find(id(1)))->global_position.x == 1e12,
            "due event resolves current large-world region");
    world::World legacy;
    auto old = bytes(legacy);
    old.erase(old.end() - 38, old.end() - 26);
    old[8] = std::byte{19};
    rehash(old);
    const auto v19 = world::decode_world_snapshot(old);
    require(v19.snapshot && v19.snapshot->schedules.empty(), "v19 loads empty scheduler");
    auto malformed = bytes(legacy);
    for (unsigned i = 0; i < 4; ++i)
        malformed[malformed.size() - 38 + i] = std::byte{255};
    rehash(malformed);
    require(!world::decode_world_snapshot(malformed).snapshot,
            "unsafe scheduler table count rejected");
    world::World stress;
    populate(stress);
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 1; i <= world::world_schedule_capacity; ++i)
        require(static_cast<bool>(stress.schedule_event(event(false), 1000000000 + i, 0,
                                                        C::coalesce, id(i + 1000))),
                "large future index");
    require(stress.schedule_event(event(), 9).result == R::capacity_exceeded, "capacity bounded");
    const auto indexed = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 10000; ++i)
        require(stress.step_world_clock(0), "idle indexed step");
    const auto end = std::chrono::steady_clock::now();
    auto page = stress.schedule_page();
    require(page.entries.size() == 64 && page.has_more && stress.schedule_context({}, 2).truncated,
            "bounded ordered query");
    std::size_t count = 0;
    world::PersistentId cursor;
    do {
        page = stress.schedule_page(cursor);
        count += page.entries.size();
        if (!page.entries.empty())
            cursor = page.entries.back().id;
    } while (page.has_more);
    require(count == world::world_schedule_capacity, "cursor covers every schedule");
    std::cout << "Scheduler stress: " << count
              << " insert_ms=" << std::chrono::duration<double, std::milli>(indexed - start).count()
              << " idle_10000_steps_ms="
              << std::chrono::duration<double, std::milli>(end - indexed).count()
              << " sizeof_schedule=" << sizeof(world::WorldSchedule)
              << " bytes snapshot=" << bytes(stress).size() << " bytes\n";
}
