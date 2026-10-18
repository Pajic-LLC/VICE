#include <vice/editor/project_build.hpp>
#include <vice/editor/project_session.hpp>

#include <vice/project/project.hpp>
#include <vice/world/snapshot_file.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "editor project build test failed: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] std::filesystem::path make_temp_root() {
    std::error_code error;
    const auto temp = std::filesystem::temp_directory_path(error);
    require(!error, "temporary directory path");
    const auto root = temp / "vice_editor_project_build_test_43000000";
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root / "project" / "assets" / "worlds", error);
    require(!error, "temporary project directories");
    return root;
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        require(!error, "text file parent directory");
    }
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "text file open");
    output << text;
    require(static_cast<bool>(output), "text file write");
}

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(value.data()), value.size()};
}

} // namespace

int main() {
    using namespace vice;

    const auto root = make_temp_root();
    const auto project_root = root / "project";
    const auto manifest_path = project_root / "game.viceproj";
    const auto startup_world = project_root / "assets" / "worlds" / "main.vworld";
    const auto cache_base = root / "cache";
    const auto state_base = root / "state";

    write_text(
        manifest_path,
        "vice-project 1\n"
        "id 43000000-0000-4000-8000-000000000133\n"
        "name \"Build Test\"\n"
        "game_name \"Palma Game\"\n"
        "game_target \"palma-game\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    write_text(
        startup_world,
        "vice-world-source 1\n\n"
        "region 1 1 0 0 0 0 0 0 full bounds -10 -10 -10 10 10 10\n"
    );

    std::string diagnostics;
    auto session = editor::ProjectSession::open(
        manifest_path,
        cache_base,
        state_base,
        &diagnostics
    );
    require(session.has_value(), "project session opens");
    require(diagnostics.empty(), "project session diagnostics");

    const auto built = editor::build_project(*session);
    require(static_cast<bool>(built), "project build succeeds");
    require(built.diagnostics.empty(), "project build diagnostics");
    const auto expected_cooked = (
        cache_base /
        "43000000-0000-4000-8000-000000000133" /
        "worlds" /
        "main.vworldc"
    ).lexically_normal();
    require(built.cooked_startup_world == expected_cooked, "cooked startup World path");
    require(std::filesystem::is_regular_file(expected_cooked), "cooked startup World exists");
    const auto loaded = world::load_world_snapshot(expected_cooked);
    require(loaded.snapshot.has_value(), "cooked startup World loads");
    require(loaded.snapshot->regions.size() == 1U, "cooked startup World region count");

#ifdef _WIN32
    const auto runtime_host = root / "bin" / "vice_player.exe";
    const std::string target_filename = "palma-game.exe";
#else
    const auto runtime_host = root / "bin" / "vice_player";
    const std::string target_filename = "palma-game";
#endif
    write_text(runtime_host, "VICE runtime host placeholder\n");

    std::error_code permission_error;
    const auto host_permissions = std::filesystem::status(runtime_host, permission_error).permissions();
    require(!permission_error, "runtime host permissions read");
    std::filesystem::permissions(
        runtime_host,
        host_permissions | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace,
        permission_error
    );
    require(!permission_error, "runtime host permissions write");

    const auto game_build = editor::build_game_target(*session, runtime_host);
    require(static_cast<bool>(game_build), "game target build succeeds");
    require(game_build.game_target == "palma-game", "game target identity");

    const auto expected_output = (
        project_root / ".vice" / "build" / "development" / "palma-game"
    ).lexically_normal();
    require(game_build.game_output_root == expected_output, "game output root");
    require(game_build.game_executable == expected_output / target_filename, "target executable path");
    require(std::filesystem::is_regular_file(game_build.game_executable), "target executable exists");
    require(
        game_build.runtime_manifest == expected_output / "vice.runtime.viceproj",
        "runtime manifest path"
    );
    require(std::filesystem::is_regular_file(game_build.runtime_manifest), "runtime manifest exists");

    const auto runtime_manifest = project::load_manifest(game_build.runtime_manifest);
    require(runtime_manifest.manifest.has_value(), "runtime manifest loads");
    require(runtime_manifest.manifest->game_target == "palma-game", "runtime manifest target");
    require(runtime_manifest.manifest->game_name == "Palma Game", "runtime manifest display name");

    const auto expected_runtime_world = (
        expected_output /
        "content" /
        "43000000-0000-4000-8000-000000000133" /
        "worlds" /
        "main.vworldc"
    ).lexically_normal();
    require(std::filesystem::is_regular_file(expected_runtime_world), "runtime startup World exists");
    const auto runtime_world = world::load_world_snapshot(expected_runtime_world);
    require(runtime_world.snapshot.has_value(), "runtime startup World loads");

    const auto game_request = editor::game_run_request(game_build);
    require(game_request.executable == game_build.game_executable, "game run executable");
    require(game_request.arguments.empty(), "staged game target needs no arguments");
    require(game_request.working_directory == expected_output, "game run working directory");

    const auto compatibility_request = editor::project_run_request(*session, runtime_host);
    require(compatibility_request.executable == runtime_host, "compatibility run executable");
    require(compatibility_request.arguments.size() == 2U, "compatibility argument count");
    require(
        compatibility_request.arguments[0] == path_text(std::filesystem::absolute(manifest_path).lexically_normal()),
        "compatibility manifest argument"
    );
    require(
        compatibility_request.arguments[1] == path_text(std::filesystem::absolute(cache_base).lexically_normal()),
        "compatibility cooked-cache argument"
    );

    auto retargeted = session->manifest();
    retargeted.game_target = "palma-preview";
    require(session->apply_manifest(std::move(retargeted), &diagnostics), "live game target update");
    require(
        session->game_output_root() ==
            (project_root / ".vice" / "build" / "development" / "palma-preview").lexically_normal(),
        "retargeted output root"
    );

    write_text(startup_world, "this is not a VICE World\n");
    const auto invalid = editor::build_project(*session);
    require(!static_cast<bool>(invalid), "invalid authored World rejected");
    require(
        invalid.diagnostics.find(path_text(startup_world)) != std::string::npos,
        "invalid authored World identifies source file"
    );

    std::error_code error;
    std::filesystem::remove(startup_world, error);
    require(!error, "startup World removal");
    const auto missing = editor::build_project(*session);
    require(!static_cast<bool>(missing), "missing authored World rejected");
    require(
        missing.diagnostics.find(path_text(startup_world)) != std::string::npos,
        "missing authored World identifies source file"
    );

    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
}
