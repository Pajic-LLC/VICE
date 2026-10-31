#include <vice/platform/dynamic_library.hpp>
#include <vice/project/workspace.hpp>
#include <vice/runtime/package_metadata.hpp>
#include <vice/tools/project_workflow.hpp>
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
    std::cerr << "project build workflow test failed: " << message << '\n';
    std::exit(1);
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        require(!error, "create text parent directory");
    }
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "open text file");
    output << text;
    require(static_cast<bool>(output), "write text file");
}

} // namespace

int main() {
    using namespace vice;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        "vice_project_build_workflow_tests_84000000";
    require(!error, "resolve temporary directory");
    std::filesystem::remove_all(root, error);
    error.clear();

    const auto project_root = root / "project";
    const auto manifest_path = project_root / "game.viceproj";
    const auto startup_world = project_root / "assets" / "worlds" / "main.vworld";
    const auto texture_source = project_root / "assets" / "textures" / "surface.vtex";
    const auto texture_image = project_root / "assets" / "textures" / "surface.ppm";
    const auto game_source = project_root / "game" / "game.cpp";
    const auto cache_base = root / "cache";

    write_text(
        manifest_path,
        "vice-project 1\n"
        "id 84000000-0000-4000-8000-000000000001\n"
        "name \"Shared Build Workflow\"\n"
        "game_name \"Shared Build Game\"\n"
        "game_target \"shared-build-game\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    write_text(
        startup_world,
        "vice-world-source 1\n"
        "region 7 70 1000000000000.25 -2000000000000.5 3000000000000.75 0 0 0 full bounds -10 -10 -10 10 10 10\n"
    );
    write_text(texture_image, "P3\n2 1\n255\n255 64 32  32 96 255\n");
    write_text(
        texture_source,
        "vice-texture-source 1\n"
        "id 84000000-0000-4000-8000-000000000010\n"
        "image \"textures/surface.ppm\"\n"
        "color_space srgb\n"
    );

    const auto opened = project::open_workspace(manifest_path, cache_base);
    require(static_cast<bool>(opened), "open authoritative project workspace");
    const auto& workspace = *opened.workspace;

#ifdef _WIN32
    const auto runtime_host = root / "bin" / "vice_player.exe";
    const std::string target_filename = "shared-build-game.exe";
#else
    const auto runtime_host = root / "bin" / "vice_player";
    const std::string target_filename = "shared-build-game";
#endif
    write_text(runtime_host, "VICE runtime host placeholder\n");
    const auto permissions = std::filesystem::status(runtime_host, error).permissions();
    require(!error, "read runtime host permissions");
    std::filesystem::permissions(
        runtime_host,
        permissions | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace,
        error
    );
    require(!error, "write runtime host permissions");

    const auto no_module = tools::build_project(workspace, runtime_host);
    if (!no_module) std::cerr << no_module.diagnostics << '\n';
    require(static_cast<bool>(no_module), "shared build without game module succeeds");
    require(no_module.project_id == "84000000-0000-4000-8000-000000000001", "stable project ID");
    require(no_module.game_target == "shared-build-game", "game target identity");
    require(no_module.game_module_source.empty(), "missing game source is a valid state");
    require(no_module.assets.region_count == 1U, "startup World region count");
    require(no_module.assets.texture_count == 1U, "texture count");

    const auto expected_output = (
        project_root / ".vice" / "build" / "development" / "shared-build-game"
    ).lexically_normal();
    require(no_module.game_output_root == expected_output, "authoritative game output root");
    require(no_module.game_executable == expected_output / target_filename, "staged executable path");
    require(std::filesystem::is_regular_file(no_module.game_executable), "staged executable exists");
    require(std::filesystem::is_regular_file(no_module.runtime_manifest), "runtime manifest exists");

    const auto staged_package = runtime::load_staged_package_metadata(no_module.game_executable);
    require(static_cast<bool>(staged_package), "staged package metadata loads");
    require(!staged_package.dade_required, "unprotected project remains unprotected");

    const auto staged_world = expected_output / "content" /
        "84000000-0000-4000-8000-000000000001" / "worlds" / "main.vworldc";
    require(std::filesystem::is_regular_file(staged_world), "startup World is staged");
    const auto loaded_world = world::load_world_snapshot(staged_world);
    require(loaded_world.snapshot.has_value(), "staged startup World loads");
    require(loaded_world.snapshot->regions.size() == 1U, "staged World keeps authoritative region");
    require(
        loaded_world.snapshot->regions.front().origin.x == 1000000000000.25,
        "large-world origin precision survives build"
    );

    const auto staged_texture = expected_output / "content" /
        "84000000-0000-4000-8000-000000000001" / "textures" / "surface.vtexc";
    require(std::filesystem::is_regular_file(staged_texture), "cooked texture is staged");

    const auto launch = tools::project_launch_request(no_module);
    require(launch.executable == no_module.game_executable, "launch uses staged executable only");
    require(launch.arguments.empty(), "staged game launch has no authoring arguments");
    require(launch.working_directory == expected_output, "launch uses staged output working directory");

    write_text(
        game_source,
        "#include <vice/runtime/module_api.hpp>\n\n"
        "class SharedBuildGame final : public vice::runtime::module::Game {\n"
        "public:\n"
        "    void on_frame(vice::runtime::module::Context&, vice::runtime::module::Frame& frame) override {\n"
        "        frame.clear_color = {0.1F, 0.3F, 0.5F, 1.0F};\n"
        "    }\n"
        "};\n\n"
        "VICE_GAME_MODULE(SharedBuildGame)\n"
    );

    const auto unavailable = tools::build_project(
        workspace,
        runtime_host,
        tools::ProjectBuildToolchain{}
    );
    require(!static_cast<bool>(unavailable), "game module requires configured shared toolchain");
    require(
        unavailable.diagnostics.find("toolchain") != std::string::npos,
        "missing toolchain is explained"
    );

    const auto with_module = tools::build_project(workspace, runtime_host);
    require(static_cast<bool>(with_module), "shared build with game module succeeds");
    require(with_module.game_module_source == game_source, "game module source is reported");
    const auto expected_module = expected_output / "modules" /
        ("shared-build-game" + std::string{platform::dynamic_library_extension()});
    require(with_module.game_module == expected_module, "staged game module path");
    require(std::filesystem::is_regular_file(expected_module), "compiled game module is staged");

    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
}
