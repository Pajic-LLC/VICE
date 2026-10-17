#include <vice/project/project.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "project test failed: " << message << '\n';
    std::exit(1);
}

constexpr std::string_view legacy_manifest = R"(vice-project 1
id 40000000-0000-4000-8000-000000000001
name "VICE Lab"
assets "assets"
startup_world "worlds/vice_lab.vworld"
)";

constexpr std::string_view canonical_manifest = R"(vice-project 1
id 40000000-0000-4000-8000-000000000001
name "VICE Lab"
game_name "VICE Lab"
assets "assets"
startup_world "worlds/vice_lab.vworld"
)";

} // namespace

int main() {
    using namespace vice::project;

    const auto parsed = parse_manifest(legacy_manifest);
    require(parsed.manifest.has_value(), "legacy manifest parse");
    require(parsed.diagnostics.empty(), "legacy manifest diagnostics");
    require(parsed.manifest->name == "VICE Lab", "project name");
    require(parsed.manifest->game_name == "VICE Lab", "legacy game name fallback");
    require(parsed.manifest->asset_root.generic_string() == "assets", "asset root");
    require(
        parsed.manifest->startup_world.generic_string() == "worlds/vice_lab.vworld",
        "startup World"
    );

    const std::string canonical = serialize_manifest(*parsed.manifest);
    require(canonical == canonical_manifest, "deterministic serialization");
    const auto reparsed = parse_manifest(canonical);
    require(reparsed.manifest.has_value(), "canonical parse");
    require(*reparsed.manifest == *parsed.manifest, "canonical round trip");

    const auto custom_game = parse_manifest(
        "vice-project 1\n"
        "id 40000000-0000-4000-8000-000000000001\n"
        "name \"Internal Project\"\n"
        "game_name \"Palma Bay\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    require(custom_game.manifest.has_value(), "explicit game name parse");
    require(custom_game.manifest->game_name == "Palma Bay", "explicit game name value");
    require(!parse_manifest(
        "vice-project 1\n"
        "id 40000000-0000-4000-8000-000000000001\n"
        "name \"Internal Project\"\n"
        "game_name \"\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    ).manifest.has_value(), "empty explicit game name rejected");

    const auto id = ProjectId::parse("40000000-0000-4000-8000-000000000001");
    require(id.has_value(), "project ID parse");
    require(id->to_string() == "40000000-0000-4000-8000-000000000001", "project ID round trip");
    require(!ProjectId::parse("00000000-0000-0000-0000-000000000000").has_value(), "zero ID rejected");
    require(!ProjectId::parse("not-a-project-id").has_value(), "invalid ID rejected");

    require(!parse_manifest(
        "vice-project 1\n"
        "id 40000000-0000-4000-8000-000000000001\n"
        "name \"One\"\n"
        "name \"Two\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    ).manifest.has_value(), "duplicate field rejected");

    require(!parse_manifest(
        "vice-project 1\n"
        "id 40000000-0000-4000-8000-000000000001\n"
        "name \"Missing startup\"\n"
        "assets \"assets\"\n"
    ).manifest.has_value(), "missing field rejected");

    require(!parse_manifest(
        "vice-project 1\n"
        "id 40000000-0000-4000-8000-000000000001\n"
        "name \"Absolute\"\n"
        "assets \"/tmp/assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    ).manifest.has_value(), "absolute asset root rejected");

    require(!parse_manifest(
        "vice-project 1\n"
        "id 40000000-0000-4000-8000-000000000001\n"
        "name \"Drive absolute\"\n"
        "assets \"C:/game/assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    ).manifest.has_value(), "drive path rejected");

    require(!parse_manifest(
        "vice-project 1\n"
        "id 40000000-0000-4000-8000-000000000001\n"
        "name \"Traversal\"\n"
        "assets \"../assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    ).manifest.has_value(), "asset traversal rejected");

    require(!parse_manifest(
        "vice-project 1\n"
        "id 40000000-0000-4000-8000-000000000001\n"
        "name \"Traversal\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/../outside.vworld\"\n"
    ).manifest.has_value(), "startup traversal rejected");

    require(!parse_manifest(
        "vice-project 1\n"
        "id 40000000-0000-4000-8000-000000000001\n"
        "name \"Backslash\"\n"
        "assets \"assets\\\\nested\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    ).manifest.has_value(), "non-portable separator rejected");

    require(!parse_manifest(
        "vice-project 1\n"
        "id 40000000-0000-4000-8000-000000000001\n"
        "name \"Wrong startup type\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworldc\"\n"
    ).manifest.has_value(), "cooked startup World rejected");

    const std::filesystem::path first_manifest =
        std::filesystem::path{"root"} / "first" / "game.viceproj";
    const std::filesystem::path second_manifest =
        std::filesystem::path{"root"} / "moved" / "game.viceproj";

    require(
        resolve_asset_root(first_manifest, *parsed.manifest).generic_string() == "root/first/assets",
        "first asset resolution"
    );
    require(
        resolve_startup_world(first_manifest, *parsed.manifest).generic_string() ==
            "root/first/assets/worlds/vice_lab.vworld",
        "first startup resolution"
    );
    require(
        resolve_asset_root(second_manifest, *parsed.manifest).generic_string() == "root/moved/assets",
        "moved asset resolution"
    );
    require(
        resolve_startup_world(second_manifest, *parsed.manifest).generic_string() ==
            "root/moved/assets/worlds/vice_lab.vworld",
        "moved startup resolution"
    );

    Manifest alternate_startup = *parsed.manifest;
    alternate_startup.startup_world = "worlds/alternate.vworld";
    require(
        resolve_startup_world(first_manifest, alternate_startup).generic_string() ==
            "root/first/assets/worlds/alternate.vworld",
        "updated startup World resolution"
    );

    const std::filesystem::path cooked_cache = std::filesystem::path{"cache"} / "projects";
    const std::string project_key = "40000000-0000-4000-8000-000000000001";
    require(
        resolve_cooked_root(cooked_cache, *parsed.manifest).generic_string() ==
            "cache/projects/" + project_key,
        "project-scoped cooked root"
    );
    require(
        resolve_cooked_startup_world(cooked_cache, *parsed.manifest).generic_string() ==
            "cache/projects/" + project_key + "/worlds/vice_lab.vworldc",
        "cooked startup World"
    );
    require(
        resolve_cooked_root(cooked_cache, *parsed.manifest) ==
            resolve_cooked_root(cooked_cache, *reparsed.manifest),
        "project move keeps cooked identity"
    );
    Manifest no_id = *parsed.manifest;
    no_id.id = {};
    require(resolve_cooked_root(cooked_cache, no_id).empty(), "missing project ID rejects cooked root");
    require(
        resolve_cooked_startup_world({}, *parsed.manifest).empty(),
        "empty cooked cache rejects startup path"
    );

    std::error_code temp_error;
    const auto temp_root = std::filesystem::temp_directory_path(temp_error);
    require(!temp_error, "temporary directory resolution");
    const auto temp_manifest = temp_root / "vice_project_manifest_test_40000000.viceproj";
    {
        std::ofstream output{temp_manifest, std::ios::binary | std::ios::trunc};
        require(static_cast<bool>(output), "temporary manifest open");
        output << legacy_manifest;
        require(static_cast<bool>(output), "temporary manifest write");
    }
    const auto loaded = load_manifest(temp_manifest);
    require(loaded.manifest.has_value(), "manifest file load");
    require(*loaded.manifest == *parsed.manifest, "manifest file load value");

    Manifest saved_manifest = *loaded.manifest;
    saved_manifest.name = "Settings Test";
    saved_manifest.game_name = "Settings Game";
    saved_manifest.startup_world = "worlds/settings.vworld";
    std::string save_diagnostics;
    require(save_manifest(temp_manifest, saved_manifest, &save_diagnostics), "manifest file save");
    require(save_diagnostics.empty(), "manifest save diagnostics");
    const auto saved = load_manifest(temp_manifest);
    require(saved.manifest.has_value(), "saved manifest reload");
    require(*saved.manifest == saved_manifest, "saved manifest value");

    const auto missing_parent = temp_root / "vice_project_missing_parent_40000000" / "game.viceproj";
    require(!save_manifest(missing_parent, saved_manifest, &save_diagnostics), "missing parent save rejected");
    require(!save_diagnostics.empty(), "missing parent save diagnostic");

    std::filesystem::remove(temp_manifest, temp_error);
    require(!temp_error, "temporary manifest cleanup");

    return 0;
}
