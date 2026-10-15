#include <vice/world/persistent_id.hpp>
#include <vice/world/relation.hpp>
#include <vice/world/world.hpp>

#include <array>
#include <cassert>
#include <string_view>

namespace {

vice::world::PersistentId fixed_id(const std::uint8_t tail) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[15] = tail;
    return vice::world::PersistentId{bytes};
}

void test_persistent_id_round_trip() {
    using vice::world::PersistentId;

    const auto id = PersistentId::random();
    assert(!id.is_nil());

    const auto text = id.to_string();
    assert(text.size() == 36);

    const auto parsed = PersistentId::parse(text);
    assert(parsed.has_value());
    assert(*parsed == id);
    assert(!PersistentId::parse("not-an-id").has_value());
}

void test_entity_lifetime_and_stale_handles() {
    vice::world::World world;

    const auto first = world.create_entity();
    assert(world.alive(first));
    assert(world.entity_count() == 1);

    assert(world.destroy_entity(first));
    assert(!world.alive(first));
    assert(world.entity_count() == 0);
    assert(!world.destroy_entity(first));

    const auto second = world.create_entity();
    assert(second.index == first.index);
    assert(second.generation != first.generation);
    assert(world.alive(second));
}

void test_persistent_identity_restore() {
    vice::world::World first_world;
    const auto id = fixed_id(7);

    const auto first_entity = first_world.create_entity(id);
    assert(first_entity.has_value());
    assert(!first_world.create_entity(id).has_value());

    const auto serialized = id.to_string();
    const auto restored_id = vice::world::PersistentId::parse(serialized);
    assert(restored_id.has_value());

    vice::world::World second_world;
    const auto second_entity = second_world.create_entity(*restored_id);
    assert(second_entity.has_value());
    assert(second_world.persistent_id(*second_entity) == restored_id);
    assert(second_world.find(*restored_id) == second_entity);
}

void test_relations() {
    vice::world::World world;
    const auto parent = world.create_entity();
    const auto child = world.create_entity();
    const auto relation_kind = fixed_id(42);

    const vice::world::Relation relation{child, relation_kind, parent};
    assert(world.add_relation(relation));
    assert(!world.add_relation(relation));
    assert(world.relations().size() == 1);

    assert(world.destroy_entity(parent));
    assert(world.relations().empty());
}

} // namespace

int main() {
    test_persistent_id_round_trip();
    test_entity_lifetime_and_stale_handles();
    test_persistent_identity_restore();
    test_relations();
    return 0;
}
