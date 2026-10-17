#include <vice/editor/project_session.hpp>
#include <vice/editor/project_settings.hpp>
#include <vice/project/project.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "editor project settings test failed: " << message << '\n';
    std::exit(1);
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "test file open");
    output << text;
    require(static_cast<bool>(output), "test file write");
}

} // namespace

int main() {
    using namespace vice;

    std::error_code error;
    const auto temp_root = std::filesystem::temp_directory_path(error) /
        "vice_editor_project_settings_test_40000000";
    require(!error, "temporary directory path");
    std::filesystem::remove_all(temp_root, error);
    error.clear();

    const auto project_root = temp_root / "project";
    const auto worlds = project_root / "assets" / "worlds";
    std::filesystem::create_directories(worlds, error);
    require(!error, "project directories");

    const auto manifest_path = project_root / "vice.viceproj";
    write_text(
        manifest_path,
        "vice-project 1\n"
        "id 42000000-0000-4000-8000-000000000137\n"
        "name \"Settings Project\"\n"
        "game_name \"Settings Game\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    write_text(worlds / "main.vworld", "vice-world-source 1\n");
    write_text(worlds / "alternate.vworld", "vice-world-source 1\n");

    std::string diagnostics;
    auto session = editor::ProjectSession::open(
        manifest_path,
        temp_root / "cache",
        temp_root / "state",
        &diagnostics
    );
    require(session.has_value(), "project session open");
    require(diagnostics.empty(), "project session diagnostics");

    const auto project_id = session->manifest().id;
    const auto asset_root = session->manifest().asset_root;

    require(
        editor::save_project_settings(
            *session,
            editor::ProjectSettingsUpdate{
                .project_name = "Renamed Project",
                .game_name = "Palma Bay",
                .startup_world = "worlds/alternate.vworld",
            },
            &diagnostics
        ),
        "settings save"
    );
    require(diagnostics.empty(), "settings save diagnostics");
    require(session->manifest().id == project_id, "settings preserve project id");
    require(session->manifest().asset_root == asset_root, "settings preserve asset root");
    require(session->manifest().name == "Renamed Project", "session project name update");
    require(session->manifest().game_name == "Palma Bay", "session game name update");
    require(
        session->manifest().startup_world == std::filesystem::path{"worlds/alternate.vworld"},
        "session startup World update"
    );
    require(
        session->startup_world_source() == (worlds / "alternate.vworld").lexically_normal(),
        "session startup source update"
    );
    require(
        session->cooked_startup_world() ==
            (temp_root / "cache" / project_id.to_string() / "worlds" / "alternate.vworldc").lexically_normal(),
        "session cooked startup update"
    );

    const auto reloaded = project::load_manifest(manifest_path);
    require(reloaded.manifest.has_value(), "saved manifest reload");
    require(reloaded.manifest->name == "Renamed Project", "saved project name");
    require(reloaded.manifest->game_name == "Palma Bay", "saved game name");
    require(
        reloaded.manifest->startup_world == std::filesystem::path{"worlds/alternate.vworld"},
        "saved startup World"
    );

    require(
        !editor::save_project_settings(
            *session,
            editor::ProjectSettingsUpdate{
                .project_name = "Renamed Project",
                .game_name = "",
                .startup_world = "worlds/alternate.vworld",
            },
            &diagnostics
        ),
        "empty game name rejected"
    );
    require(!diagnostics.empty(), "empty game name diagnostic");

    require(
        !editor::save_project_settings(
            *session,
            editor::ProjectSettingsUpdate{
                .project_name = "Renamed Project",
                .game_name = "Palma Bay",
                .startup_world = "worlds/missing.vworld",
            },
            &diagnostics
        ),
        "missing startup World rejected"
    );
    require(!diagnostics.empty(), "missing startup diagnostic");

    require(
        !editor::save_project_settings(
            *session,
            editor::ProjectSettingsUpdate{
                .project_name = "Renamed Project",
                .game_name = "Palma Bay",
                .startup_world = "../outside.vworld",
            },
            &diagnostics
        ),
        "startup traversal rejected"
    );
    require(!diagnostics.empty(), "startup traversal diagnostic");

    const auto after_failures = project::load_manifest(manifest_path);
    require(after_failures.manifest.has_value(), "manifest remains loadable after rejected settings");
    require(after_failures.manifest->game_name == "Palma Bay", "rejected settings do not mutate manifest");
    require(
        after_failures.manifest->startup_world == std::filesystem::path{"worlds/alternate.vworld"},
        "rejected settings preserve startup World"
    );

    std::filesystem::remove_all(temp_root, error);
    require(!error, "temporary cleanup");
    return 0;
}
