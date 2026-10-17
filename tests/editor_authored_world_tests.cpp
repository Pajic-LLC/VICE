#include <vice/editor/project_build.hpp>
#include <vice/editor/project_session.hpp>
#include <vice/project/project.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "editor authored World test failed: " << message << '\n';
    std::exit(1);
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "text file open");
    output << text;
    require(static_cast<bool>(output), "text file write");
}

[[nodiscard]] std::filesystem::path temp_root() {
    std::error_code error;
    const auto temp = std::filesystem::temp_directory_path(error);
    require(!error, "temporary directory");
    const auto root = temp / "vice_editor_authored_world_test_45000000";
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root / "project" / "assets" / "worlds", error);
    require(!error, "temporary project tree");
    return root;
}

} // namespace

int main() {
    using namespace vice;

    const auto root = temp_root();
    const auto project_root = root / "project";
    const auto asset_root = project_root / "assets";
    const auto manifest_path = project_root / "vice.viceproj";
    const auto main_world = asset_root / "worlds" / "main.vworld";
    const auto alternate_world = asset_root / "worlds" / "alternate.vworld";
    const auto saved_as_world = asset_root / "worlds" / "saved-as.vworld";

    write_text(
        manifest_path,
        "vice-project 1\n"
        "id 45000000-0000-4000-8000-000000000135\n"
        "name \"Authored Test\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    write_text(
        main_world,
        "vice-world-source 1\n\n"
        "region 1 1 0 0 0 0 0 0 full bounds -10 -10 -10 10 10 10\n"
    );
    write_text(
        alternate_world,
        "vice-world-source 1\n\n"
        "region 1 1 0 0 0 0 0 0 full bounds -20 -20 -20 20 20 20\n"
        "region 2 2 30 0 0 1 0 0 reduced bounds -5 -5 -5 5 5 5\n"
    );

    std::string diagnostics;
    auto session = editor::ProjectSession::open(
        manifest_path,
        root / "cache",
        root / "state",
        &diagnostics
    );
    require(session.has_value(), "project session opens");
    require(diagnostics.empty(), "project session diagnostics");

    editor::WorldDocument document;
    require(document.open_authored(main_world, editor::DirtyReplacePolicy::discard, &diagnostics), "open authored World");
    require(document.authored(), "document is authored");
    require(document.world().set_region_fidelity(world::RegionId{1}, world::RegionFidelity::reduced), "edit authored World");
    document.mark_dirty();
    require(document.save_authored(&diagnostics), "save authored World");
    require(!document.dirty(), "save clears dirty state");

    editor::WorldDocument reopened;
    require(reopened.open_authored(main_world, editor::DirtyReplacePolicy::discard, &diagnostics), "reopen authored World");
    const auto region = reopened.world().region(world::RegionId{1});
    require(region.has_value(), "reopened region exists");
    require(region->fidelity == world::RegionFidelity::reduced, "authored edit round trips");

    require(reopened.save_authored_as(saved_as_world, &diagnostics), "Save As authored World");
    require(std::filesystem::is_regular_file(saved_as_world), "Save As source exists");
    editor::WorldDocument saved_as;
    require(saved_as.open_authored(saved_as_world, editor::DirtyReplacePolicy::discard, &diagnostics), "Save As source reopens");

    require(
        !session->set_startup_world(std::filesystem::path{"../outside.vworld"}, &diagnostics),
        "startup traversal rejected"
    );
    require(
        !session->set_startup_world(std::filesystem::path{"worlds/main.vworldc"}, &diagnostics),
        "cooked startup rejected"
    );
    require(
        !session->set_startup_world(std::filesystem::path{"worlds/missing.vworld"}, &diagnostics),
        "missing startup rejected"
    );
    require(
        session->set_startup_world(std::filesystem::path{"worlds/alternate.vworld"}, &diagnostics),
        "startup World changes"
    );

    const auto reloaded_manifest = project::load_manifest(manifest_path);
    require(reloaded_manifest.manifest.has_value(), "changed project manifest reloads");
    require(
        reloaded_manifest.manifest->startup_world.generic_string() == "worlds/alternate.vworld",
        "startup World persists"
    );

    const auto built = editor::build_project(*session);
    require(static_cast<bool>(built), "project builds selected startup World");
    require(built.region_count == 2U, "build follows selected startup World");

    std::error_code error;
    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
}
