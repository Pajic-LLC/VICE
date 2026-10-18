#include <vice/project/project.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

struct LaunchPaths {
    std::filesystem::path manifest;
    std::filesystem::path cooked_cache_root;
};

[[nodiscard]] std::filesystem::path absolute_normalized(
    const std::filesystem::path& path,
    std::string& diagnostics
) {
    if (path.empty()) {
        diagnostics = "Path is empty.";
        return {};
    }

    std::error_code error;
    auto resolved = std::filesystem::absolute(path, error);
    if (error) {
        diagnostics = "Could not resolve path: " + error.message();
        return {};
    }
    diagnostics.clear();
    return resolved.lexically_normal();
}

[[nodiscard]] LaunchPaths packaged_launch_paths(
    const std::filesystem::path& executable,
    std::string& diagnostics
) {
    const auto resolved_executable = absolute_normalized(executable, diagnostics);
    if (resolved_executable.empty()) return {};
    const auto root = resolved_executable.parent_path();
    if (root.empty()) {
        diagnostics = "Could not resolve runtime output directory.";
        return {};
    }
    diagnostics.clear();
    return LaunchPaths{
        .manifest = root / "vice.runtime.viceproj",
        .cooked_cache_root = root / "content",
    };
}

} // namespace

int main(const int argc, char** argv) {
    std::string diagnostics;
    LaunchPaths launch;

    if (argc == 1) {
        launch = packaged_launch_paths(std::filesystem::path{argv[0]}, diagnostics);
        if (launch.manifest.empty() || launch.cooked_cache_root.empty()) {
            std::cerr << "VICE Runtime could not resolve staged game output: " << diagnostics << '\n';
            return 1;
        }
    } else if (argc == 3) {
        launch.manifest = absolute_normalized(std::filesystem::path{argv[1]}, diagnostics);
        if (launch.manifest.empty()) {
            std::cerr << "VICE Runtime could not resolve the project manifest: " << diagnostics << '\n';
            return 1;
        }
        launch.cooked_cache_root = absolute_normalized(std::filesystem::path{argv[2]}, diagnostics);
        if (launch.cooked_cache_root.empty()) {
            std::cerr << "VICE Runtime could not resolve the cooked-cache root: " << diagnostics << '\n';
            return 1;
        }
    } else {
        std::cerr << "Usage: staged game target, or vice_player <project.viceproj> <cooked-cache-root>\n";
        return 2;
    }

    const auto loaded_project = vice::project::load_manifest(launch.manifest);
    if (!loaded_project.manifest.has_value()) {
        std::cerr << "VICE Runtime could not open the project: " << loaded_project.diagnostics << '\n';
        return 1;
    }

    const auto& project = *loaded_project.manifest;
    const auto startup_world = vice::project::resolve_cooked_startup_world(
        launch.cooked_cache_root,
        project
    );
    if (startup_world.empty()) {
        std::cerr << "VICE Runtime could not resolve the cooked startup World for the project.\n";
        return 1;
    }

    const auto loaded_world = vice::world::load_world_snapshot(startup_world);
    if (!loaded_world.snapshot.has_value()) {
        std::cerr
            << "VICE Runtime could not load the cooked startup World `"
            << startup_world.generic_string()
            << "`: "
            << loaded_world.diagnostics
            << '\n';
        return 1;
    }

    vice::world::World world;
    if (!world.restore(*loaded_world.snapshot)) {
        std::cerr
            << "VICE Runtime could not restore the cooked startup World `"
            << startup_world.generic_string()
            << "`.\n";
        return 1;
    }

    std::cout
        << project.game_name << '\n'
        << "Target: " << project.game_target << '\n'
        << "Project: " << project.name << '\n'
        << "Project ID: " << project.id.to_string() << '\n'
        << "Startup World: " << startup_world.generic_string() << '\n'
        << "Entities: " << world.entity_count() << '\n'
        << "Resources: " << world.resource_count() << '\n';

    return 0;
}
