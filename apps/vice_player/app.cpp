#include <vice/project/project.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

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

} // namespace

int main(const int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: vice_player <project.viceproj> <cooked-cache-root>\n";
        return 2;
    }

    std::string diagnostics;
    const auto manifest_path = absolute_normalized(std::filesystem::path{argv[1]}, diagnostics);
    if (manifest_path.empty()) {
        std::cerr << "VICE Player could not resolve the project manifest: " << diagnostics << '\n';
        return 1;
    }

    const auto cooked_cache_root = absolute_normalized(std::filesystem::path{argv[2]}, diagnostics);
    if (cooked_cache_root.empty()) {
        std::cerr << "VICE Player could not resolve the cooked-cache root: " << diagnostics << '\n';
        return 1;
    }

    const auto loaded_project = vice::project::load_manifest(manifest_path);
    if (!loaded_project.manifest.has_value()) {
        std::cerr << "VICE Player could not open the project: " << loaded_project.diagnostics << '\n';
        return 1;
    }

    const auto& project = *loaded_project.manifest;
    const auto startup_world = vice::project::resolve_cooked_startup_world(cooked_cache_root, project);
    if (startup_world.empty()) {
        std::cerr << "VICE Player could not resolve the cooked startup World for the project.\n";
        return 1;
    }

    const auto loaded_world = vice::world::load_world_snapshot(startup_world);
    if (!loaded_world.snapshot.has_value()) {
        std::cerr
            << "VICE Player could not load the cooked startup World `"
            << startup_world.generic_string()
            << "`: "
            << loaded_world.diagnostics
            << '\n';
        return 1;
    }

    vice::world::World world;
    if (!world.restore(*loaded_world.snapshot)) {
        std::cerr
            << "VICE Player could not restore the cooked startup World `"
            << startup_world.generic_string()
            << "`.\n";
        return 1;
    }

    std::cout
        << "VICE Player\n"
        << "Game: " << project.game_name << '\n'
        << "Project: " << project.name << '\n'
        << "Project ID: " << project.id.to_string() << '\n'
        << "Startup World: " << startup_world.generic_string() << '\n'
        << "Entities: " << world.entity_count() << '\n'
        << "Resources: " << world.resource_count() << '\n';

    return 0;
}
