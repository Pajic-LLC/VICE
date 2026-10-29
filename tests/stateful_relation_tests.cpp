#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <vice/tools/world_authoring.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>
namespace {
using namespace vice;
void require(bool ok, const char *why) {
    if (!ok) {
        std::cerr << "Relations: " << why << '\n';
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
world::ComponentTypeDesc schema() {
    return {{id(100)},
            "RelationMeasure",
            2,
            {{1, world::ComponentValueKind::real, "weight"},
             {2, world::ComponentValueKind::integer, "count"},
             {3, world::ComponentValueKind::text, "label"}}};
}
world::ComponentPayload payload() {
    return {{id(100)}, 2, {{3, std::string{"opaque \"value\""}}, {1, .8}, {2, std::int64_t{7}}}};
}
std::vector<std::byte> bytes(const world::World &w) {
    const auto encoded = world::encode_world_snapshot(w.snapshot());
    require(static_cast<bool>(encoded), "encode");
    return encoded.bytes;
}
void rehash(std::vector<std::byte> &data) {
    std::uint64_t h = 14695981039346656037ULL;
    for (std::size_t i = 40; i < data.size(); ++i) {
        h ^= std::to_integer<unsigned char>(data[i]);
        h *= 1099511628211ULL;
    }
    for (unsigned i = 0; i < 8; ++i)
        data[32 + i] = static_cast<std::byte>(h >> (8 * i));
}
} // namespace
int main() {
    using namespace vice;
    world::World w;
    require(w.register_component_type(schema()), "register reusable schema");
    for (unsigned n = 1; n <= 4; ++n)
        require(w.create_entity(id(n)).has_value(), "owners");
    const world::RelationKey edge{id(1), id(50), id(2)}, reverse{id(2), id(50), id(1)},
        self{id(1), id(51), id(1)}, other{id(3), id(50), id(4)};
    require(w.add_relation(edge) && !w.add_relation(edge) && w.add_relation(reverse) &&
                w.add_relation(self) && w.add_relation(other),
            "unique directed keys and self edge");
    require(w.set_relation_component(edge, payload()) &&
                w.set_component(*w.find(id(3)), payload()) &&
                !w.unregister_component_type({id(100)}),
            "schema use includes entity and relation owners");
    require(w.component_payload_count() == 1 && w.relation_payload_count() == 1,
            "distinct payload budgets");
    require(w.relations_for(id(1), world::RelationDirection::outgoing).size() == 2 &&
                w.relations_for(id(1), world::RelationDirection::incoming).size() == 2 &&
                w.relations_for(id(1)).size() == 3 &&
                w.relations_for(id(1), world::RelationDirection::both, id(50)).size() == 2,
            "endpoint index direction/kind filtering counts self once");
    const auto baseline = bytes(w);
    const auto revision = w.relation_revision();
    auto invalid = payload();
    invalid.fields[1].value = std::numeric_limits<double>::quiet_NaN();
    require(!w.set_relation_component(edge, invalid) &&
                !w.set_relation_component({id(1), id(52), id(2)}, payload()) &&
                bytes(w) == baseline && w.relation_revision() == revision,
            "invalid upsert changes neither state nor page token");
    auto broken = w.snapshot();
    broken.relations[0].components.push_back(payload());
    require(!w.restore(broken) && bytes(w) == baseline && w.relation_revision() == revision,
            "duplicate payload restore is transactional");
    broken = w.snapshot();
    broken.relations[0].object = id(999);
    require(!w.restore(broken) && bytes(w) == baseline,
            "missing persistent endpoint restore rejected");
    broken = w.snapshot();
    broken.component_types[0].version = 3;
    require(!w.restore(broken) && bytes(w) == baseline,
            "relation schema mismatch restore rejected");
    const auto source = tools::world_cooker::serialize_source_manifest(w.snapshot());
    const auto parsed = tools::world_cooker::parse_source_manifest(source);
    require(parsed.snapshot &&
                tools::world_cooker::serialize_source_manifest(*parsed.snapshot) == source,
            "canonical source preserves edge keys and data");
    auto permuted = w.snapshot();
    std::reverse(permuted.entities.begin(), permuted.entities.end());
    std::reverse(permuted.relations.begin(), permuted.relations.end());
    for (auto &r : permuted.relations)
        for (auto &p : r.components)
            std::reverse(p.fields.begin(), p.fields.end());
    world::World canonical;
    require(canonical.restore(permuted) && bytes(canonical) == baseline,
            "serialization independent of insertion and field order");
    const auto decoded = world::decode_world_snapshot(baseline);
    world::World resumed;
    require(decoded.snapshot && resumed.restore(*decoded.snapshot) && bytes(resumed) == baseline &&
                resumed.relation_revision() != revision &&
                resumed.relation_component(edge, {id(100)}) ==
                    w.relation_component(edge, {id(100)}),
            "restore rebuilds indexes and invalidates page tokens at equal counts");
    const auto projection = w.snapshot(world::ComponentPayloadSnapshot::omit);
    require(std::all_of(projection.relations.begin(), projection.relations.end(),
                        [](const auto &r) { return r.components.empty(); }) &&
                !world::encode_world_snapshot(projection),
            "render projection omits relation payloads and cannot be saved");
    tools::world_authoring::Transaction tx;
    auto changed = payload();
    changed.fields[2].value = std::int64_t{9};
    tx.operations = {tools::world_authoring::SetRelationComponent{edge, changed}};
    require(static_cast<bool>(tools::world_authoring::apply(w.snapshot(), tx)),
            "typed relation mutation");
    tx.operations.push_back(
        tools::world_authoring::SetRelationComponent{{id(2), id(52), id(4)}, payload()});
    require(!tools::world_authoring::apply(w.snapshot(), tx) && bytes(w) == baseline,
            "invalid second relation mutation rejects complete transaction");
    tx.operations = {
        tools::world_authoring::RemoveRelation{{edge.subject, edge.kind, edge.object, {}}}};
    const auto removed = tools::world_authoring::apply(w.snapshot(), tx);
    require(removed && removed.snapshot.relations.size() == 3,
            "remove relation matches key even when stateful");
    require(w.register_region({.id = {1},
                               .simulation_island = {1},
                               .origin = {1e12, 0, -1e12},
                               .fidelity = world::RegionFidelity::inactive}) &&
                w.set_spatial_state(*w.find(id(1)), {{1e12 + 3, 0, -1e12 - 4}, {1}}) &&
                w.clear_spatial_state(*w.find(id(1))) &&
                w.relation_component(edge, {id(100)}).has_value(),
            "relation state independent of large-world position/fidelity/spatial representation");
    require(w.remove_relation_component(edge, {id(100)}) &&
                w.remove_component(*w.find(id(3)), {id(100)}) &&
                w.unregister_component_type({id(100)}) && w.relation_payload_count() == 0,
            "schema index released across owner kinds");
    require(w.register_component_type(schema()) && w.set_relation_component(edge, payload()),
            "restore relation data");
    require(!w.unregister_component_type({id(100)}),
            "schema referenced only by an edge remains protected");
    require(w.register_region({.id = {2},
                               .simulation_island = {2},
                               .origin = {-1e12, 0, 1e12},
                               .fidelity = world::RegionFidelity::reduced}) &&
                w.set_spatial_state(*w.find(id(1)), {{-1e12 + 2, 0, 1e12 + 3}, {2}}) &&
                w.relation_component(edge, {id(100)}).has_value(),
            "region handoff retains global edge identity and payload");
    const auto stale = world::Relation{*w.find(id(1)), id(50), *w.find(id(2))};
    require(w.destroy_entity(*w.find(id(1))) && w.relations().size() == 1 &&
                w.has_relation(other) && w.relation_payload_count() == 0 &&
                w.relations_for(id(2)).empty() && w.unregister_component_type({id(100)}),
            "indexed endpoint deletion clears incoming/outgoing/self edges and data");
    require(w.create_entity(id(1)).has_value() && !w.add_relation(stale) &&
                !w.remove_relation(stale) && w.add_relation(edge) &&
                w.relation_components(edge).empty(),
            "stale handles rejected; recreation has no ghost payload");
    world::World old;
    require(old.register_component_type(schema()), "legacy schema");
    const auto legacy_owner = old.create_entity();
    require(old.set_component(legacy_owner, payload()), "legacy component");
    auto legacy = bytes(old);
    legacy.resize(legacy.size() - 54);
    legacy[8] = std::byte{16};
    rehash(legacy);
    const auto v16 = world::decode_world_snapshot(legacy);
    require(v16.snapshot && v16.snapshot->entities[0].components.size() == 1,
            "v16 component files remain readable");
    // Malformed relation state with a correct checksum must fail past the file envelope.
    world::World wire;
    require(wire.register_component_type(schema()), "wire schema");
    require(wire.create_entity(id(1)).has_value() && wire.create_entity(id(2)).has_value() &&
                wire.add_relation(edge) && wire.set_relation_component(edge, payload()),
            "wire relation");
    auto malformed = bytes(wire);
    std::ostringstream record;
    world::write_component_payload(record, *wire.relation_component(edge, {id(100)}));
    const auto table = malformed.size() - (30 + 4 + 48 + 4 + record.str().size());
    malformed[table + 4] ^= std::byte{1};
    rehash(malformed);
    require(!world::decode_world_snapshot(malformed),
            "unknown relation key record rejected with valid checksum");
    world::World stress;
    require(stress.register_component_type(
                {{id(100)}, "Counter", 1, {{1, world::ComponentValueKind::integer, "count"}}}),
            "stress schema");
    require(stress.create_entity(id(1)).has_value(), "stress hub");
    const auto start = std::chrono::steady_clock::now();
    for (unsigned n = 0; n < 30000; ++n) {
        require(stress.create_entity(id(1000 + n)).has_value(), "stress endpoint");
        const world::RelationKey key{id(1), id(50), id(1000 + n)};
        require(stress.add_relation(key) &&
                    stress.set_relation_component(key, {{id(100)}, 1, {{1, std::int64_t{n}}}}),
                "30k indexed edges and payloads");
    }
    const auto added = std::chrono::steady_clock::now();
    for (unsigned n = 0; n < 30000; ++n)
        require(stress.relation_component({id(1), id(50), id(1000 + n)}, {id(100)}).has_value(),
                "indexed state query");
    const auto queried = std::chrono::steady_clock::now();
    require(stress.relations_for(id(1)).size() == 30000, "ordered high-degree query");
    std::optional<world::RelationKey> cursor;
    std::size_t seen = 0, pages = 0;
    bool more = true;
    while (more) {
        const auto page =
            stress.relation_page(id(1), world::RelationDirection::outgoing, id(50), cursor, 64);
        require(page && page->keys.size() <= 64 && ++pages < 1000, "bounded cursor page");
        for (const auto key : page->keys) {
            require(!cursor || *cursor < key, "cursor order never repeats an edge");
            cursor = key;
            ++seen;
        }
        cursor = page->after;
        more = page->has_more;
    }
    require(seen == 30000, "cursor iteration covers 30k edges without offset rescans");
    const auto filtered =
        stress.relation_page(id(1), world::RelationDirection::both, id(999), {}, 64);
    require(filtered && filtered->keys.empty() && filtered->has_more && filtered->after,
            "filter scan budget allows empty intermediate pages with a progress cursor");
    const auto ordered = std::chrono::steady_clock::now();
    const auto render_projection = stress.snapshot(world::ComponentPayloadSnapshot::render);
    require(render_projection.relations.empty() && !render_projection.component_payloads_included &&
                !world::encode_world_snapshot(render_projection),
            "render projections omit the whole graph and cannot replace persistence state");
    const auto projected = std::chrono::steady_clock::now();
    const auto cooked = bytes(stress);
    const auto saved = std::chrono::steady_clock::now();
    require(stress.destroy_entity(*stress.find(id(1))) && stress.relations().empty() &&
                stress.relation_payload_count() == 0 && stress.unregister_component_type({id(100)}),
            "indexed bulk deletion releases all shared schema users");
    const auto deleted = std::chrono::steady_clock::now();
    const auto ms = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    std::cout << "{\"relations\":30000,\"payloads\":30000,\"key_bytes\":"
              << sizeof(world::RelationKey) << ",\"add_ms\":" << ms(start, added)
              << ",\"query_ms\":" << ms(added, queried)
              << ",\"ordered_query_ms\":" << ms(queried, ordered)
              << ",\"render_projection_ms\":" << ms(ordered, projected)
              << ",\"encode_ms\":" << ms(projected, saved)
              << ",\"delete_hub_ms\":" << ms(saved, deleted)
              << ",\"cooked_bytes\":" << cooked.size() << "}\n";
}
