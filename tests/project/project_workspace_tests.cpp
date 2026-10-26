#include <vice/dev/project_workspace.hpp>
#include <vice/project/project.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "project workspace test failed: " << message << '\n';
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
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) / "vice_project_workspace_tests";
    require(!error, "resolve temporary directory");
    std::filesystem::remove_all(root, error);
    error.clear();

    const auto manifest_path = root / "project" / "vice.viceproj";
    const auto startup_world = root / "project" / "assets" / "worlds" / "main.vworld";
    const auto cache_base = root / "cache";
    write_text(
        manifest_path,
        "vice-project 1\n"
        "id 83000000-0000-4000-8000-000000000001\n"
        "name \"Headless Workflow\"\n"
        "game_name \"Headless Workflow Game\"\n"
        "game_target \"headless-workflow\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    write_text(startup_world, "vice-world-source 1\n");

    const auto opened = vice::dev::open_project_workspace(manifest_path, cache_base);
    require(static_cast<bool>(opened), "workspace opens");
    require(opened.diagnostics.empty(), "workspace diagnostics clear");
    const auto& workspace = *opened.workspace;

    const auto loaded = vice::project::load_manifest(manifest_path);
    require(loaded.manifest.has_value(), "authoritative project manifest loads");
    require(workspace.manifest == *loaded.manifest, "workspace preserves authoritative manifest");
    require(
        workspace.asset_root == vice::project::resolve_asset_root(manifest_path, *loaded.manifest),
        "asset root uses authoritative resolver"
    );
    require(
        workspace.startup_world_source == vice::project::resolve_startup_world(manifest_path, *loaded.manifest),
        "startup World uses authoritative resolver"
    );
    require(
        workspace.cooked_root == vice::project::resolve_cooked_root(cache_base, *loaded.manifest),
        "cooked root uses authoritative resolver"
    );
    require(
        workspace.cooked_startup_world ==
            vice::project::resolve_cooked_startup_world(cache_base, *loaded.manifest),
        "cooked startup World uses authoritative resolver"
    );
    require(
        workspace.game_output_root ==
            vice::project::resolve_game_output_root(manifest_path, *loaded.manifest),
        "game output uses authoritative resolver"
    );

    const auto opened_again = vice::dev::open_project_workspace(manifest_path, cache_base);
    require(static_cast<bool>(opened_again), "workspace reopens");
    require(opened_again.workspace->manifest.id == workspace.manifest.id, "stable project identity");
    require(opened_again.workspace->cooked_root == workspace.cooked_root, "deterministic cooked root");

    const auto invalid_manifest = root / "invalid" / "vice.viceproj";
    write_text(
        invalid_manifest,
        "vice-project 1\n"
        "id 83000000-0000-4000-8000-000000000001\n"
        "name \"Invalid\"\n"
        "game_name \"Invalid\"\n"
        "game_target \"invalid\"\n"
        "assets \"../assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    const auto invalid = vice::dev::open_project_workspace(invalid_manifest, cache_base);
    require(!static_cast<bool>(invalid), "invalid project is rejected");
    require(!invalid.diagnostics.empty(), "invalid project has diagnostics");

    const auto missing_cache = vice::dev::open_project_workspace(manifest_path, {});
    require(!static_cast<bool>(missing_cache), "empty cooked-cache base is rejected");
    require(!missing_cache.diagnostics.empty(), "empty cooked-cache diagnostic");

    std::filesystem::remove_all(root, error);
    require(!error, "remove test directory");
    return 0;
}
