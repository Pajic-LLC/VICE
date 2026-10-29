#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <locale>
#include <vice/tools/world_authoring.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/tools/world_edit.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>
namespace {
using namespace vice;
void require(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "Components: " << message << '\n';
        std::exit(1);
    }
}
world::PersistentId id(unsigned n) {
    world::PersistentId::Bytes b{};
    b[0] = 0xb7;
    b[6] = 0x40;
    b[8] = 0x80;
    for (unsigned i = 0; i < 4; ++i)
        b[12 + i] = static_cast<std::uint8_t>(n >> (i * 8));
    return world::PersistentId{b};
}
world::ComponentTypeDesc schema() {
    using K = world::ComponentValueKind;
    return {{id(100)},
            "ExampleState",
            3,
            {{5, K::identifier, "stable_key"},
             {2, K::integer, "counter"},
             {1, K::boolean, "enabled"},
             {4, K::text, "label"},
             {3, K::real, "measure"}}};
}
world::ComponentPayload payload() {
    return {{id(100)},
            3,
            {{4, std::string{"name \"quoted\" \\ path café"}},
             {3, -0.0},
             {2, std::numeric_limits<std::int64_t>::max()},
             {5, id(500)},
             {1, true}}};
}
std::vector<std::byte> encoded(const world::World &w) {
    auto value = world::encode_world_snapshot(w.snapshot());
    require(static_cast<bool>(value), "encode validated state");
    return value.bytes;
}
} // namespace
int main() {
    using namespace vice;
    world::World w;
    const auto e = *w.create_entity(id(1));
    require(w.register_component_type(schema()), "game-defined schema registration");
    require(w.set_component(e, payload()), "attach all typed values");
    const auto initial = encoded(w);
    require(w.component_payload_count() == 1 && w.components(e).size() == 1 &&
                !w.unregister_component_type({id(100)}),
            "one instance and live schema guard");
    const auto projection = w.snapshot(world::ComponentPayloadSnapshot::omit);
    require(!projection.component_payloads_included && projection.entities[0].components.empty() &&
                !w.restore(projection) && !world::encode_world_snapshot(projection) &&
                encoded(w) == initial,
            "read-only render projections skip payload copies and cannot be accidentally "
            "saved/restored");
    auto reverse = schema();
    std::reverse(reverse.fields.begin(), reverse.fields.end());
    world::World alternate;
    require(alternate.create_entity(id(1)).has_value() &&
                alternate.register_component_type(reverse),
            "permuted schema");
    auto p = payload();
    std::reverse(p.fields.begin(), p.fields.end());
    require(alternate.set_component(*alternate.find(id(1)), p) && encoded(alternate) == initial,
            "wire bytes independent of insertion order and negative zero");
    require(!std::signbit(std::get<double>(w.component(e, {id(100)})->fields[2].value)),
            "negative zero normalized");
    const auto serialized = world::serialize_component_payloads(w.components(e));
    require(serialized.find("\"9223372036854775807\"") != std::string::npos,
            "debug integers remain exact beyond JSON binary64 precision");
    auto bad = schema();
    bad.id = {id(101)};
    bad.name = "Other";
    bad.fields[0].id = bad.fields[1].id;
    require(!w.register_component_type(bad), "duplicate field ID rejected");
    bad = schema();
    bad.id = {id(101)};
    bad.name = "Other";
    bad.fields[0].name = bad.fields[1].name;
    require(!w.register_component_type(bad), "duplicate field name rejected");
    const auto rejected = [&](world::ComponentPayload value) {
        require(!w.set_component(e, value) && encoded(w) == initial,
                "invalid upsert preserves old payload");
    };
    p = payload();
    p.version = 4;
    rejected(p);
    p = payload();
    p.fields.pop_back();
    rejected(p);
    p = payload();
    p.fields.push_back(p.fields.back());
    rejected(p);
    p = payload();
    p.fields[2].value = 2.0;
    rejected(p);
    p = payload();
    p.fields[1].value = std::numeric_limits<double>::infinity();
    rejected(p);
    p = payload();
    p.fields[0].value = std::string(129, 'x');
    rejected(p);
    p = payload();
    p.fields[0].value = std::string{"bad\nline"};
    rejected(p);
    p = payload();
    p.fields[0].value = std::string{"\xc0\x80", 2};
    rejected(p);
    p = payload();
    p.fields[3].value = world::PersistentId{};
    rejected(p);
    auto snapshot = w.snapshot();
    snapshot.entities[0].components.push_back(payload());
    require(!w.restore(snapshot) && encoded(w) == initial, "duplicate restore is atomic");
    snapshot = w.snapshot();
    snapshot.component_types[0].version = 4;
    require(!w.restore(snapshot) && encoded(w) == initial,
            "unsupported migration cannot partially replace World");
    snapshot = w.snapshot();
    snapshot.component_types.clear();
    require(!w.restore(snapshot) && encoded(w) == initial,
            "missing schema rejected transactionally");
    const auto source = tools::world_cooker::serialize_source_manifest(w.snapshot());
    struct Comma final : std::numpunct<char> {
        char do_decimal_point() const override { return ','; }
        char do_thousands_sep() const override { return '.'; }
        std::string do_grouping() const override { return "\3"; }
    };
    const auto previous_locale =
        std::locale::global(std::locale(std::locale::classic(), new Comma));
    const auto localized_source = tools::world_cooker::serialize_source_manifest(w.snapshot());
    const auto localized_parse = tools::world_cooker::parse_source_manifest(source);
    std::locale::global(previous_locale);
    require(localized_source == source && localized_parse.snapshot &&
                tools::world_cooker::serialize_source_manifest(*localized_parse.snapshot) == source,
            "source and component values ignore process locale");
    const auto parsed = tools::world_cooker::parse_source_manifest(source);
    require(parsed.snapshot &&
                tools::world_cooker::serialize_source_manifest(*parsed.snapshot) == source,
            "canonical diffable source roundtrip with escaped UTF-8");
    const auto decoded = world::decode_world_snapshot(initial);
    world::World restored;
    require(decoded.snapshot && restored.restore(*decoded.snapshot) && encoded(restored) == initial,
            "binary snapshot restore retains values and persistent identities");
    auto truncated = initial;
    truncated.pop_back();
    require(!world::decode_world_snapshot(truncated), "truncated checkpoint rejected");
    world::World bounded;
    const auto owner = bounded.create_entity();
    for (unsigned i = 0; i < 17; ++i) {
        world::ComponentTypeDesc t{{id(200 + i)}, "Marker" + std::to_string(i), 1, {}};
        require(bounded.register_component_type(t), "bounded schema");
        const bool attached = bounded.set_component(owner, {t.id, 1, {}});
        require(attached == (i < 16), "per-owner payload limit rejects without mutation");
    }
    require(bounded.component_payload_count() == 16 && bounded.remove_component(owner, {id(200)}) &&
                bounded.set_component(owner, {{id(216)}, 1, {}}),
            "capacity returns after indexed removal");
    world::World maximum;
    auto many = schema();
    many.fields.clear();
    for (unsigned i = 1; i <= 16; ++i)
        many.fields.push_back({i, world::ComponentValueKind::text, "f" + std::to_string(i)});
    require(maximum.register_component_type(many), "maximum field schema");
    world::ComponentPayload full{{id(100)}, 3, {}};
    for (unsigned i = 1; i <= 16; ++i)
        full.fields.push_back({i, std::string(128, '"')});
    require(maximum.set_component(maximum.create_entity(), full) &&
                world::decode_world_snapshot(encoded(maximum)).snapshot.has_value(),
            "maximum escaped text payload roundtrips within bounded record");
    world::World tags;
    require(tags.register_component_type({{id(101)}, "Tag", 1, {}}) &&
                tags.set_component(tags.create_entity(), {{id(101)}, 1, {}}),
            "empty schema is a persistent marker component");
    world::World empty;
    auto legacy = encoded(empty);
    legacy.resize(legacy.size() - 38);
    legacy[8] = std::byte{15};
    const auto old = world::decode_world_snapshot(legacy);
    require(!old, "checksum protects truncated format conversion");
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 40; i < legacy.size(); ++i) {
        hash ^= std::to_integer<std::uint8_t>(legacy[i]);
        hash *= 1099511628211ULL;
    }
    for (unsigned i = 0; i < 8; ++i)
        legacy[32 + i] = static_cast<std::byte>(hash >> (8 * i));
    require(world::decode_world_snapshot(legacy).snapshot.has_value(),
            "v15 without component extensions remains readable");
    world::World legacy_types;
    const world::ComponentTypeDesc legacy_type{{id(800)}, "Legacy", 1, {}};
    require(legacy_types.register_component_type(legacy_type), "legacy identity-only schema");
    auto legacy_schema = encoded(legacy_types);
    const auto extension = 40 + 16 + 4 + 4 + legacy_type.name.size();
    legacy_schema.erase(legacy_schema.begin() + static_cast<std::ptrdiff_t>(extension),
                        legacy_schema.begin() + static_cast<std::ptrdiff_t>(extension + 5));
    legacy_schema.resize(legacy_schema.size() - 38);
    legacy_schema[8] = std::byte{15};
    hash = 14695981039346656037ULL;
    for (std::size_t i = 40; i < legacy_schema.size(); ++i) {
        hash ^= std::to_integer<std::uint8_t>(legacy_schema[i]);
        hash *= 1099511628211ULL;
    }
    for (unsigned i = 0; i < 8; ++i)
        legacy_schema[32 + i] = static_cast<std::byte>(hash >> (8 * i));
    const auto legacy_read = world::decode_world_snapshot(legacy_schema);
    require(legacy_read.snapshot && legacy_read.snapshot->component_types[0] == legacy_type,
            "v15 identity registry remains readable without schema fields");
    // Source edits use the same candidate/validate/commit boundary as all World authoring.
    tools::world_authoring::Transaction tx;
    auto changed = payload();
    changed.fields[2].value = std::int64_t{27};
    tx.operations = {tools::world_authoring::SetComponent{id(1), changed}};
    const auto applied = tools::world_authoring::apply(w.snapshot(), tx);
    require(static_cast<bool>(applied), "typed component edit");
    tx.operations.push_back(tools::world_authoring::SetComponent{id(999), payload()});
    require(!tools::world_authoring::apply(w.snapshot(), tx) && encoded(w) == initial,
            "invalid second edit cannot publish first edit");
    require(w.register_region({.id = {1},
                               .simulation_island = {1},
                               .origin = {1e12, 0, -1e12},
                               .fidelity = world::RegionFidelity::inactive}) &&
                w.set_spatial_state(e, {{1e12 + 3, 2, -1e12 - 5}, {1}}),
            "large-world inactive fixture");
    require(w.set_component(e, changed) && w.clear_spatial_state(e) &&
                w.component(e, {id(100)}).has_value(),
            "stored component state is independent of spatial/fidelity representation");
    require(w.remove_component(e, {id(100)}) && !w.remove_component(e, {id(100)}) &&
                w.component_payload_count() == 0 && w.unregister_component_type({id(100)}),
            "remove and unregister release use index");
    require(w.register_component_type(schema()) && w.set_component(e, payload()) &&
                w.destroy_entity(e),
            "owner destruction removes payload");
    const auto replacement = w.create_entity();
    require(!w.set_component(e, payload()) && w.components(replacement).empty() &&
                w.component_payload_count() == 0 && w.unregister_component_type({id(100)}),
            "generation checks and recycled slots cannot resurrect payloads");
    world::World stress;
    require(stress.register_component_type(schema()), "stress schema");
    const auto begin = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 10000; ++i)
        require(stress.set_component(*stress.create_entity(id(i + 1000)), payload()),
                "stress attach");
    const auto attach = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 10000; ++i)
        require(stress.component(*stress.find(id(i + 1000)), {id(100)}).has_value(),
                "stress indexed lookup");
    const auto query = std::chrono::steady_clock::now();
    const auto cooked = encoded(stress);
    const auto serialized_at = std::chrono::steady_clock::now();
    world::World resumed;
    const auto state = world::decode_world_snapshot(cooked);
    require(state.snapshot && resumed.restore(*state.snapshot) &&
                resumed.component_payload_count() == 10000,
            "10k payload decode and restore");
    const auto ms = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    std::cout
        << "{\"entities\":10000,\"payloads\":10000,\"fields_per_payload\":5,\"field_value_size\":"
        << sizeof(world::ComponentFieldValue)
        << ",\"payload_object_size\":" << sizeof(world::ComponentPayload)
        << ",\"attach_ms\":" << ms(begin, attach) << ",\"query_ms\":" << ms(attach, query)
        << ",\"encode_ms\":" << ms(query, serialized_at) << ",\"cooked_bytes\":" << cooked.size()
        << "}\n";
}
