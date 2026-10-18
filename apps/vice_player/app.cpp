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

    if (argc == 1) {
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

    DefaultGameModule game;
    return vice::runtime::run_native_game(*loaded.project, game);
}
