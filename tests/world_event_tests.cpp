#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>
namespace {
using namespace vice;
void require(bool ok, const char *s) {
    if (!ok) {
        std::cerr << "Events: " << s << '\n';
        std::exit(1);
    }
}
world::PersistentId id(unsigned n) {
    world::PersistentId::Bytes b{};
    b[0] = 0xb8;
    b[6] = 0x40;
    b[8] = 0x80;
    for (unsigned i = 0; i < 4; ++i)
        b[12 + i] = static_cast<std::uint8_t>(n >> (i * 8));
    return world::PersistentId{b};
}
world::ComponentTypeDesc schema(unsigned n = 100) {
    return {{id(n)},
            "ExampleFact" + std::to_string(n),
            1,
            {{1, world::ComponentValueKind::integer, "quantity"},
             {2, world::ComponentValueKind::real, "measure"}}};
}
world::WorldEventRequest request(bool retained = true, unsigned type = 100) {
    world::WorldEventRequest r;
    r.subject = id(1);
    r.target = id(2);
    r.object = id(1);
    r.retained = retained;
    r.data.payload = {{id(type)}, 1, {{2, -0.0}, {1, std::int64_t{50}}}};
    return r;
}
void populate(world::World &w) {
    require(w.create_entity(id(1)).has_value() && w.create_entity(id(2)).has_value() &&
                w.register_component_type(schema()),
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
    using R = world::WorldEventResult;
    world::World w;
    populate(w);
    require(w.step_world_clock(17), "clock");
    const auto first = w.emit_world_event(request());
    require(static_cast<bool>(first) && first.ref.sequence == 2,
            "capture starts before first fact");
    auto r = request();
    r.data.parents = {first.ref};
    require(w.step_world_clock(9), "advance");
    const auto second = w.emit_world_event(r);
    require(static_cast<bool>(second), "one cause");
    r.data.parents = {second.ref, first.ref};
    const auto third = w.emit_world_event(r);
    require(static_cast<bool>(third), "multiple causes");
    const auto event = w.causal_event(third.ref.sequence);
    require(event && event->world_ticks == 26 && event->first == id(2) && event->second == id(1) &&
                event->data->parents[0] == first.ref && event->data->parents[1] == second.ref,
            "timestamp, targets and canonical parent order");
    require(!std::signbit(std::get<double>(event->data->payload.fields[1].value)) &&
                !w.unregister_component_type({id(100)}),
            "canonical payload and event schema usage");
    const auto context = w.causal_context(id(2));
    require(context.events.size() == 3 &&
                world::serialize_causal_context(context).find("included") != std::string::npos,
            "target query follows all causes");
    const auto before = bytes(w);
    const auto unchanged = [&] {
        require(bytes(w) == before, "invalid emission changes no persistent state");
    };
    r.data.parents = {first.ref, first.ref};
    require(w.emit_world_event(r).result == R::invalid_parent, "duplicate cause");
    unchanged();
    r = request();
    r.data.parents = {{id(900), id(901), 1}};
    require(w.emit_world_event(r).result == R::history_mismatch, "wrong history");
    unchanged();
    r.data.parents = {{first.ref.history, id(902), first.ref.sequence}};
    require(w.emit_world_event(r).result == R::invalid_parent, "wrong UUID");
    unchanged();
    r = request();
    r.subject = id(999);
    require(w.emit_world_event(r).result == R::missing_entity, "stale identity");
    unchanged();
    r = request();
    r.region = {99};
    require(w.emit_world_event(r).result == R::invalid_argument, "missing region");
    unchanged();
    r = request(true, 999);
    require(w.emit_world_event(r).result == R::missing_schema, "missing schema");
    unchanged();
    r = request();
    r.data.payload.version = 2;
    require(w.emit_world_event(r).result == R::schema_conflict, "version");
    unchanged();
    r = request();
    r.data.payload.fields[0].value = std::numeric_limits<double>::infinity();
    require(w.emit_world_event(r).result == R::invalid_argument, "nonfinite payload");
    unchanged();
    auto saved = w.snapshot();
    const auto text = tools::world_cooker::serialize_source_manifest(saved);
    const auto parsed = tools::world_cooker::parse_source_manifest(text);
    require(parsed.snapshot &&
                parsed.snapshot->causal_history.events == saved.causal_history.events &&
                tools::world_cooker::serialize_source_manifest(*parsed.snapshot) == text,
            "deterministic source");
    const auto decoded = world::decode_world_snapshot(before);
    world::World restored;
    require(decoded.snapshot && restored.restore(*decoded.snapshot) && bytes(restored) == before,
            "binary restore exact identity and values");
    r = request();
    r.data.parents = {third.ref};
    const auto continued = restored.emit_world_event(r);
    require(static_cast<bool>(continued) && continued.ref.sequence == third.ref.sequence + 2 &&
                restored.causal_event(continued.ref.sequence)->parent_id == third.ref.id,
            "resume creates segment without losing parent");
    auto bad = saved;
    bad.causal_history.events.back().world_ticks = 27;
    require(!w.restore(bad), "future timestamp restore");
    unchanged();
    bad = saved;
    bad.causal_history.events.back().data->parents[1].id = id(999);
    require(!w.restore(bad), "second parent UUID validation");
    unchanged();
    bad = saved;
    bad.causal_history.events.back().data->payload.version = 99;
    require(!w.restore(bad) && !world::encode_world_snapshot(bad),
            "invalid payload restore and encode");
    unchanged();
    bad = saved;
    bad.causal_history.events.back().data->parents.resize(5, first.ref);
    require(!w.restore(bad), "parent bound");
    unchanged();
    const auto signal = w.emit_world_event(request(false));
    require(static_cast<bool>(signal) && w.world_signal(1) && bytes(w) == before,
            "signal never saved");
    r = request();
    r.data.parents = {signal.ref};
    require(w.emit_world_event(r).result == R::history_mismatch,
            "signal is not persistent causal evidence");
    require(w.configure_world_clock(true, 1000, false) && w.advance_world_clock(.25) &&
                w.signal_info().retained_count == 1,
            "paused signal window");
    require(w.step_world_clock(world::world_signal_lifetime_ticks - 1) && w.world_signal(1) &&
                w.step_world_clock(1) && !w.world_signal(1),
            "expiry exact boundary while manually stepping");
    require(w.register_component_type(schema(101)), "signal-only schema");
    for (unsigned i = 0; i < 300; ++i)
        require(static_cast<bool>(w.emit_world_event(request(false, 101))), "bounded signals");
    require(w.signal_info().retained_count == 256 && w.signal_info().first_sequence == 46 &&
                !w.unregister_component_type({id(101)}) && w.signal_context(id(2), 2).truncated,
            "signal capacity and query budget");
    world::World clone;
    require(clone.restore(w.snapshot()) && clone.signal_info().retained_count == 0 &&
                clone.signal_info().stream.is_nil(),
            "signals excluded from Play clone and restore");
    w.clear_world_signals();
    require(w.unregister_component_type({id(101)}), "signal cleanup releases schema");
    world::World stress;
    populate(stress);
    require(stress.register_component_type(schema(101)), "stress schema");
    auto old = stress.emit_world_event(request(true, 101));
    auto cause = old.ref;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 12000; ++i) {
        r = request();
        r.data.parents = {cause};
        const auto next = stress.emit_world_event(r);
        require(static_cast<bool>(next), "chained emission stress");
        cause = next.ref;
    }
    require(stress.causal_info().retained_count == 1024 &&
                stress.unregister_component_type({id(101)}) &&
                world::validate_causal_history(stress.causal_snapshot()),
            "bounded history releases old schema and preserves pruned causes");
    r = request();
    r.data.parents = {old.ref};
    require(stress.emit_world_event(r).result == R::parent_pruned, "pruned cause rejection");
    world::World distant;
    populate(distant);
    world::RegionDesc region;
    region.id = {1};
    region.simulation_island = {10};
    region.origin = {1e12, 0, 0};
    region.fidelity = world::RegionFidelity::inactive;
    require(distant.register_region(region) &&
                distant.set_spatial_state(*distant.find(id(1)), {{1e12, 5, 0}, {1}}),
            "inactive large World");
    const auto distant_fact = distant.emit_world_event(request());
    require(static_cast<bool>(distant_fact) &&
                distant.causal_event(distant_fact.ref.sequence)->region.value == 1 &&
                distant.set_region_fidelity({1}, world::RegionFidelity::reduced),
            "facts independent of fidelity");
    world::World distant_restore;
    require(distant_restore.restore(distant.snapshot()) &&
                distant_restore.spatial_state(*distant_restore.find(id(1)))->global_position.x ==
                    1e12,
            "large coordinate event persistence");
    const auto projection = stress.snapshot(world::ComponentPayloadSnapshot::render);
    require(projection.causal_history.events.empty() && !stress.restore(projection),
            "render skips event payload history");
    world::World legacy;
    require(legacy.begin_causal_recording(), "legacy marker");
    auto v18 = bytes(legacy);
    v18.erase(v18.end() - 50, v18.end() - 26);
    v18.erase(v18.end() - 35, v18.end() - 26);
    v18[8] = std::byte{18};
    rehash(v18);
    const auto read18 = world::decode_world_snapshot(v18);
    require(read18.snapshot && read18.snapshot->causal_history.events[0].world_ticks == 0,
            "v18 journal defaults timestamps");
    auto corrupt = before;
    corrupt[corrupt.size() - 39] = std::byte{255};
    rehash(corrupt);
    require(!world::decode_world_snapshot(corrupt).snapshot,
            "malformed typed text rejected after checksum repair");
    std::cout << "Event stress: 12000 chained facts in "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                     .count()
              << "ms; CausalEvent=" << sizeof(world::CausalEvent)
              << " EventRef=" << sizeof(world::EventRef)
              << " bytes; retained=" << stress.causal_info().retained_count
              << " file=" << bytes(stress).size() << " bytes\n";
}
