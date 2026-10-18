from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text()


def write(path: str, text: str) -> None:
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text)


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, got {count}: {old[:100]!r}")
    write(path, text.replace(old, new, 1))


def replace_count(path: str, old: str, new: str, expected: int) -> None:
    text = read(path)
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{path}: expected {expected} matches, got {count}: {old[:100]!r}")
    write(path, text.replace(old, new))


write(
    "engine/world/include/vice/world/light.hpp",
    r'''#pragma once

#include <cmath>
#include <cstdint>

namespace vice::world {

enum class LightType : std::uint8_t {
    directional = 1,
    point = 2,
};

struct LightColor {
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;

    friend bool operator==(const LightColor&, const LightColor&) = default;
};

struct Light {
    LightType type = LightType::directional;
    LightColor color{};
    float intensity = 1.0F;
    float range = 0.0F;
    bool enabled = true;

    friend bool operator==(const Light&, const Light&) = default;
};

[[nodiscard]] inline bool valid(const Light& light) noexcept {
    if (light.type != LightType::directional && light.type != LightType::point) return false;
    if (!std::isfinite(light.color.red) || !std::isfinite(light.color.green) ||
        !std::isfinite(light.color.blue) || light.color.red < 0.0F ||
        light.color.green < 0.0F || light.color.blue < 0.0F ||
        !std::isfinite(light.intensity) || light.intensity < 0.0F ||
        !std::isfinite(light.range) || light.range < 0.0F) {
        return false;
    }
    return light.type != LightType::point || light.range > 0.0F;
}

} // namespace vice::world
''',
)

replace_once(
    "engine/world/include/vice/world/world.hpp",
    "#include <vice/world/entity.hpp>\n",
    "#include <vice/world/entity.hpp>\n#include <vice/world/light.hpp>\n",
)
replace_once(
    "engine/world/include/vice/world/world.hpp",
    "    [[nodiscard]] std::optional<EntityId> primary_camera() const noexcept;\n\n",
    "    [[nodiscard]] std::optional<EntityId> primary_camera() const noexcept;\n\n"
    "    bool set_light(EntityId entity, Light light);\n"
    "    bool clear_light(EntityId entity);\n"
    "    [[nodiscard]] std::optional<Light> light(EntityId entity) const noexcept;\n\n",
)
replace_once(
    "engine/world/include/vice/world/world.hpp",
    "        std::optional<Camera> camera;\n        std::vector<ResourceReference> resource_references;\n",
    "        std::optional<Camera> camera;\n        std::optional<Light> light;\n"
    "        std::vector<ResourceReference> resource_references;\n",
)

replace_count(
    "engine/world/src/world.cpp",
    "    slot.camera.reset();\n    slot.resource_references.clear();\n",
    "    slot.camera.reset();\n    slot.light.reset();\n    slot.resource_references.clear();\n",
    2,
)
replace_once(
    "engine/world/src/world.cpp",
    "    slots_[entity.index].authored_transform.reset();\n"
    "    slots_[entity.index].camera.reset();\n    return true;\n",
    "    slots_[entity.index].authored_transform.reset();\n"
    "    slots_[entity.index].camera.reset();\n"
    "    slots_[entity.index].light.reset();\n    return true;\n",
)
replace_once(
    "engine/world/src/world.cpp",
    "std::optional<SpatialState> World::spatial_state(const EntityId entity) const noexcept {\n",
    '''bool World::set_light(const EntityId entity, const Light light_value) {
    if (!alive(entity) || !slots_[entity.index].spatial_state.has_value() || !valid(light_value)) {
        return false;
    }
    slots_[entity.index].light = light_value;
    return true;
}

bool World::clear_light(const EntityId entity) {
    if (!alive(entity) || !slots_[entity.index].light.has_value()) return false;
    slots_[entity.index].light.reset();
    return true;
}

std::optional<Light> World::light(const EntityId entity) const noexcept {
    if (!alive(entity)) return std::nullopt;
    return slots_[entity.index].light;
}

std::optional<SpatialState> World::spatial_state(const EntityId entity) const noexcept {
''',
)

replace_once(
    "engine/world/include/vice/world/snapshot.hpp",
    "#include <vice/world/camera.hpp>\n",
    "#include <vice/world/camera.hpp>\n#include <vice/world/light.hpp>\n",
)
replace_once(
    "engine/world/include/vice/world/snapshot.hpp",
    "    std::optional<Camera> camera;\n    std::vector<ResourceReference> resource_references;\n",
    "    std::optional<Camera> camera;\n    std::optional<Light> light;\n"
    "    std::vector<ResourceReference> resource_references;\n",
)
replace_once(
    "engine/world/src/primitives.cpp",
    "            .camera = slot.camera,\n            .resource_references = slot.resource_references,\n",
    "            .camera = slot.camera,\n            .light = slot.light,\n"
    "            .resource_references = slot.resource_references,\n",
)
replace_once(
    "engine/world/src/primitives.cpp",
    '''        if (entity_record.camera.has_value() &&
            !restored.set_camera(*entity, *entity_record.camera)) {
            return false;
        }
        for (const auto reference : entity_record.resource_references) {
''',
    '''        if (entity_record.camera.has_value() &&
            !restored.set_camera(*entity, *entity_record.camera)) {
            return false;
        }
        if (entity_record.light.has_value() &&
            !restored.set_light(*entity, *entity_record.light)) {
            return false;
        }
        for (const auto reference : entity_record.resource_references) {
''',
)

replace_once(
    "engine/world/include/vice/world/snapshot_file.hpp",
    "inline constexpr std::uint32_t world_snapshot_file_format_version = 4U;",
    "inline constexpr std::uint32_t world_snapshot_file_format_version = 5U;",
)

replace_once(
    "engine/world/src/snapshot_file.cpp",
    '''        append_u8(payload, entity.camera.has_value() ? 1U : 0U);
        if (entity.camera.has_value()) {
            append_u8(payload, static_cast<std::uint8_t>(entity.camera->projection));
            append_f32(payload, entity.camera->vertical_fov_radians);
            append_f32(payload, entity.camera->near_plane);
            append_f32(payload, entity.camera->far_plane);
            append_u8(payload, entity.camera->primary ? 1U : 0U);
        }
        append_u32(payload, static_cast<std::uint32_t>(entity.resource_references.size()));
''',
    '''        append_u8(payload, entity.camera.has_value() ? 1U : 0U);
        if (entity.camera.has_value()) {
            append_u8(payload, static_cast<std::uint8_t>(entity.camera->projection));
            append_f32(payload, entity.camera->vertical_fov_radians);
            append_f32(payload, entity.camera->near_plane);
            append_f32(payload, entity.camera->far_plane);
            append_u8(payload, entity.camera->primary ? 1U : 0U);
        }
        append_u8(payload, entity.light.has_value() ? 1U : 0U);
        if (entity.light.has_value()) {
            append_u8(payload, static_cast<std::uint8_t>(entity.light->type));
            append_f32(payload, entity.light->color.red);
            append_f32(payload, entity.light->color.green);
            append_f32(payload, entity.light->color.blue);
            append_f32(payload, entity.light->intensity);
            append_f32(payload, entity.light->range);
            append_u8(payload, entity.light->enabled ? 1U : 0U);
        }
        append_u32(payload, static_cast<std::uint32_t>(entity.resource_references.size()));
''',
)
replace_once(
    "engine/world/src/snapshot_file.cpp",
    '''        if (version >= 3U) {
            std::uint32_t reference_count = 0;
''',
    '''        if (version >= 5U) {
            std::uint8_t has_light = 0;
            if (!reader.read_u8(has_light)) {
                result.diagnostics = "World snapshot ended inside entity light metadata.";
                return result;
            }
            if (has_light > 1U) {
                result.diagnostics = "World snapshot contains invalid entity light metadata.";
                return result;
            }
            if (has_light != 0U) {
                std::uint8_t type = 0;
                std::uint8_t enabled = 0;
                Light light;
                if (!reader.read_u8(type) ||
                    !reader.read_f32(light.color.red) ||
                    !reader.read_f32(light.color.green) ||
                    !reader.read_f32(light.color.blue) ||
                    !reader.read_f32(light.intensity) ||
                    !reader.read_f32(light.range) ||
                    !reader.read_u8(enabled)) {
                    result.diagnostics = "World snapshot ended inside entity light record.";
                    return result;
                }
                light.type = static_cast<LightType>(type);
                light.enabled = enabled != 0U;
                if (enabled > 1U || !valid(light)) {
                    result.diagnostics = "World snapshot contains an invalid entity light record.";
                    return result;
                }
                entity.light = light;
            }
        }

        if (version >= 3U) {
            std::uint32_t reference_count = 0;
''',
)

replace_once(
    "tools/world_cooker/src/world_cooker.cpp",
    '''                if (metadata == "resource") {
''',
    '''                if (metadata == "light") {
                    if (entity.light.has_value()) {
                        fail("entity light is duplicated.");
                        return result;
                    }
                    if (!entity.spatial_state.has_value()) {
                        fail("light requires a spatial entity.");
                        return result;
                    }
                    std::string type_text;
                    std::string enabled_text;
                    world::Light light;
                    if (!(values >> type_text >> light.color.red >> light.color.green >>
                            light.color.blue >> light.intensity)) {
                        fail("entity light is incomplete.");
                        return result;
                    }
                    if (type_text == "directional") {
                        light.type = world::LightType::directional;
                        light.range = 0.0F;
                    } else if (type_text == "point") {
                        light.type = world::LightType::point;
                        if (!(values >> light.range)) {
                            fail("point light range is missing.");
                            return result;
                        }
                    } else {
                        fail("light type must be `directional` or `point`.");
                        return result;
                    }
                    if (!(values >> enabled_text)) {
                        fail("light enabled state is missing.");
                        return result;
                    }
                    if (enabled_text == "enabled") light.enabled = true;
                    else if (enabled_text == "disabled") light.enabled = false;
                    else {
                        fail("light state must be `enabled` or `disabled`.");
                        return result;
                    }
                    if (!world::valid(light)) {
                        fail("entity light values are invalid.");
                        return result;
                    }
                    entity.light = light;
                    continue;
                }

                if (metadata == "resource") {
''',
)

replace_once(
    "apps/vice_editor/src/project_session.cpp",
    '        "entity a0000000-0000-4000-8000-000000000001 spatial 0 3 8 1 camera perspective 60 0.05 1000 primary\\n";\n',
    '        "entity a0000000-0000-4000-8000-000000000001 spatial 0 3 8 1 camera perspective 60 0.05 1000 primary\\n"\n'
    '        "entity a0000000-0000-4000-8000-000000000002 spatial 0 6 4 1 transform -0.25881905 0 0 0.96592583 1 1 1 light directional 1 0.95 0.9 3 enabled\\n";\n',
)

write(
    "tests/world_light_tests.cpp",
    r'''#include <vice/world/world.hpp>

#include <cassert>
#include <limits>
#include <optional>

int main() {
    using namespace vice::world;

    World world;
    assert(world.register_region(RegionDesc{
        .id = RegionId{1U},
        .simulation_island = SimulationIslandId{1U},
        .bounds = RegionBounds{
            .min = {-20.0F, -20.0F, -20.0F},
            .max = {20.0F, 20.0F, 20.0F},
        },
    }));

    const auto entity = world.create_entity();
    assert(!world.set_light(entity, Light{}));
    assert(world.set_spatial_state(entity, SpatialState{.global_position = {}, .region = RegionId{1U}}));

    Light directional;
    directional.type = LightType::directional;
    directional.color = {1.0F, 0.5F, 0.25F};
    directional.intensity = 3.0F;
    assert(world.set_light(entity, directional));
    assert(world.light(entity) == std::optional{directional});

    Light invalid = directional;
    invalid.intensity = std::numeric_limits<float>::quiet_NaN();
    assert(!world.set_light(entity, invalid));
    invalid = directional;
    invalid.type = LightType::point;
    invalid.range = 0.0F;
    assert(!world.set_light(entity, invalid));

    Light point = directional;
    point.type = LightType::point;
    point.range = 12.0F;
    assert(world.set_light(entity, point));
    assert(world.light(entity) == std::optional{point});

    const auto snapshot = world.snapshot();
    assert(snapshot.entities.size() == 1U);
    assert(snapshot.entities[0].light == std::optional{point});
    World restored;
    assert(restored.restore(snapshot));
    const auto restored_entity = restored.find(snapshot.entities[0].persistent_id);
    assert(restored_entity.has_value());
    assert(restored.light(*restored_entity) == std::optional{point});

    assert(world.clear_spatial_state(entity));
    assert(!world.light(entity).has_value());
    return 0;
}
''',
)
replace_once(
    "tests/CMakeLists.txt",
    "add_test(NAME vice_world_tests COMMAND vice_world_tests)\n\n",
    "add_test(NAME vice_world_tests COMMAND vice_world_tests)\n\n"
    "add_executable(vice_world_light_tests world_light_tests.cpp)\n"
    "target_link_libraries(vice_world_light_tests PRIVATE VICE::World)\n"
    "target_compile_features(vice_world_light_tests PRIVATE cxx_std_23)\n"
    "vice_enable_warnings(vice_world_light_tests)\n"
    "add_test(NAME vice_world_light_tests COMMAND vice_world_light_tests)\n\n",
)

path = "tests/world_snapshot_file_tests.cpp"
text = read(path)
old = '''    if (version >= 3U) {
        append_u32(payload, 0U);
    }
'''
new = '''    if (version >= 4U) {
        append_u8(payload, 1U);
        append_u8(payload, static_cast<std::uint8_t>(CameraProjection::perspective));
        append_f32(payload, 1.0471975512F);
        append_f32(payload, 0.05F);
        append_f32(payload, 500.0F);
        append_u8(payload, 1U);
    }
    if (version >= 3U) {
        append_u32(payload, 0U);
    }
'''
if text.count(old) != 1:
    raise SystemExit("legacy snapshot anchor missing")
text = text.replace(old, new, 1)
text = text.replace(
    "entity 10000000-0000-4000-8000-000000000001 spatial 5 1 0 1 camera perspective 72 0.1 750 primary\\n",
    "entity 10000000-0000-4000-8000-000000000001 spatial 5 1 0 1 camera perspective 72 0.1 750 primary light directional 1 0.9 0.8 2.5 enabled\\n",
    1,
)
text = text.replace(
    "    assert(parsed.snapshot->entities[0].camera->primary);\n",
    "    assert(parsed.snapshot->entities[0].camera->primary);\n"
    "    assert(parsed.snapshot->entities[0].light.has_value());\n"
    "    assert(parsed.snapshot->entities[0].light->type == world::LightType::directional);\n"
    "    assert(parsed.snapshot->entities[0].light->intensity == 2.5F);\n",
    1,
)
text = text.replace(
    "    assert(restored.primary_camera() == player);\n",
    "    assert(restored.primary_camera() == player);\n"
    "    assert(restored.light(*player).has_value());\n"
    "    assert(restored.light(*player)->color.green == 0.9F);\n",
    1,
)
text = text.replace(
    "    for (const std::uint32_t legacy_version : {1U, 2U, 3U}) {\n",
    "    for (const std::uint32_t legacy_version : {1U, 2U, 3U, 4U}) {\n",
    1,
)
text = text.replace(
    "        assert(!legacy.snapshot->entities[0].camera.has_value());\n",
    "        if (legacy_version >= 4U) {\n"
    "            assert(legacy.snapshot->entities[0].camera.has_value());\n"
    "            assert(legacy.snapshot->entities[0].camera->primary);\n"
    "        } else {\n"
    "            assert(!legacy.snapshot->entities[0].camera.has_value());\n"
    "        }\n"
    "        assert(!legacy.snapshot->entities[0].light.has_value());\n",
    1,
)
anchor = '    constexpr std::string_view duplicate_primary_camera = R"(\n'
if anchor not in text:
    raise SystemExit("invalid-light test anchor missing")
text = text.replace(
    anchor,
    '''    constexpr std::string_view invalid_light = R"(
vice-world-source 1
region 1 1 0 0 0 0 0 0 full none
entity 10000000-0000-4000-8000-000000000001 spatial 0 0 0 1 light point 1 1 1 2 0 enabled
)";
    assert(!tools::world_cooker::parse_source_manifest(invalid_light).snapshot.has_value());

    constexpr std::string_view duplicate_light = R"(
vice-world-source 1
region 1 1 0 0 0 0 0 0 full none
entity 10000000-0000-4000-8000-000000000001 spatial 0 0 0 1 light directional 1 1 1 1 enabled light directional 1 1 1 1 enabled
)";
    assert(!tools::world_cooker::parse_source_manifest(duplicate_light).snapshot.has_value());

    constexpr std::string_view duplicate_primary_camera = R"(
''',
    1,
)
write(path, text)
