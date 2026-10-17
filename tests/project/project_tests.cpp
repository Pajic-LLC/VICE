#include <vice/project/project.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "project test failed: " << message << '\n';
    std::exit(1);
}

constexpr std::string_view valid_manifest = R"(vice-project 1
id 40000000-0000-4000-8000-000000000001
name "VICE Lab"
assets "assets"
startup_world "worlds/vice_lab.vworld"
)";

} // namespace

int main() {
    using namespace vice::project;

    const auto parsed = parse_manifest(valid_manifest);
    require(parsed.manifest.has_value(), "valid manifest parse");
    require(parsed.diagnostics.empty(), "valid manifest diagnostics");
    require(parsed.manifest->name == "VICE Lab", "project name");
    require(parsed.manifest->asset_root.generic_string() == "assets", "asset root");
    require(
        parsed.manifest->startup_world.generic_string() == "worlds/vice_lab.vworld",
        "startup World"
    );

    const std::string canonical = serialize_manifest(*parsed.manifest);
    require(canonical == valid_manifest, "deterministic serialization");
    const auto reparsed = parse_manifest(canonical);
    require(reparsed.manifest.has_value(), "canonical parse");
    require(*reparsed.manifest == *parsed.manifest, "canonical round trip");

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

    return 0;
}
