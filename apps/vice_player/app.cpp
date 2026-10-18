#include <vice/runtime/module_loader.hpp>
#include <vice/runtime/runtime.hpp>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

class DefaultGameModule final : public vice::runtime::GameModule {};

} // namespace

int main(const int argc, char** argv) {
    std::string diagnostics;
    vice::runtime::LaunchPaths launch;
    const bool staged_mode = argc == 1;

    if (staged_mode) {
        launch = vice::runtime::staged_launch_paths(
            std::filesystem::path{argv[0]},
            &diagnostics
        );
    } else if (argc == 3) {
        launch = vice::runtime::explicit_launch_paths(
            std::filesystem::path{argv[1]},
            std::filesystem::path{argv[2]},
            &diagnostics
        );
    } else {
        std::cerr
            << "Usage: staged game target, or vice_player <project.viceproj> <cooked-cache-root>\n";
        return 2;
    }

    if (launch.manifest.empty() || launch.cooked_cache_root.empty()) {
        std::cerr << "VICE Runtime could not resolve launch paths: " << diagnostics << '\n';
        return 1;
    }

    auto loaded = vice::runtime::load_project(launch);
    if (!loaded.project.has_value()) {
        std::cerr << "VICE Runtime could not boot the game project: " << loaded.diagnostics << '\n';
        return 1;
    }

    DefaultGameModule default_game;
    vice::runtime::GameModule* game = &default_game;
    vice::runtime::GameModuleLoadResult project_game;

    if (staged_mode) {
        const auto module_path = vice::runtime::staged_game_module_path(
            launch.manifest.parent_path() / "game-target",
            loaded.project->manifest.game_target
        );
        std::error_code error;
        const bool module_exists = std::filesystem::is_regular_file(module_path, error);
        if (error) {
            std::cerr
                << "VICE Runtime could not inspect project game module `"
                << module_path.generic_string()
                << "`: "
                << error.message()
                << '\n';
            return 1;
        }
        if (module_exists) {
            project_game = vice::runtime::load_game_module(module_path);
            if (!project_game) {
                std::cerr << "VICE Runtime could not load project game code: "
                          << project_game.diagnostics << '\n';
                return 1;
            }
            game = project_game.module.get();
        }
    }

    return vice::runtime::run_native_game(*loaded.project, *game);
}
