#include <vice/tools/project_render_material_cook.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "project render material cook test failed: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    using namespace vice;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        "vice_project_render_material_cook_test_71000001";
    require(!error, "temporary directory path");
    std::filesystem::remove_all(root, error);
    error.clear();

    const auto asset_root = root / "assets";
    const auto cooked_root = root / "cooked";
    std::filesystem::create_directories(asset_root, error);
    require(!error, "asset root creation");

    project::Workspace workspace;
    workspace.asset_root = asset_root;
    workspace.cooked_root = cooked_root;

    const auto empty = tools::cook_project_render_materials(workspace);
    if (!empty.success) std::cerr << empty.diagnostics << '\n';
    require(empty.success, "empty persisted project material set cooks");
    require(empty.materials.empty(), "empty project produces no cooked materials");

    workspace.asset_root = root / "missing-assets";
    const auto missing = tools::cook_project_render_materials(workspace);
    require(!missing.success, "missing asset root fails explicitly");
    require(!missing.diagnostics.empty(), "missing asset root reports diagnostics");

    std::filesystem::remove_all(root, error);
    return 0;
}
