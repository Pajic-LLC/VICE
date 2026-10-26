#include <vice/project/workspace.hpp>
#include <vice/render/mesh_asset.hpp>
#include <vice/tools/project_mesh_cook.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "project mesh cook test failed: " << message << '\n';
    std::exit(1);
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    require(!error, "create test directory");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "open test file");
    output << text;
    require(static_cast<bool>(output), "write test file");
}

} // namespace

int main() {
    using namespace vice;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) / "vice_project_mesh_cook_tests";
    require(!error, "resolve temporary directory");
    std::filesystem::remove_all(root, error);
    error.clear();

    const auto manifest_path = root / "project" / "vice.viceproj";
    const auto world_source = root / "project" / "assets" / "worlds" / "main.vworld";
    const auto mesh_source = root / "project" / "assets" / "meshes" / "triangle.vmesh";
    const auto cache_base = root / "cache";

    write_text(
        manifest_path,
        "vice-project 1\n"
        "id 84000000-0000-4000-8000-000000000001\n"
        "name \"Shared Mesh Cook\"\n"
        "game_name \"Shared Mesh Cook\"\n"
        "game_target \"shared-mesh-cook\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    write_text(world_source, "vice-world-source 1\n");
    write_text(
        mesh_source,
        "vice-mesh-source 1\n"
        "name triangle\n"
        "id 84000000-0000-4000-8000-000000000002\n"
        "collision none\n\n"
        "vertex -1 0 0 1 0 0\n"
        "vertex 1 0 0 0 1 0\n"
        "vertex 0 1 0 0 0 1\n"
        "triangle 0 1 2\n"
    );

    const auto opened = project::open_workspace(manifest_path, cache_base);
    require(static_cast<bool>(opened), "project workspace opens");

    const auto cooked = tools::cook_project_meshes(*opened.workspace);
    require(static_cast<bool>(cooked), "mesh cook succeeds");
    require(cooked.diagnostics.empty(), "mesh cook diagnostics clear");
    require(cooked.meshes.size() == 1U, "one mesh cooked");
    require(cooked.meshes.front().relative_path == std::filesystem::path{"meshes/triangle.vmeshc"}, "relative cooked path");

    const auto expected = (
        cache_base /
        "84000000-0000-4000-8000-000000000001" /
        "meshes" /
        "triangle.vmeshc"
    ).lexically_normal();
    require(cooked.meshes.front().cooked_path == expected, "workspace cooked root used");
    require(std::filesystem::is_regular_file(expected), "cooked mesh exists");
    const auto loaded = render::load_mesh_asset(expected);
    require(loaded.asset.has_value(), "cooked mesh loads");
    require(
        world::PersistentId{loaded.asset->stable_id}.to_string() ==
            "84000000-0000-4000-8000-000000000002",
        "stable mesh ID preserved"
    );

    write_text(mesh_source, "this is not a VICE mesh\n");
    const auto invalid = tools::cook_project_meshes(*opened.workspace);
    require(!static_cast<bool>(invalid), "invalid mesh rejects cook");
    require(
        invalid.diagnostics.find(mesh_source.generic_string()) != std::string::npos,
        "invalid mesh diagnostic identifies source"
    );
    require(std::filesystem::is_regular_file(expected), "last good cooked mesh remains after failed recook");

    std::filesystem::remove_all(root, error);
    require(!error, "remove test directory");
    return 0;
}
