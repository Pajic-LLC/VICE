#include <vice/dev/cli.hpp>
#include <vice/platform/file.hpp>
#include <vice/world/persistent_id.hpp>

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "World edit CLI test failed: " << message << '\n'; std::exit(1); }
}
void write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file << text;
    require(static_cast<bool>(file), "write fixture");
}
bool contains(const vice::dev::CommandResult& result, std::string_view text) {
    return result.json.find(text) != std::string::npos;
}
} // namespace

int main() {
    using namespace vice;
    const auto root = std::filesystem::temp_directory_path() / ("vice-world-cli-" + world::PersistentId::random().to_string());
    std::filesystem::create_directories(root);
    const auto path = root / "main.vworld";
    const auto request = root / "move.viceedit";
    const std::string original = "vice-world-source 1\nregion 1 10 1000000000000 0 0 0 0 0 reduced none\n"
        "entity 90000000-0000-4000-8000-000000000201 spatial 1000000000000.25 0 0 1\n";
    write(path, original);
    const auto path_text = path.string();
    const auto request_text = request.string();
    const std::array<std::string_view, 3> inspect_args{"world", "inspect", path_text};
    const auto inspected = dev::execute(inspect_args);
    require(inspected.exit_code == 0 && contains(inspected, "\"source_revision\":\"" + platform::text_revision(original)),
        "inspect returns exact source revision");
    const auto request_source = "vice-world-edit 1\nrevision " + platform::text_revision(original) +
        "\nset_spatial 90000000-0000-4000-8000-000000000201 1000000000001.25 2.5 3.75 1\n";
    write(request, request_source);
    const std::array<std::string_view, 5> preview_args{"world", "edit", path_text, request_text, "--dry-run"};
    const auto preview = dev::execute(preview_args);
    require(preview.exit_code == 0 && contains(preview, "\"code\":\"previewed\"") &&
        contains(preview, "\"published\":false"), "structured preview success");
    require(platform::read_text_file(path).contents == original, "preview leaves file unchanged");
    require(dev::execute(preview_args).json == preview.json, "deterministic repeated preview response");
    const std::array<std::string_view, 5> apply_args{"world", "edit", path_text, request_text, "--apply"};
    const auto applied = dev::execute(apply_args);
    require(applied.exit_code == 0 && contains(applied, "\"code\":\"applied\"") &&
        contains(applied, "\"published\":true"), "structured apply success");
    const auto updated = platform::read_text_file(path).contents;
    const auto after = dev::execute(inspect_args);
    require(contains(after, "1000000000001.25") && contains(after, platform::text_revision(updated)),
        "inspect observes edited position and new revision");
    const auto stale = dev::execute(apply_args);
    require(stale.exit_code == dev::exit_validation && contains(stale, "\"code\":\"revision_conflict\"") &&
        platform::read_text_file(path).contents == updated, "stale request cannot overwrite source");
    write(request, "vice-world-edit 1\nrevision " + platform::text_revision(updated) +
        "\nset_fidelity 1 full\nset_transform 90000000-0000-4000-8000-000000000201 0 0 0 0 1 1 1\n");
    const auto invalid = dev::execute(apply_args);
    require(invalid.exit_code == dev::exit_validation && contains(invalid, "\"failed_operation\":1") &&
        platform::read_text_file(path).contents == updated, "batch invariant error identifies operation and preserves file");
    const std::array<std::string_view, 4> missing_mode{"world", "edit", path_text, request_text};
    require(dev::execute(missing_mode).exit_code == dev::exit_usage, "mutation requires explicit mode");
    write(request, "invalid request");
    require(contains(dev::execute(apply_args), "\"code\":\"invalid_request\""), "parser rejection is structured");
    std::filesystem::remove(request);
    require(dev::execute(apply_args).exit_code == dev::exit_unavailable, "missing request is structured");
    std::filesystem::remove_all(root);
    return 0;
}
