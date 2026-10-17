#include <vice/editor/project_build.hpp>
#include <vice/editor/project_session.hpp>

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
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    write_text(
        startup_world,
        "vice-world-source 1\n\n"
        "region 1 1 0 0 0 0 0 0 full bounds -10 -10 -10 10 10 10\n"
    );

    std::string diagnostics;
    const auto session = editor::ProjectSession::open(
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

    const auto player = root / "bin" / "vice_player";
    const auto request = editor::project_run_request(*session, player);
    require(request.executable == player, "run executable");
    require(request.arguments.size() == 2U, "run argument count");
    require(
        request.arguments[0] == path_text(std::filesystem::absolute(manifest_path).lexically_normal()),
        "run manifest argument"
    );
    require(
        request.arguments[1] == path_text(std::filesystem::absolute(cache_base).lexically_normal()),
        "run cooked-cache argument"
    );
    require(
        request.working_directory == std::filesystem::absolute(project_root).lexically_normal(),
        "run working directory"
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
