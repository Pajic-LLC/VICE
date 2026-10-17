#include <vice/editor/project_session.hpp>
#include <vice/editor/project_workflow.hpp>
#include <vice/project/project.hpp>
#include <vice/world/snapshot_file.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "editor project workflow test failed: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] std::filesystem::path temp_root() {
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        "vice_editor_project_workflow_test_40000000";
    require(!error, "temporary root");
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root / "projects", error);
    require(!error, "temporary project parent");
    return root;
}

} // namespace

int main() {
    using namespace vice;

    const auto root = temp_root();
    const auto created = editor::create_project(editor::ProjectCreateDesc{
        .name = "My First Game",
        .parent_directory = root / "projects",
    });
    require(static_cast<bool>(created), "project creation");
    require(created.diagnostics.empty(), "project creation diagnostics");
    require(created.manifest_path->filename() == "vice.viceproj", "manifest filename");

    const auto loaded = project::load_manifest(*created.manifest_path);
    require(loaded.manifest.has_value(), "created manifest loads");
    require(loaded.manifest->name == "My First Game", "created project name");
    require(static_cast<bool>(loaded.manifest->id), "created project ID");
    require(loaded.manifest->asset_root == "assets", "created asset root");
    require(loaded.manifest->startup_world == std::filesystem::path{"worlds/main.vworld"}, "created startup World");
    require(
        project::serialize_manifest(*loaded.manifest) == project::serialize_manifest(*loaded.manifest),
        "created manifest serialization deterministic"
    );

    const auto project_root = created.manifest_path->parent_path();
    require(std::filesystem::is_directory(project_root / "assets" / "worlds"), "worlds folder");
    require(std::filesystem::is_directory(project_root / "assets" / "meshes"), "meshes folder");
    require(std::filesystem::is_directory(project_root / "assets" / "collision"), "collision folder");
    require(std::filesystem::is_directory(project_root / "assets" / "materials"), "materials folder");
    require(std::filesystem::is_directory(project_root / "assets" / "shaders"), "shaders folder");
    require(std::filesystem::is_regular_file(project_root / "assets" / "worlds" / "main.vworld"), "startup source World");

    std::string diagnostics;
    const auto session = editor::ProjectSession::open(
        *created.manifest_path,
        root / "cache",
        root / "state",
        &diagnostics
    );
    require(session.has_value(), "created project session");
    require(diagnostics.empty(), "created project session diagnostics");

#if defined(VICE_EDITOR_HAS_WORLD_COOKER)
    require(editor::cook_project_startup_world(*session, &diagnostics), "startup World cooking");
    require(diagnostics.empty(), "startup World cooking diagnostics");
    require(std::filesystem::is_regular_file(session->cooked_startup_world()), "cooked startup World exists");
    const auto cooked = world::load_world_snapshot(session->cooked_startup_world());
    require(cooked.snapshot.has_value(), "cooked startup World loads");
    require(cooked.snapshot->regions.size() == 1U, "default World has one region");
#else
    require(!editor::cook_project_startup_world(*session, &diagnostics), "missing cooker reports unavailable");
    require(!diagnostics.empty(), "missing cooker diagnostics");
#endif

    const auto collision = editor::create_project(editor::ProjectCreateDesc{
        .name = "My First Game",
        .parent_directory = root / "projects",
    });
    require(!collision, "existing project destination rejected");
    require(std::filesystem::is_regular_file(*created.manifest_path), "existing project remains intact");

    const auto blank = editor::create_project(editor::ProjectCreateDesc{
        .name = "   ",
        .parent_directory = root / "projects",
    });
    require(!blank, "blank project name rejected");

    const std::string folder = editor::project_folder_name("  My:Game?  ");
    require(folder.find(':') == std::string::npos, "folder removes colon");
    require(folder.find('?') == std::string::npos, "folder removes question mark");
    require(!folder.empty(), "folder name remains usable");

    std::error_code error;
    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
}
