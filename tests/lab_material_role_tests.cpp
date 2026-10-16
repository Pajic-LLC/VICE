#include "lab_materials.hpp"

#include <vice/world/world.hpp>

#include <cassert>
#include <string_view>

int main() {
    using namespace vice;

    const auto cube_id = world::PersistentId::parse(
        "10000000-0000-4000-8000-000000000002"
    );
    const auto cube_material_id = world::PersistentId::parse(
        "20000000-0000-4000-8000-000000000002"
    );
    const auto uses_material = world::PersistentId::parse(
        "30000000-0000-4000-8000-000000000001"
    );
    assert(cube_id.has_value());
    assert(cube_material_id.has_value());
    assert(uses_material.has_value());

    world::World old_world;
    const auto cube = old_world.create_entity(*cube_id);
    assert(cube.has_value());
    assert(old_world.entity_count() == 1U);
    assert(old_world.relations().empty());

    std::string diagnostics;
    assert(lab::ensure_material_roles(old_world, &diagnostics));
    assert(diagnostics.empty());
    assert(old_world.entity_count() == 6U);
    assert(old_world.relations().size() == 4U);

    world::World restored;
    assert(restored.restore(old_world.snapshot()));
    assert(restored.entity_count() == 6U);
    assert(restored.relations().size() == 4U);

    assert(lab::ensure_material_roles(restored, &diagnostics));
    assert(diagnostics.empty());
    assert(restored.entity_count() == 6U);
    assert(restored.relations().size() == 4U);

    const auto restored_cube = restored.find(*cube_id);
    assert(restored_cube.has_value());
    bool found_cube_material = false;
    for (const auto& relation : restored.relations()) {
        if (relation.subject != *restored_cube || relation.kind != *uses_material) continue;
        const auto material = restored.persistent_id(relation.object);
        assert(material.has_value());
        assert(*material == *cube_material_id);
        found_cube_material = true;
    }
    assert(found_cube_material);
    return 0;
}
