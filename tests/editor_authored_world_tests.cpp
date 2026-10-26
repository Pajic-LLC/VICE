#include <vice/editor/project_build.hpp>
#include <vice/editor/project_session.hpp>
#include <vice/editor/project_settings.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/tools/world_edit.hpp>
#include <vice/platform/file.hpp>
#include <vice/world/world.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "editor authored World test failed: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] vice::world::PersistentId id(const std::string_view value) {
    const auto parsed = vice::world::PersistentId::parse(value);
    require(parsed.has_value(), "persistent ID parse");
    return *parsed;
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        require(!error, "text parent directory");
    }
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "text file open");
    output << text;
    require(static_cast<bool>(output), "text file write");
}

[[nodiscard]] std::filesystem::path temp_root() {
    std::error_code error;
    const auto temp = std::filesystem::temp_directory_path(error);
    require(!error, "temporary directory");
    const auto root = temp / "vice_editor_authored_world_current_test_45000000";
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root / "project" / "assets" / "worlds", error);
    require(!error, "temporary project tree");
    return root;
}

[[nodiscard]] vice::world::World build_authored_world() {
    using namespace vice;
    world::World value;

    const world::ComponentTypeId component{id("45000000-0000-4000-8000-000000000101")};
    const world::ResourceId mesh{id("45000000-0000-4000-8000-000000000102")};
    const world::ResourceId material{id("45000000-0000-4000-8000-000000000103")};
    const world::ResourceId environment{id("45000000-0000-4000-8000-000000000104")};
    const auto subject_id = id("45000000-0000-4000-8000-000000000105");
    const auto object_id = id("45000000-0000-4000-8000-000000000106");
    const auto relation_kind = id("45000000-0000-4000-8000-000000000107");

    require(value.register_component_type(world::ComponentTypeDesc{
        .id = component,
        .name = "AuthoredGameplay",
        .version = 3U,
    }), "component registration");
    require(value.create_resource(mesh).has_value(), "mesh resource registration");
    require(value.create_resource(material).has_value(), "material resource registration");
    require(value.create_resource(environment).has_value(), "environment resource registration");
    require(value.register_region(world::RegionDesc{
        .id = world::RegionId{1},
        .simulation_island = world::SimulationIslandId{7},
        .origin = {1.0e12 + 3.0, -2.0e12 + 4.0, 3.0e12 - 5.0},
        .streaming_cell = {11, -12, 13},
        .fidelity = world::RegionFidelity::full,
        .bounds = world::RegionBounds{
            .min = {-30.0F, -20.0F, -10.0F},
            .max = {30.0F, 20.0F, 10.0F},
        },
    }), "region registration");

    const auto subject = value.create_entity(subject_id);
    const auto object = value.create_entity(object_id);
    require(subject.has_value() && object.has_value(), "entity creation");
    require(value.set_spatial_state(*subject, world::SpatialState{
        .global_position = {1.0e12 + 4.25, -2.0e12 + 5.5, 3.0e12 - 6.75},
        .region = world::RegionId{1},
    }), "subject spatial state");
    require(value.set_spatial_state(*object, world::SpatialState{
        .global_position = {1.0e12 + 8.0, -2.0e12 + 5.0, 3.0e12 - 4.0},
        .region = world::RegionId{1},
    }), "object spatial state");
    require(value.set_authored_transform(*subject, world::AuthoredTransform{
        .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
        .scale = {1.5F, 2.0F, 0.75F},
    }), "authored transform");
    require(value.set_camera(*subject, world::Camera{
        .projection = world::CameraProjection::perspective,
        .vertical_fov_radians = 1.0471975512F,
        .near_plane = 0.05F,
        .far_plane = 5000.0F,
        .primary = true,
    }), "camera");
    require(value.set_light(*subject, world::Light{
        .type = world::LightType::point,
        .color = {0.8F, 0.65F, 0.5F},
        .intensity = 1700.0F,
        .range = 42.0F,
        .enabled = true,
    }), "light");
    require(value.set_reflection_probe(*subject, world::ReflectionProbe{
        .half_extents = {9.0F, 7.0F, 5.0F},
        .blend_distance = 1.25F,
        .priority = 4,
        .enabled = true,
    }), "reflection probe");
    require(value.set_resource_reference(*subject, world::ResourceReference{
        .role = world::ResourceRole::render_mesh,
        .resource = mesh,
    }), "mesh reference");
    require(value.set_resource_reference(*subject, world::ResourceReference{
        .role = world::ResourceRole::render_material,
        .resource = material,
    }), "material reference");
    require(value.set_resource_reference(*subject, world::ResourceReference{
        .role = world::ResourceRole::reflection_environment,
        .resource = environment,
    }), "reflection environment reference");
    require(value.add_relation(world::Relation{
        .subject = *subject,
        .kind = relation_kind,
        .object = *object,
    }), "relation");
    return value;
}

} // namespace

int main() {
    using namespace vice;

    const auto root = temp_root();
    const auto project_root = root / "project";
    const auto asset_root = project_root / "assets";
    const auto manifest_path = project_root / "vice.viceproj";
    const auto main_world = asset_root / "worlds" / "main.vworld";
    const auto saved_as_world = asset_root / "worlds" / "saved-as.vworld";

    auto authored = build_authored_world();
    std::string diagnostics;
    const auto source = tools::world_cooker::serialize_source_manifest(authored.snapshot(), &diagnostics);
    require(!source.empty(), "World source serialization");
    require(diagnostics.empty(), "World source serialization diagnostics");
    const auto parsed = tools::world_cooker::parse_source_manifest(source);
    require(parsed.snapshot.has_value(), "serialized World reparses");
    require(parsed.diagnostics.empty(), "serialized World parse diagnostics");
    const auto canonical_again = tools::world_cooker::serialize_source_manifest(*parsed.snapshot, &diagnostics);
    require(canonical_again == source, "World source serialization is deterministic");
    require(source.find(" camera perspective ") != std::string::npos, "camera serialized");
    require(source.find(" light point ") != std::string::npos, "light serialized");
    require(source.find(" reflection_probe ") != std::string::npos, "reflection probe serialized");
    require(source.find(" resource reflection_environment ") != std::string::npos, "reflection environment serialized");

    write_text(
        manifest_path,
        "vice-project 1\n"
        "id 45000000-0000-4000-8000-000000000135\n"
        "name \"Authored Test\"\n"
        "game_name \"Authored Test Game\"\n"
        "game_target \"authored-test\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    write_text(main_world, source);

    auto session = editor::ProjectSession::open(
        manifest_path,
        root / "cache",
        root / "state",
        &diagnostics
    );
    require(session.has_value(), "project session opens");
    require(diagnostics.empty(), "project session diagnostics");

    require(
        editor::resolve_project_authored_world_path(
            *session,
            std::filesystem::path{"worlds/main.vworld"},
            true,
            &diagnostics
        ) == std::optional<std::filesystem::path>{std::filesystem::weakly_canonical(main_world)},
        "project-relative authored path resolves"
    );
    require(
        !editor::resolve_project_authored_world_path(
            *session,
            std::filesystem::path{"../outside.vworld"},
            false,
            &diagnostics
        ).has_value(),
        "parent traversal rejected"
    );
    require(!diagnostics.empty(), "parent traversal diagnostic");
    require(
        !editor::resolve_project_authored_world_path(
            *session,
            root / "outside.vworld",
            false,
            &diagnostics
        ).has_value(),
        "absolute outside path rejected"
    );
    require(
        !editor::resolve_project_authored_world_path(
            *session,
            std::filesystem::path{"worlds/main.vworldc"},
            false,
            &diagnostics
        ).has_value(),
        "cooked extension rejected"
    );
    require(
        !editor::resolve_project_authored_world_path(
            *session,
            std::filesystem::path{"worlds/missing.vworld"},
            true,
            &diagnostics
        ).has_value(),
        "missing Open target rejected"
    );

    editor::WorldDocument document;
    require(document.open_authored(main_world, editor::DirtyReplacePolicy::discard, &diagnostics), "open authored World");
    require(document.authored(), "document is authored");
    const auto subject_id = id("45000000-0000-4000-8000-000000000105");
    const auto subject = document.world().find(subject_id);
    require(subject.has_value(), "authored entity reopens");
    const auto probe = document.world().reflection_probe(*subject);
    require(probe.has_value() && probe->priority == 4, "reflection probe round trips");
    const auto camera = document.world().camera(*subject);
    require(camera.has_value() && camera->primary, "camera round trips");
    const auto light = document.world().light(*subject);
    require(light.has_value() && light->type == world::LightType::point, "light round trips");
    require(
        document.world().resource_reference(*subject, world::ResourceRole::reflection_environment).has_value(),
        "reflection environment round trips"
    );

    auto edited_probe = *probe;
    edited_probe.priority = 9;
    require(document.world().set_reflection_probe(*subject, edited_probe), "edit authored reflection probe");
    document.mark_dirty();
    require(document.save_authored(&diagnostics), "save authored World");
    require(!document.dirty(), "save clears dirty state");

    editor::WorldDocument reopened;
    require(reopened.open_authored(main_world, editor::DirtyReplacePolicy::discard, &diagnostics), "reopen authored World");
    const auto reopened_subject = reopened.world().find(subject_id);
    require(reopened_subject.has_value(), "saved entity exists");
    const auto reopened_probe = reopened.world().reflection_probe(*reopened_subject);
    require(reopened_probe.has_value() && reopened_probe->priority == 9, "authored edit persists");

    require(reopened.world().register_region(world::RegionDesc{
        .id = world::RegionId{2},
        .simulation_island = world::SimulationIslandId{8},
        .origin = {1.0e12 + 100.0, -2.0e12, 3.0e12},
        .streaming_cell = {12, -12, 13},
        .fidelity = world::RegionFidelity::reduced,
        .bounds = world::RegionBounds{
            .min = {-5.0F, -5.0F, -5.0F},
            .max = {5.0F, 5.0F, 5.0F},
        },
    }), "second region for Save As");
    reopened.mark_dirty();
    const auto save_as = editor::resolve_project_authored_world_path(
        *session,
        std::filesystem::path{"worlds/saved-as.vworld"},
        false,
        &diagnostics
    );
    require(save_as.has_value() && *save_as == std::filesystem::weakly_canonical(saved_as_world), "Save As path resolves");
    require(reopened.save_authored_as(*save_as, &diagnostics), "Save As authored World");
    require(std::filesystem::is_regular_file(saved_as_world), "Save As source exists");

    require(
        editor::save_project_settings(
            *session,
            editor::ProjectSettingsUpdate{
                .project_name = session->manifest().name,
                .game_name = session->manifest().game_name,
                .startup_world = "worlds/saved-as.vworld",
            },
            &diagnostics
        ),
        "Project Settings selects Save As World"
    );
    require(session->manifest().startup_world == std::filesystem::path{"worlds/saved-as.vworld"}, "startup World updated");

    const auto built = editor::build_project(*session);
    require(static_cast<bool>(built), "project builds selected authored World");
    require(built.region_count == 2U, "Build Project follows selected authored source");
    require(built.cooked_startup_world.filename() == "saved-as.vworldc", "Build Project cooks selected source path");

    editor::WorldDocument stale_editor;
    require(stale_editor.open_authored(saved_as_world, editor::DirtyReplacePolicy::discard, &diagnostics),
        "open document before agent edit");
    const auto before_agent = platform::read_text_file(saved_as_world);
    require(before_agent.success, "read pre-agent source");
    tools::world_authoring::EditRequest agent_edit;
    agent_edit.expected_revision = platform::text_revision(before_agent.contents);
    agent_edit.transaction.operations.push_back(tools::world_authoring::SetRegionFidelity{
        world::RegionId{2}, world::RegionFidelity::full});
    const auto agent_result = tools::world_authoring::edit_file(saved_as_world, agent_edit, false);
    require(agent_result.success && agent_result.published, "agent edits same authored source");
    tools::world_authoring::Transaction editor_edit;
    editor_edit.operations.push_back(tools::world_authoring::SetRegionFidelity{
        world::RegionId{1}, world::RegionFidelity::reduced});
    require(stale_editor.apply_authoring_transaction(editor_edit, &diagnostics) && stale_editor.dirty(),
        "Editor uses shared transaction validation");
    require(!stale_editor.save_authored(&diagnostics) && stale_editor.dirty(),
        "stale Editor save rejected with unsaved edits retained");
    require(platform::read_text_file(saved_as_world).contents == agent_result.proposed_source,
        "stale Editor does not overwrite agent source");
    require(stale_editor.open_authored(saved_as_world, editor::DirtyReplacePolicy::discard, &diagnostics),
        "explicitly reload external changes");
    require(stale_editor.apply_authoring_transaction(editor_edit, &diagnostics) &&
        stale_editor.save_authored(&diagnostics) && !stale_editor.dirty(),
        "rebased Editor transaction saves through same publication service");

    std::error_code error;
    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
}
