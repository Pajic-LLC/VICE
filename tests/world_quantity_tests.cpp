#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>
namespace {
using namespace vice;
void require(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "Quantities: " << message << '\n';
        std::exit(1);
    }
}
world::PersistentId id(unsigned n) {
    world::PersistentId::Bytes b{};
    b[0] = 0xc8;
    b[6] = 0x40;
    b[8] = 0x80;
    for (unsigned i = 0; i < 4; ++i)
        b[12 + i] = static_cast<std::uint8_t>(n >> (i * 8));
    return world::PersistentId{b};
}
world::QuantityTypeDesc type(unsigned n = 100) {
    return {{id(n)}, "ExampleUnits" + std::to_string(n), 1, 100};
}
world::QuantityKey key(unsigned holder, unsigned t = 100) { return {id(holder), {id(t)}}; }
world::QuantityTransaction transaction(world::QuantityOperation op, unsigned source,
                                       unsigned target, std::int64_t amount) {
    world::QuantityTransaction t;
    t.operation = op;
    t.source = source ? id(source) : world::PersistentId{};
    t.target = target ? id(target) : world::PersistentId{};
    t.type = {id(100)};
    t.amount = amount;
    return t;
}
void populate(world::World &w) {
    require(w.create_entity(id(1)).has_value() && w.create_entity(id(2)).has_value() &&
                w.create_entity(id(3)).has_value() && w.register_quantity_type(type()),
            "setup");
    require(w.create_quantity_account(key(1), 100) == world::QuantityResult::success &&
                w.create_quantity_account(key(2), 50) == world::QuantityResult::success,
            "empty accounts");
}
std::vector<std::byte> bytes(const world::World &w) {
    const auto result = world::encode_world_snapshot(w.snapshot());
    require(static_cast<bool>(result), "encode");
    return result.bytes;
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
    using O = world::QuantityOperation;
    using R = world::QuantityResult;
    world::World w;
    populate(w);
    require(w.register_quantity_type(type()) && !w.remove_quantity_type({id(100)}),
            "immutable idempotent type and account reference guard");
    auto conflict = type();
    conflict.quanta_per_unit = 10;
    require(!w.register_quantity_type(conflict), "unit-scale conflict");
    const auto produced = w.transact_quantity(transaction(O::produce, 0, 1, 100));
    require(static_cast<bool>(produced) && produced.target_before == 0 &&
                produced.target_after == 100,
            "produce exact quanta");
    auto move = transaction(O::transfer, 1, 2, 25);
    move.parents = {produced.event};
    const auto transferred = w.transact_quantity(move);
    require(static_cast<bool>(transferred) && transferred.source_before == 100 &&
                transferred.source_after == 75 && transferred.target_before == 0 &&
                transferred.target_after == 25,
            "atomic transfer");
    const auto fact = w.causal_event(transferred.event.sequence);
    require(fact && fact->data->payload.type == world::quantity_receipt_type() &&
                fact->parent_id == produced.event.id && fact->subject == id(1) &&
                fact->first == id(2),
            "engine receipt and provenance");
    const auto before = bytes(w);
    const auto unchanged = [&] {
        require(bytes(w) == before && w.quantity_account(key(1))->amount == 75 &&
                    w.quantity_account(key(2))->amount == 25,
                "failed operation leaves World unchanged");
    };
    require(w.transact_quantity(transaction(O::transfer, 1, 2, 76)).result ==
                R::insufficient_quantity,
            "insufficient");
    unchanged();
    require(w.transact_quantity(transaction(O::transfer, 1, 2, 26)).result == R::capacity_exceeded,
            "destination capacity");
    unchanged();
    require(w.transact_quantity(transaction(O::produce, 0, 2, 26)).result == R::capacity_exceeded &&
                w.transact_quantity(transaction(O::consume, 2, 0, 26)).result ==
                    R::insufficient_quantity,
            "production/consumption bounds");
    unchanged();
    for (auto amount : {std::int64_t{0}, std::int64_t{-1}})
        require(w.transact_quantity(transaction(O::transfer, 1, 2, amount)).result ==
                    R::invalid_argument,
                "nonpositive rejected");
    unchanged();
    auto invalid = transaction(O::transfer, 1, 2, 1);
    invalid.type = {id(999)};
    require(w.transact_quantity(invalid).result == R::invalid_type, "invalid resource type");
    invalid = transaction(O::transfer, 1, 99, 1);
    require(w.transact_quantity(invalid).result == R::invalid_owner, "missing owner");
    invalid = transaction(O::transfer, 1, 3, 1);
    require(w.transact_quantity(invalid).result == R::missing_account, "missing container");
    invalid = transaction(O::transfer, 1, 1, 1);
    require(w.transact_quantity(invalid).result == R::invalid_argument, "self-transfer");
    unchanged();
    invalid = move;
    invalid.amount = 1;
    invalid.parents[0].id = id(999);
    const auto rejected = w.transact_quantity(invalid);
    require(rejected.result == R::event_rejected &&
                rejected.event_result == world::WorldEventResult::invalid_parent,
            "invalid parent rejects before balance mutation");
    unchanged();
    require(w.set_quantity_capacity(key(1), 74) == R::capacity_exceeded &&
                w.remove_quantity_account(key(1)) == R::nonempty_account &&
                !w.destroy_entity(*w.find(id(1))),
            "cannot discard balances or shrink below them");
    unchanged();
    world::WorldEventRequest forged;
    forged.subject = id(1);
    forged.target = id(2);
    forged.data = *fact->data;
    require(!w.emit_world_event(forged) && !w.schedule_event(forged, 1) &&
                !w.set_component(*w.find(id(1)), forged.data.payload),
            "receipt cannot be forged through events schedules or components");
    unchanged();
    auto hijack = world::quantity_receipt_schema();
    hijack.fields.clear();
    require(!w.register_component_type(hijack) &&
                !w.unregister_component_type(world::quantity_receipt_type()),
            "engine schema reserved");
    unchanged();
    const auto source = tools::world_cooker::serialize_source_manifest(w.snapshot());
    const auto parsed = tools::world_cooker::parse_source_manifest(source);
    require(parsed.snapshot &&
                tools::world_cooker::serialize_source_manifest(*parsed.snapshot) == source,
            "deterministic source");
    const auto decoded = world::decode_world_snapshot(before);
    world::World resumed;
    require(decoded.snapshot && resumed.restore(*decoded.snapshot) && bytes(resumed) == before,
            "binary identity/state roundtrip");
    move.amount = 10;
    move.parents = {transferred.event};
    const auto next = resumed.transact_quantity(move);
    require(static_cast<bool>(next) && resumed.quantity_account(key(1))->amount == 65 &&
                resumed.quantity_account(key(2))->amount == 35 &&
                resumed.causal_event(next.event.sequence)->parent_id == transferred.event.id,
            "checkpoint continues quantities and causes");
    auto bad = w.snapshot();
    bad.quantity_accounts.push_back(bad.quantity_accounts.front());
    require(!w.restore(bad), "duplicate accounts restore atomic");
    unchanged();
    bad = w.snapshot();
    bad.quantity_accounts[0].amount = bad.quantity_accounts[0].capacity + 1;
    require(!w.restore(bad), "over-capacity restore");
    unchanged();
    bad = w.snapshot();
    bad.quantity_accounts[0].key.holder = id(999);
    require(!w.restore(bad), "missing holder restore");
    unchanged();
    bad = w.snapshot();
    bad.quantity_types.push_back(bad.quantity_types.front());
    require(!w.restore(bad), "duplicate catalog identity");
    unchanged();
    bad = w.snapshot();
    bad.quantity_types[0].version = 2;
    require(!w.restore(bad), "historical receipt schema version validation");
    unchanged();
    bad = w.snapshot();
    bad.causal_history.events.back().data->payload.fields[5].value = std::int64_t{99};
    require(!w.restore(bad) && !world::encode_world_snapshot(bad),
            "malformed receipt conservation rejected");
    unchanged();
    world::World exhausted;
    auto end = w.snapshot();
    world::CausalEvent marker;
    marker.id = marker.capture_id = id(900);
    marker.sequence = std::numeric_limits<std::uint64_t>::max() - 1;
    end.causal_history = {id(901), std::numeric_limits<std::uint64_t>::max(), {marker}};
    require(exhausted.restore(end), "sequence ceiling fixture");
    const auto at_ceiling = bytes(exhausted);
    auto ceiling = exhausted.transact_quantity(transaction(O::transfer, 1, 2, 1));
    require(ceiling.result == R::event_rejected &&
                ceiling.event_result == world::WorldEventResult::sequence_exhausted &&
                bytes(exhausted) == at_ceiling,
            "receipt capacity failure cannot partially move quantities");
    world::World wide;
    populate(wide);
    require(wide.set_quantity_capacity(key(1), std::numeric_limits<std::int64_t>::max()) ==
                    R::success &&
                wide.set_quantity_capacity(key(2), std::numeric_limits<std::int64_t>::max()) ==
                    R::success &&
                static_cast<bool>(wide.transact_quantity(
                    transaction(O::produce, 0, 1, std::numeric_limits<std::int64_t>::max()))),
            "maximum exact quantity");
    require(wide.transact_quantity(transaction(O::produce, 0, 1, 1)).result ==
                    R::capacity_exceeded &&
                static_cast<bool>(wide.transact_quantity(
                    transaction(O::transfer, 1, 2, std::numeric_limits<std::int64_t>::max()))),
            "maximum transfer does not overflow");
    require(static_cast<bool>(w.transact_quantity(transaction(O::consume, 1, 0, 75))) &&
                static_cast<bool>(w.transact_quantity(transaction(O::consume, 2, 0, 25))) &&
                w.remove_quantity_account(key(1)) == R::success &&
                w.remove_quantity_account(key(2)) == R::success &&
                !w.remove_quantity_type({id(100)}) && w.destroy_entity(*w.find(id(1))),
            "consume, release accounts, preserve historical type/entity evidence");
    world::World historical;
    require(historical.restore(w.snapshot()), "deleted historical owner restores");
    require(w.register_component_type({{id(200)}, "RetentionMarker", 1, {}}), "retention schema");
    for (unsigned i = 0; i < 1100; ++i) {
        world::WorldEventRequest e;
        e.subject = id(2);
        e.data.payload = {{id(200)}, 1, {}};
        require(static_cast<bool>(w.emit_world_event(e)), "prune receipts");
    }
    require(w.remove_quantity_type({id(100)}) &&
                w.unregister_component_type(world::quantity_receipt_type()),
            "receipt pruning releases type/schema use counts");
    world::World remote;
    populate(remote);
    world::RegionDesc region;
    region.id = {1};
    region.simulation_island = {10};
    region.origin = {1e12, 0, 0};
    region.fidelity = world::RegionFidelity::inactive;
    require(remote.register_region(region) &&
                remote.set_spatial_state(*remote.find(id(1)), {{1e12, 5, 0}, {1}}) &&
                static_cast<bool>(remote.transact_quantity(transaction(O::produce, 0, 1, 100))) &&
                static_cast<bool>(remote.transact_quantity(transaction(O::transfer, 1, 2, 50))),
            "inactive large-world quantities");
    require(historical.restore(remote.snapshot()) &&
                historical.quantity_account(key(2))->amount == 50,
            "inactive region resume");
    world::World legacy;
    auto old = bytes(legacy);
    old.erase(old.end() - 50, old.end() - 26);
    old[8] = std::byte{20};
    rehash(old);
    const auto v20 = world::decode_world_snapshot(old);
    require(v20.snapshot && v20.snapshot->quantity_accounts.empty() &&
                v20.snapshot->quantity_types.empty(),
            "v20 defaults empty quantities");
    auto malformed = bytes(legacy);
    for (unsigned i = 0; i < 4; ++i)
        malformed[malformed.size() - 38 + i] = std::byte{255};
    rehash(malformed);
    require(!world::decode_world_snapshot(malformed).snapshot, "unsafe quantity type count");
    auto bad_accounts = bytes(legacy);
    for (unsigned i = 0; i < 4; ++i)
        bad_accounts[bad_accounts.size() - 34 + i] = std::byte{255};
    rehash(bad_accounts);
    require(!world::decode_world_snapshot(bad_accounts).snapshot, "unsafe account table count");
    world::World bounded;
    require(bounded.create_entity(id(1)).has_value(), "bounded holder");
    for (unsigned t = 100; t <= 164; ++t)
        require(bounded.register_quantity_type(type(t)), "bounded type");
    for (unsigned t = 100; t < 164; ++t)
        require(bounded.create_quantity_account(key(1, t), 0) == R::success, "per-holder slots");
    require(bounded.create_quantity_account(key(1, 164), 1) == R::storage_limit &&
                bounded.quantity_accounts(id(1)).size() == 64,
            "per-holder account limit");
    const auto page = bounded.quantity_type_page();
    require(page.entries.size() == 64 && page.has_more &&
                bounded.quantity_type_page(page.entries.back().id.value).entries.size() == 1,
            "bounded type pages");
    auto invalid_type = type(200);
    invalid_type.quanta_per_unit = 0;
    require(!bounded.register_quantity_type(invalid_type), "invalid quantum scale");
    invalid_type = type(200);
    invalid_type.name = "Named \"units\" café";
    require(bounded.register_quantity_type(invalid_type), "UTF-8 named resource");
    const auto quoted = tools::world_cooker::serialize_source_manifest(bounded.snapshot());
    const auto quoted_back = tools::world_cooker::parse_source_manifest(quoted);
    require(quoted_back.snapshot &&
                tools::world_cooker::serialize_source_manifest(*quoted_back.snapshot) == quoted,
            "quantity names escape deterministically");
    world::World stress;
    for (unsigned t = 100; t < 116; ++t)
        require(stress.register_quantity_type(type(t)), "stress type");
    const auto start = std::chrono::steady_clock::now();
    for (unsigned n = 1; n <= 4096; ++n) {
        require(stress.create_entity(id(n)).has_value(), "stress owner");
        for (unsigned t = 100; t < 116; ++t)
            require(stress.create_quantity_account(key(n, t), 1000) == R::success,
                    "stress account");
    }
    const auto inserted = std::chrono::steady_clock::now();
    require(stress.register_quantity_type(type(116)) &&
                stress.quantity_account_count() == world::quantity_account_capacity &&
                stress.create_quantity_account(key(1, 116), 1) == R::storage_limit,
            "bounded accounts");
    require(static_cast<bool>(stress.transact_quantity(transaction(O::produce, 0, 1, 1000))),
            "stress seed");
    for (unsigned i = 0; i < 20000; ++i)
        require(static_cast<bool>(stress.transact_quantity(
                    transaction(O::transfer, i % 2 ? 2 : 1, i % 2 ? 1 : 2, 1))),
                "indexed transfer stress");
    const auto moved = std::chrono::steady_clock::now();
    std::cout << "Quantities: accounts=" << stress.quantity_account_count() << " insert_ms="
              << std::chrono::duration<double, std::milli>(inserted - start).count()
              << " 20000_transfer_ms="
              << std::chrono::duration<double, std::milli>(moved - inserted).count()
              << " sizeof_account=" << sizeof(world::QuantityAccount)
              << " sizeof_type=" << sizeof(world::QuantityTypeDesc)
              << " snapshot_bytes=" << bytes(stress).size() << "\n";
}
