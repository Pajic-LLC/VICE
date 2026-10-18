#include <vice/editor/project_build.hpp>
#include <vice/editor/project_session.hpp>
#include <vice/render/mesh_asset.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "editor project mesh build test failed: " << message << '\n';
    std::exit(1);
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    require(!error, "text parent directory");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "text file open");
    output << text;
    require(static_cast<bool>(output), "text file write");
}

} // namespace

int main() {
    using namespace vice;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        "vice_editor_project_mesh_build_test_53000000";
    require(!error, "temporary directory path");
    std::filesystem::remove_all(root, error);
    error.clear();

    const auto project_root = root / "project";
    const auto manifest_path = project_root / "game.viceproj";
    const auto world_source = project_root / "assets" / "worlds" / "main.vworld";
    const auto mesh_source = project_root / "assets" / "meshes" / "triangle.vmesh";
    const auto cache_base = root / "cache";
    const auto state_base = root / "state";

    write_text(
        manifest_path,
        "vice-project 1\n"
        "id 53000000-0000-4000-8000-000000000001\n"
        "name \"Mesh Build Test\"\n"
        "game_name \"Mesh Build Test\"\n"
        "game_target \"mesh-build-test\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    write_text(
        world_source,
        "vice-world-source 1\n\n"
        "region 1 1 0 0 0 0 0 0 full bounds -10 -10 -10 10 10 10\n"
    );
    write_text(
        mesh_source,
        "vice-mesh-source 1\n"
        "name triangle\n"
        "id 53000000-0000-4000-8000-000000000002\n"
        "collision none\n\n"
        "vertex -1 0 0 1 0 0\n"
        "vertex 1 0 0 0 1 0\n"
        "vertex 0 1 0 0 0 1\n"
        "triangle 0 1 2\n"
    );

    std::string diagnostics;
    auto session = editor::ProjectSession::open(manifest_path, cache_base, state_base, &diagnostics);
    require(session.has_value(), "project session opens");
    require(diagnostics.empty(), "project session diagnostics");

#ifdef _WIN32
    const auto runtime_host = root / "bin" / "vice_player.exe";
#else
    const auto runtime_host = root / "bin" / "vice_player";
#endif
    write_text(runtime_host, "VICE runtime host placeholder\n");
    const auto permissions = std::filesystem::status(runtime_host, error).permissions();
    require(!error, "runtime host permissions read");
    std::filesystem::permissions(
        runtime_host,
        permissions | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace,
        error
    );
    require(!error, "runtime host permissions write");

    const auto built = editor::build_game_target(*session, runtime_host);
    require(static_cast<bool>(built), "game target with mesh builds");
    require(built.diagnostics.empty(), "mesh build diagnostics");
    require(built.mesh_count == 1U, "mesh build count");

    const auto expected_cached = (
        cache_base /
        "53000000-0000-4000-8000-000000000001" /
        "meshes" /
        "triangle.vmeshc"
    ).lexically_normal();
    require(std::filesystem::is_regular_file(expected_cached), "cooked mesh cache exists");
    const auto cached_mesh = render::load_mesh_asset(expected_cached);
    require(cached_mesh.asset.has_value(), "cooked mesh cache loads");

    const auto expected_staged = (
        built.game_output_root /
        "content" /
        "53000000-0000-4000-8000-000000000001" /
        "meshes" /
        "triangle.vmeshc"
    ).lexically_normal();
    require(std::filesystem::is_regular_file(expected_staged), "staged cooked mesh exists");
    const auto staged_mesh = render::load_mesh_asset(expected_staged);
    require(staged_mesh.asset.has_value(), "staged cooked mesh loads");
    require(
        world::PersistentId{staged_mesh.asset->stable_id}.to_string() ==
            "53000000-0000-4000-8000-000000000002",
        "staged cooked mesh preserves stable ID"
    );

    const auto last_good_output = built.game_output_root;
    const auto last_good_mesh = expected_staged;
    write_text(mesh_source, "this is not a VICE mesh\n");
    const auto invalid = editor::build_game_target(*session, runtime_host);
    require(!static_cast<bool>(invalid), "invalid authored mesh rejects build");
    require(
        invalid.diagnostics.find(mesh_source.generic_string()) != std::string::npos,
        "invalid authored mesh diagnostic identifies source"
    );
    require(std::filesystem::is_directory(last_good_output), "last good game output survives failed mesh build");
    require(std::filesystem::is_regular_file(last_good_mesh), "last good staged mesh survives failed build");

    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
}
