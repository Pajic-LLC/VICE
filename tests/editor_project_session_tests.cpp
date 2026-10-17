#include <vice/editor/project_session.hpp>

#include <vice/world/persistent_id.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "editor project session test failed: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] vice::world::PersistentId fixed_id(const std::uint8_t tail) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[15] = tail;
    return vice::world::PersistentId{bytes};
}

[[nodiscard]] std::filesystem::path make_temp_root() {
    std::error_code error;
    const auto temp = std::filesystem::temp_directory_path(error);
    require(!error, "temporary directory path");
    const auto root = temp / "vice_editor_project_session_test_40000000";
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root / "project" / "assets" / "worlds", error);
    require(!error, "temporary project directories");
    return root;
}

void test_project_session(const std::filesystem::path& root) {
    const auto manifest_path = root / "project" / "game.viceproj";
    {
        std::ofstream output{manifest_path, std::ios::binary | std::ios::trunc};
        require(static_cast<bool>(output), "manifest open");
        output <<
            "vice-project 1\n"
            "id 40000000-0000-4000-8000-000000000099\n"
            "name \"Session Test\"\n"
            "assets \"assets\"\n"
            "startup_world \"worlds/main.vworld\"\n";
        require(static_cast<bool>(output), "manifest write");
    }
    {
        std::ofstream startup{root / "project" / "assets" / "worlds" / "main.vworld"};
        require(static_cast<bool>(startup), "startup World placeholder");
    }

    std::string diagnostics;
    const auto session = vice::editor::ProjectSession::open(
        manifest_path,
        root / "cache",
        root / "state",
        &diagnostics
    );
    require(session.has_value(), "project session open");
    require(diagnostics.empty(), "project session diagnostics");
    require(session->manifest().name == "Session Test", "project session name");
    require(
        session->asset_root() == (root / "project" / "assets").lexically_normal(),
        "project asset root"
    );
    require(
        session->startup_world_source() ==
            (root / "project" / "assets" / "worlds" / "main.vworld").lexically_normal(),
        "project startup World source"
    );

    const std::string key = "40000000-0000-4000-8000-000000000099";
    require(session->cooked_root() == (root / "cache" / key).lexically_normal(), "cooked root");
    require(
        session->cooked_startup_world() ==
            (root / "cache" / key / "worlds" / "main.vworldc").lexically_normal(),
        "cooked startup World"
    );
    require(
        session->editor_state_root() == (root / "state" / key).lexically_normal(),
        "editor state root"
    );
    require(
        session->layout_path() == (root / "state" / key / "layout.ini").lexically_normal(),
        "project layout path"
    );
    require(
        session->working_world_path() == (root / "state" / key / "world.vworldc").lexically_normal(),
        "working World path"
    );

    auto second_manifest = root / "project" / "other.viceproj";
    {
        std::ofstream output{second_manifest, std::ios::binary | std::ios::trunc};
        require(static_cast<bool>(output), "second manifest open");
        output <<
            "vice-project 1\n"
            "id 40000000-0000-4000-8000-000000000100\n"
            "name \"Other\"\n"
            "assets \"assets\"\n"
            "startup_world \"worlds/main.vworld\"\n";
        require(static_cast<bool>(output), "second manifest write");
    }
    const auto second = vice::editor::ProjectSession::open(
        second_manifest,
        root / "cache",
        root / "state",
        &diagnostics
    );
    require(second.has_value(), "second project session open");
    require(second->cooked_root() != session->cooked_root(), "project cache isolation");
    require(second->editor_state_root() != session->editor_state_root(), "project state isolation");
}

void test_world_document(const std::filesystem::path& root) {
    using namespace vice;

    editor::WorldDocument document;
    require(!document.dirty(), "new document clean");
    require(!document.has_path(), "new document has no path");

    const world::RegionId region{7};
    require(document.world().register_region(world::RegionDesc{
        .id = region,
        .simulation_island = world::SimulationIslandId{70},
        .origin = {10.0, 0.0, 0.0},
        .streaming_cell = {1, 0, 0},
        .fidelity = world::RegionFidelity::full,
    }), "document region");
    const auto persistent_id = fixed_id(44);
    const auto entity = document.world().create_entity(persistent_id);
    require(entity.has_value(), "document entity");
    require(document.world().set_spatial_state(*entity, world::SpatialState{
        .global_position = {12.0, 3.0, -4.0},
        .region = region,
    }), "document spatial state");
    document.mark_dirty();
    require(document.dirty(), "explicit dirty state");

    const auto first_path = root / "documents" / "first.vworldc";
    std::string diagnostics;
    require(document.save_as(first_path, &diagnostics), "Save As");
    require(diagnostics.empty(), "Save As diagnostics");
    require(!document.dirty(), "Save As returns clean");
    require(document.has_path(), "Save As sets path");
    require(document.path() == std::filesystem::absolute(first_path).lexically_normal(), "Save As path");

    const auto spatial = document.world().spatial_state(*entity);
    require(spatial.has_value(), "saved spatial state");
    auto moved = *spatial;
    moved.global_position.x = 99.0;
    require(document.world().set_spatial_state(*entity, moved), "edit after save");
    document.mark_dirty();

    require(!document.open(first_path, editor::DirtyReplacePolicy::reject, &diagnostics), "dirty open rejected");
    require(document.dirty(), "dirty state preserved after rejected open");
    require(
        diagnostics.find("unsaved changes") != std::string::npos,
        "dirty replacement diagnostic"
    );

    require(document.open(first_path, editor::DirtyReplacePolicy::discard, &diagnostics), "explicit discard open");
    require(!document.dirty(), "opened document clean");
    const auto restored_entity = document.world().find(persistent_id);
    require(restored_entity.has_value(), "restored entity identity");
    const auto restored_spatial = document.world().spatial_state(*restored_entity);
    require(restored_spatial.has_value(), "restored spatial state");
    require(restored_spatial->global_position.x == 12.0, "saved snapshot restored");

    document.mark_dirty();
    require(!document.new_empty(editor::DirtyReplacePolicy::reject, &diagnostics), "dirty new rejected");
    require(document.new_empty(editor::DirtyReplacePolicy::discard, &diagnostics), "dirty new explicit discard");
    require(!document.dirty(), "new empty document clean");
    require(!document.has_path(), "new empty document clears path");
    require(document.world().entity_count() == 0U, "new empty World");

    require(!document.save(&diagnostics), "Save without path rejected");
    require(diagnostics.find("Save As") != std::string::npos, "Save without path diagnostic");

    const auto second_path = root / "documents" / "second.vworldc";
    require(document.save_as(second_path, &diagnostics), "empty document Save As");
    require(document.save(&diagnostics), "Save existing document");
    require(!document.dirty(), "Save existing document clean");
}

} // namespace

int main() {
    const auto root = make_temp_root();
    test_project_session(root);
    test_world_document(root);

    std::error_code error;
    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
}
