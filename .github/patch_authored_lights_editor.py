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
        raise SystemExit(f"{path}: expected one match, got {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


# Editor state model and history.
replace_once(
    "apps/vice_editor/include/vice/editor/state.hpp",
    "#include <vice/world/entity.hpp>\n",
    "#include <vice/world/entity.hpp>\n#include <vice/world/light.hpp>\n",
)
replace_once(
    "apps/vice_editor/include/vice/editor/state.hpp",
    '''bool apply_camera_states(
    world::World& world,
    std::span<const EntityCameraState> states
);

''',
    '''bool apply_camera_states(
    world::World& world,
    std::span<const EntityCameraState> states
);

struct EntityLightState {
    world::PersistentId persistent_id{};
    std::optional<world::Light> light;

    friend bool operator==(const EntityLightState&, const EntityLightState&) = default;
};

[[nodiscard]] std::optional<EntityLightState> capture_entity_light(
    const world::World& world,
    world::PersistentId persistent_id
);

bool apply_entity_light(
    world::World& world,
    const EntityLightState& state
);

''',
)
replace_once(
    "apps/vice_editor/include/vice/editor/state.hpp",
    '''    struct CreateEntityEntry {
''',
    '''    struct LightEntry {
        EntityLightState before{};
        EntityLightState after{};
        std::string label;
    };

    struct CreateEntityEntry {
''',
)
replace_once(
    "apps/vice_editor/include/vice/editor/state.hpp",
    "    using Entry = std::variant<TransformEntry, CameraEntry, CreateEntityEntry>;\n\n"
    "    bool record(TransformEntry entry);\n    bool record(CameraEntry entry);\n",
    "    using Entry = std::variant<TransformEntry, CameraEntry, LightEntry, CreateEntityEntry>;\n\n"
    "    bool record(TransformEntry entry);\n    bool record(CameraEntry entry);\n    bool record(LightEntry entry);\n",
)
replace_once(
    "apps/vice_editor/include/vice/editor/state.hpp",
    '''struct HierarchyEntry {
    world::PersistentId persistent_id{};
    std::optional<world::SpatialState> spatial_state;
    std::optional<world::Camera> camera;
};
''',
    '''struct HierarchyEntry {
    world::PersistentId persistent_id{};
    std::optional<world::SpatialState> spatial_state;
    std::optional<world::Camera> camera;
    std::optional<world::Light> light;
};
''',
)
replace_once(
    "apps/vice_editor/include/vice/editor/state.hpp",
    "    std::optional<world::Camera> camera;\n    std::vector<world::ResourceReference> resource_references;\n",
    "    std::optional<world::Camera> camera;\n    std::optional<world::Light> light;\n"
    "    std::vector<world::ResourceReference> resource_references;\n",
)

replace_once(
    "apps/vice_editor/src/state.cpp",
    '''    if (command.entity.camera.has_value() &&
        !world.set_camera(*entity, *command.entity.camera)) {
        rollback_entity();
        return false;
    }
    for (const auto reference : command.entity.resource_references) {
''',
    '''    if (command.entity.camera.has_value() &&
        !world.set_camera(*entity, *command.entity.camera)) {
        rollback_entity();
        return false;
    }
    if (command.entity.light.has_value() &&
        !world.set_light(*entity, *command.entity.light)) {
        rollback_entity();
        return false;
    }
    for (const auto reference : command.entity.resource_references) {
''',
)
replace_once(
    "apps/vice_editor/src/state.cpp",
    '''bool CommandHistory::record(TransformEntry entry) {
''',
    '''std::optional<EntityLightState> capture_entity_light(
    const world::World& world,
    const world::PersistentId persistent_id
) {
    const auto entity = world.find(persistent_id);
    if (!entity.has_value()) return std::nullopt;
    return EntityLightState{
        .persistent_id = persistent_id,
        .light = world.light(*entity),
    };
}

bool apply_entity_light(world::World& world, const EntityLightState& state) {
    if (state.persistent_id.is_nil()) return false;
    const auto entity = world.find(state.persistent_id);
    if (!entity.has_value()) return false;
    const auto previous = world.light(*entity);
    if (previous.has_value() && !world.clear_light(*entity)) return false;
    if (!state.light.has_value()) return true;
    if (world.set_light(*entity, *state.light)) return true;
    if (previous.has_value()) (void)world.set_light(*entity, *previous);
    return false;
}

bool CommandHistory::record(TransformEntry entry) {
''',
)
replace_once(
    "apps/vice_editor/src/state.cpp",
    '''bool CommandHistory::record(CameraEntry entry) {
    if (entry.before.empty() || entry.before == entry.after) return false;
    return record_entry(Entry{std::move(entry)});
}

''',
    '''bool CommandHistory::record(CameraEntry entry) {
    if (entry.before.empty() || entry.before == entry.after) return false;
    return record_entry(Entry{std::move(entry)});
}

bool CommandHistory::record(LightEntry entry) {
    if (entry.before.persistent_id.is_nil() || entry.before.persistent_id != entry.after.persistent_id ||
        entry.before == entry.after) {
        return false;
    }
    return record_entry(Entry{std::move(entry)});
}

''',
)
replace_once(
    "apps/vice_editor/src/state.cpp",
    '''            } else if constexpr (std::is_same_v<Command, CameraEntry>) {
                return apply_camera_states(world, command.before);
            } else {
''',
    '''            } else if constexpr (std::is_same_v<Command, CameraEntry>) {
                return apply_camera_states(world, command.before);
            } else if constexpr (std::is_same_v<Command, LightEntry>) {
                return apply_entity_light(world, command.before);
            } else {
''',
)
replace_once(
    "apps/vice_editor/src/state.cpp",
    '''            } else if constexpr (std::is_same_v<Command, CameraEntry>) {
                return apply_camera_states(world, command.after);
            } else {
''',
    '''            } else if constexpr (std::is_same_v<Command, CameraEntry>) {
                return apply_camera_states(world, command.after);
            } else if constexpr (std::is_same_v<Command, LightEntry>) {
                return apply_entity_light(world, command.after);
            } else {
''',
)
replace_once(
    "apps/vice_editor/src/state.cpp",
    '''            .camera = entity.camera,
        });
''',
    '''            .camera = entity.camera,
            .light = entity.light,
        });
''',
)
replace_once(
    "apps/vice_editor/src/state.cpp",
    '''    inspector.camera = world.camera(*entity);
    const auto references = world.resource_references(*entity);
''',
    '''    inspector.camera = world.camera(*entity);
    inspector.light = world.light(*entity);
    const auto references = world.resource_references(*entity);
''',
)

# UI state and Hierarchy tag.
replace_once(
    "apps/vice_editor/src/ui.cpp",
    '''    std::optional<world::PersistentId> inspector_camera_edit_id;
    std::optional<world::Camera> inspector_camera_edit_before;
    std::string inspector_camera_edit_label;
''',
    '''    std::optional<world::PersistentId> inspector_camera_edit_id;
    std::optional<world::Camera> inspector_camera_edit_before;
    std::string inspector_camera_edit_label;
    std::optional<world::PersistentId> inspector_light_edit_id;
    std::optional<world::Light> inspector_light_edit_before;
    std::string inspector_light_edit_label;
''',
)
replace_once(
    "apps/vice_editor/src/ui.cpp",
    '''                if (entry.camera.has_value()) {
                    label += entry.camera->primary ? "   Primary Camera" : "   Camera";
                }
                label += "###" + full_id;
''',
    '''                if (entry.camera.has_value()) {
                    label += entry.camera->primary ? "   Primary Camera" : "   Camera";
                }
                if (entry.light.has_value()) {
                    label += entry.light->type == world::LightType::directional
                        ? "   Directional Light"
                        : "   Point Light";
                }
                label += "###" + full_id;
''',
)

# Inspector light block after camera block.
anchor = '''                if (ImGui::CollapsingHeader("Runtime", ImGuiTreeNodeFlags_DefaultOpen)) {
'''
light_block = r'''                if (inspector->light.has_value() &&
                    ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const auto begin_light_transaction = [&](const char* label) {
                        if (ImGui::IsItemActivated()) {
                            state_->inspector_light_edit_id = inspector->persistent_id;
                            state_->inspector_light_edit_before = world.light(*inspector->entity);
                            state_->inspector_light_edit_label = label;
                        }
                    };
                    const auto finish_light_transaction = [&]() {
                        if (!ImGui::IsItemDeactivatedAfterEdit() ||
                            !state_->inspector_light_edit_id.has_value() ||
                            !state_->inspector_light_edit_before.has_value() ||
                            *state_->inspector_light_edit_id != inspector->persistent_id) {
                            return;
                        }
                        const auto after = capture_entity_light(world, inspector->persistent_id);
                        if (after.has_value()) {
                            history.record(CommandHistory::LightEntry{
                                .before = {
                                    .persistent_id = inspector->persistent_id,
                                    .light = *state_->inspector_light_edit_before,
                                },
                                .after = *after,
                                .label = state_->inspector_light_edit_label,
                            });
                        }
                        state_->inspector_light_edit_id.reset();
                        state_->inspector_light_edit_before.reset();
                        state_->inspector_light_edit_label.clear();
                    };
                    const auto apply_light = [&](const world::Light& light, const char* error) {
                        if (!world.set_light(*inspector->entity, light)) {
                            core::log(core::LogLevel::error, error);
                            return false;
                        }
                        return true;
                    };

                    auto current_light = world.light(*inspector->entity);
                    if (current_light.has_value()) {
                        const auto before_type = capture_entity_light(world, inspector->persistent_id);
                        int type = current_light->type == world::LightType::directional ? 0 : 1;
                        const char* type_names[] = {"Directional", "Point"};
                        if (ImGui::Combo("Type", &type, type_names, 2)) {
                            auto changed = *current_light;
                            changed.type = type == 0 ? world::LightType::directional : world::LightType::point;
                            changed.range = changed.type == world::LightType::point
                                ? std::max(changed.range, 10.0F)
                                : 0.0F;
                            if (before_type.has_value() && apply_light(changed, "VICE Editor could not change light type.")) {
                                const auto after = capture_entity_light(world, inspector->persistent_id);
                                if (after.has_value()) {
                                    history.record(CommandHistory::LightEntry{
                                        .before = *before_type,
                                        .after = *after,
                                        .label = "Change Light Type",
                                    });
                                }
                            }
                        }

                        current_light = world.light(*inspector->entity);
                        const auto before_enabled = capture_entity_light(world, inspector->persistent_id);
                        bool enabled = current_light->enabled;
                        if (ImGui::Checkbox("Enabled", &enabled)) {
                            auto changed = *current_light;
                            changed.enabled = enabled;
                            if (before_enabled.has_value() && apply_light(changed, "VICE Editor could not change light enabled state.")) {
                                const auto after = capture_entity_light(world, inspector->persistent_id);
                                if (after.has_value()) {
                                    history.record(CommandHistory::LightEntry{
                                        .before = *before_enabled,
                                        .after = *after,
                                        .label = enabled ? "Enable Light" : "Disable Light",
                                    });
                                }
                            }
                        }

                        current_light = world.light(*inspector->entity);
                        float color[3]{
                            current_light->color.red,
                            current_light->color.green,
                            current_light->color.blue,
                        };
                        const bool color_changed = ImGui::ColorEdit3("Color", color);
                        begin_light_transaction("Edit Light Color");
                        if (color_changed) {
                            auto changed = *current_light;
                            changed.color = {color[0], color[1], color[2]};
                            (void)apply_light(changed, "VICE Editor could not apply light color edit.");
                        }
                        finish_light_transaction();

                        current_light = world.light(*inspector->entity);
                        float intensity = current_light->intensity;
                        const bool intensity_changed = ImGui::DragFloat(
                            "Intensity",
                            &intensity,
                            0.05F,
                            0.0F,
                            100000.0F,
                            "%.2f",
                            ImGuiSliderFlags_AlwaysClamp
                        );
                        begin_light_transaction("Edit Light Intensity");
                        if (intensity_changed) {
                            auto changed = *current_light;
                            changed.intensity = intensity;
                            (void)apply_light(changed, "VICE Editor could not apply light intensity edit.");
                        }
                        finish_light_transaction();

                        current_light = world.light(*inspector->entity);
                        if (current_light->type == world::LightType::point) {
                            float range = current_light->range;
                            const bool range_changed = ImGui::DragFloat(
                                "Range",
                                &range,
                                0.1F,
                                0.01F,
                                100000.0F,
                                "%.2f",
                                ImGuiSliderFlags_AlwaysClamp
                            );
                            begin_light_transaction("Edit Light Range");
                            if (range_changed) {
                                auto changed = *current_light;
                                changed.range = range;
                                (void)apply_light(changed, "VICE Editor could not apply light range edit.");
                            }
                            finish_light_transaction();
                        } else {
                            ImGui::TextDisabled("Direction follows the entity rotation (-Z forward).");
                        }
                    }
                }

'''
replace_once("apps/vice_editor/src/ui.cpp", anchor, light_block + anchor)

# Focused state/history regression.
write(
    "tests/editor_light_state_tests.cpp",
    r'''#include <vice/editor/state.hpp>
#include <vice/world/world.hpp>

#include <cassert>
#include <optional>

int main() {
    using namespace vice;

    world::World world;
    assert(world.register_region(world::RegionDesc{
        .id = world::RegionId{1U},
        .simulation_island = world::SimulationIslandId{1U},
        .bounds = world::RegionBounds{
            .min = {-20.0F, -20.0F, -20.0F},
            .max = {20.0F, 20.0F, 20.0F},
        },
    }));
    const auto id = world::PersistentId::parse("61000000-0000-4000-8000-000000000001");
    assert(id.has_value());
    const auto entity = world.create_entity(*id);
    assert(entity.has_value());
    assert(world.set_spatial_state(*entity, world::SpatialState{
        .global_position = {1.0, 2.0, 3.0},
        .region = world::RegionId{1U},
    }));

    world::Light before_light;
    before_light.type = world::LightType::directional;
    before_light.intensity = 2.0F;
    assert(world.set_light(*entity, before_light));
    const auto before = editor::capture_entity_light(world, *id);
    assert(before.has_value());

    world::Light after_light;
    after_light.type = world::LightType::point;
    after_light.color = {0.5F, 0.6F, 0.7F};
    after_light.intensity = 7.0F;
    after_light.range = 18.0F;
    assert(world.set_light(*entity, after_light));
    const auto after = editor::capture_entity_light(world, *id);
    assert(after.has_value());

    editor::CommandHistory history;
    assert(history.record(editor::CommandHistory::LightEntry{
        .before = *before,
        .after = *after,
        .label = "Edit Light",
    }));
    assert(history.undo(world));
    assert(world.light(*entity) == std::optional{before_light});
    assert(history.redo(world));
    assert(world.light(*entity) == std::optional{after_light});

    const auto hierarchy = editor::build_hierarchy(world);
    assert(hierarchy.size() == 1U);
    assert(hierarchy.front().light == std::optional{after_light});

    const auto inspector = editor::project_inspector(world, *id);
    assert(inspector.has_value());
    assert(inspector->light == std::optional{after_light});
    return 0;
}
''',
)
replace_once(
    "tests/editor/CMakeLists.txt",
    '''add_test(NAME vice_editor_camera_state_tests COMMAND vice_editor_camera_state_tests)

''',
    '''add_test(NAME vice_editor_camera_state_tests COMMAND vice_editor_camera_state_tests)

add_executable(
  vice_editor_light_state_tests
  ../editor_light_state_tests.cpp
)
target_link_libraries(vice_editor_light_state_tests PRIVATE vice_editor_state)
target_compile_features(vice_editor_light_state_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_editor_light_state_tests)
add_test(NAME vice_editor_light_state_tests COMMAND vice_editor_light_state_tests)

''',
)
