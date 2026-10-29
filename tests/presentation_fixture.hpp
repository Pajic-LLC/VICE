#pragma once
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vice/presentation/system.hpp>
#include <vice/tools/presentation_cooker.hpp>
#include <vice/tools/project_mesh_cook.hpp>
#include <vice/tools/world_authoring.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>
namespace fixture {
using namespace vice;
inline void require(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "Presentation: " << message << '\n';
        std::exit(1);
    }
}
inline world::PersistentId id(const char *text) { return *world::PersistentId::parse(text); }
inline const auto actor = id("a2000000-0000-4000-8000-000000000003"),
                  car = id("a2000000-0000-4000-8000-000000000002"),
                  camera = id("a2000000-0000-4000-8000-000000000004"),
                  beacon = id("a4000000-0000-4000-8000-000000000010"),
                  rig = id("a4000000-0000-4000-8000-000000000200");
inline std::string text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}
struct Data {
    std::filesystem::path path;
    project::Workspace workspace;
    world::World world;
    Data() {
        path = std::filesystem::temp_directory_path() /
               ("vice-presentation-" + world::PersistentId::random().to_string());
        std::filesystem::create_directories(path / "assets");
        const auto source = std::filesystem::path{VICE_PRESENTATION_EXAMPLE} / "assets";
        for (const auto &e : std::filesystem::recursive_directory_iterator(source)) {
            if (!e.is_regular_file())
                continue;
            const auto relative = e.path().lexically_relative(source);
            std::filesystem::create_directories((path / "assets" / relative).parent_path());
            std::filesystem::copy_file(e.path(), path / "assets" / relative);
        }
        workspace.asset_root = path / "assets";
        workspace.cooked_root = path / "cooked";
        std::filesystem::create_directories(workspace.cooked_root);
        const auto parsed = tools::world_cooker::parse_source_manifest(
            text(workspace.asset_root / "worlds/presentation.vworld"));
        if (!parsed.snapshot)
            std::cerr << parsed.diagnostics;
        require(parsed.snapshot && world.restore(*parsed.snapshot),
                "parse imported-character World");
        const auto cooked = tools::cook_project_presentation(workspace);
        if (!cooked)
            std::cerr << cooked.diagnostics;
        require(static_cast<bool>(cooked), "cook rig and sounds");
        require(static_cast<bool>(tools::cook_project_meshes(workspace)),
                "cook imported part meshes");
    }
    ~Data() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
inline void rehash(std::vector<std::byte> &bytes) {
    std::uint64_t h = 14695981039346656037ULL;
    for (std::size_t i = 40; i < bytes.size(); ++i) {
        h ^= std::to_integer<std::uint8_t>(bytes[i]);
        h *= 1099511628211ULL;
    }
    for (std::size_t i = 0; i < 8; ++i)
        bytes[32 + i] = static_cast<std::byte>((h >> (8 * i)) & 255);
}
} // namespace fixture
