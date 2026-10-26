#include <vice/tools/project_workflow.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "project workflow test failed: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    using namespace vice;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        "vice_project_workflow_test_71000002";
    require(!error, "temporary directory path");
    std::filesystem::remove_all(root, error);
    error.clear();

    const auto asset_root = root / "assets";
    const auto startup_world = asset_root / "worlds" / "startup.vworld";
    const auto cooked_root = root / "cooked";
    const auto cooked_world = cooked_root / "worlds" / "startup.vworldc";
    std::filesystem::create_directories(startup_world.parent_path(), error);
    require(!error, "startup World directory creation");

    world::WorldSnapshot snapshot;
    std::string diagnostics;
    const auto authored = tools::world_cooker::serialize_source_manifest(snapshot, &diagnostics);
    require(!authored.empty(), diagnostics.empty() ? "World source serialization" : diagnostics);

    {
        std::ofstream output{startup_world, std::ios::binary | std::ios::trunc};
        require(static_cast<bool>(output), "startup World creation");
        output << authored;
        require(static_cast<bool>(output), "startup World write");
    }

    project::Workspace workspace;
    workspace.asset_root = asset_root;
    workspace.startup_world_source = startup_world;
    workspace.cooked_root = cooked_root;
    workspace.cooked_startup_world = cooked_world;

    const auto cooked = tools::cook_project_assets(workspace);
    if (!cooked.success) std::cerr << cooked.diagnostics << '\n';
    require(cooked.success, "neutral project asset workflow succeeds");
    require(cooked.cooked_startup_world == cooked_world, "cooked World path is authoritative");
    require(std::filesystem::is_regular_file(cooked_world), "cooked World is written");
    require(cooked.entity_count == 0U, "empty World entity count");
    require(cooked.region_count == 0U, "empty World region count");
    require(cooked.mesh_count == 0U, "empty project mesh count");
    require(cooked.texture_count == 0U, "empty project texture count");
    require(cooked.environment_count == 0U, "empty project environment count");
    require(cooked.material_count == 0U, "empty project material count");

    workspace.startup_world_source = root / "missing.vworld";
    const auto missing = tools::cook_project_assets(workspace);
    require(!missing.success, "missing startup World fails");
    require(!missing.diagnostics.empty(), "missing startup World reports diagnostics");

    std::filesystem::remove_all(root, error);
    return 0;
}
