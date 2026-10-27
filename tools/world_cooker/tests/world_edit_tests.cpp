#include <vice/platform/file.hpp>
#include <vice/tools/world_edit.hpp>
#include <vice/tools/world_cooker.hpp>

#include <array>
#include <barrier>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "World file edit test failed: " << message << '\n'; std::exit(1); }
}
void write(const std::filesystem::path& path, const std::string_view source) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out << source;
    out.close();
    require(static_cast<bool>(out), "write fixture");
}
std::string read(const std::filesystem::path& path) {
    const auto result = vice::platform::read_text_file(path);
    require(result.success, "read fixture");
    return result.contents;
}
constexpr std::string_view first = "90000000-0000-4000-8000-000000000201";
constexpr std::string_view second = "90000000-0000-4000-8000-000000000202";
constexpr std::string_view relation = "90000000-0000-4000-8000-000000000301";
constexpr std::string_view resource = "90000000-0000-4000-8000-000000000101";
std::string fixture() {
    return "# Preserve comments and whitespace when an edit is a no-op\nvice-world-source 1\n"
        "resource " + std::string{resource} + "\n"
        "region 1 10 1000000000000 0 0 1 0 0 reduced none\n"
        "region 2 20 1000000000100 0 0 2 0 0 full none\n"
        "entity " + std::string{first} + " spatial 1000000000000.25 0 0 1\n"
        "entity " + std::string{second} + " none\n";
}
vice::tools::world_authoring::EditRequest request(std::string_view source, std::string_view ops) {
    auto parsed = vice::tools::world_authoring::parse_edit_request(
        "vice-world-edit 1\nrevision \"" + vice::platform::text_revision(source) + "\"\n" + std::string{ops});
    require(parsed.request.has_value(), "parse request");
    return *parsed.request;
}
} // namespace

int main() {
    using namespace vice;
    using namespace tools::world_authoring;
    require(platform::text_revision("abc") ==
        "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 known vector");
    require(platform::text_revision("") ==
        "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "empty SHA-256 known vector");
    const auto root = std::filesystem::temp_directory_path() / ("vice-world-edit-" + world::PersistentId::random().to_string());
    std::filesystem::create_directories(root);
    const auto path = root / "main.vworld";
    const auto original = fixture();
    const auto created = platform::replace_text_file_if_unchanged(path, std::nullopt, original);
    require(created.state == platform::FileReplaceState::updated && read(path) == original, "atomic create if absent");
    require(platform::replace_text_file_if_unchanged(path, std::nullopt, "overwrite").state ==
        platform::FileReplaceState::conflict && read(path) == original, "create cannot overwrite existing source");
    const auto unchanged_time = std::filesystem::last_write_time(path);
    const auto noop = edit_file(path, request(original, ""), false);
    require(noop.success && !noop.changed && !noop.published && noop.code == "unchanged", "empty transaction is no-op");
    require(read(path) == original && std::filesystem::last_write_time(path) == unchanged_time,
        "no-op preserves exact source bytes and metadata");

    const std::string operations = "set_spatial " + std::string{first} + " 1000000000101.25 -2.5 3.75 2\n"
        "set_transform " + std::string{first} + " 0 0 0 1 2 3 4\n"
        "set_fidelity 2 reduced\nadd_relation " + std::string{first} + " " + std::string{relation} + " " + std::string{second} +
        "\nadd_resource " + std::string{first} + " render_mesh " + std::string{resource} + "\n";
    const auto transaction = request(original, operations);
    const auto preview = edit_file(path, transaction, true);
    require(preview.success && preview.changed && !preview.published && preview.validated_operations == 5, "valid dry-run");
    require(read(path) == original && std::filesystem::last_write_time(path) == unchanged_time, "dry-run does not mutate source");
    const auto applied = edit_file(path, transaction, false);
    require(applied.success && applied.published && applied.revision == preview.revision &&
        applied.proposed_source == preview.proposed_source, "apply matches dry-run candidate");
    require(read(path) == applied.proposed_source, "atomic publication exact content");
    const auto parsed = tools::world_cooker::parse_source_manifest(read(path));
    require(parsed.snapshot.has_value(), "edited source reparses");
    require(parsed.snapshot->entities[0].spatial_state->global_position.x == 1000000000101.25,
        "large-world submeter precision survives publication");
    require(parsed.snapshot->regions[1].fidelity == world::RegionFidelity::reduced, "explicit fidelity preserved");
    require(applied.affected_entities.size() == 2 && !applied.affected_regions.empty(), "bounded deterministic affected identities");
    const auto stale = edit_file(path, transaction, false);
    require(!stale.success && stale.code == "revision_conflict" && read(path) == applied.proposed_source, "stale edit rejected unchanged");
    const auto idempotent = edit_file(path, request(read(path), operations), false);
    require(idempotent.success && !idempotent.changed && !idempotent.published, "idempotent operations do not rewrite");

    const auto before_bad = read(path);
    const auto bad = request(before_bad, "set_fidelity 2 full\nset_transform " + std::string{first} + " 0 0 0 0 1 1 1\n");
    const auto rejected = edit_file(path, bad, false);
    require(!rejected.success && rejected.code == "transaction_rejected" && rejected.failed_operation == 1 &&
        read(path) == before_bad, "invalid second operation rejects whole file transaction");
    const auto rejected_preview = edit_file(path, bad, true);
    require(rejected_preview.code == rejected.code && rejected_preview.failed_operation == rejected.failed_operation,
        "dry-run and apply share validation");
    const auto removed = edit_file(path, request(before_bad, "clear_transform " + std::string{first} +
        "\nremove_relation " + std::string{first} + " " + std::string{relation} + " " + std::string{second} +
        "\nremove_resource " + std::string{first} + " render_mesh " + std::string{resource} + "\n"), false);
    require(removed.success && removed.validated_operations == 3, "removal and transform clearing");

    const std::string prefix = "vice-world-edit 1\nrevision " + platform::text_revision(original) + "\n";
    for (const auto& invalid : std::array<std::string, 7>{
        "vice-world-edit 2\n", "vice-world-edit 1\nrevision invalid\n",
        prefix + "set_fidelity -1 reduced\n", prefix + "set_fidelity 1 reduced extra\n",
        prefix + "unknown_operation 1\n", prefix + "set_spatial " + std::string{first} + " nan 0 0 1\n",
        prefix + "add_resource " + std::string{first} + " invalid_role " + std::string{resource} + "\n"}) {
        require(!parse_edit_request(invalid).request, "malformed or unsupported request rejected");
    }
    require(!parse_edit_request(std::string(max_edit_request_bytes + 1, ' ')).request, "request byte bound");
    std::string too_many = prefix;
    for (std::size_t i = 0; i <= max_transaction_operations; ++i) too_many += "set_fidelity 1 reduced\n";
    require(!parse_edit_request(too_many).request, "operation bound");
    auto missing_revision = transaction;
    missing_revision.expected_revision.clear();
    require(edit_file(path, missing_revision, false).code == "invalid_revision", "no wildcard mutation");
    require(edit_file(root / "cache.vworldc", transaction, false).code == "invalid_path", "cooked files cannot be edited");

    write(path, original);
    const auto race_request = request(original, "set_fidelity 1 full\n");
    std::barrier start{2};
    std::array<FileEditResult, 2> results;
    std::thread a{[&] { start.arrive_and_wait(); results[0] = edit_file(path, race_request, false); }};
    std::thread b{[&] { start.arrive_and_wait(); results[1] = edit_file(path, race_request, false); }};
    a.join(); b.join();
    require(results[0].success != results[1].success, "only one concurrent stale-base writer succeeds");
    const auto& loser = results[0].success ? results[1] : results[0];
    require(loser.code == "revision_conflict" || loser.code == "source_busy", "concurrent edit is explicit");
    const auto compare = platform::replace_text_file_if_unchanged(std::filesystem::canonical(path), original, "invalid");
    require(compare.state == platform::FileReplaceState::conflict, "publication compares full source bytes");
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        require(entry.path().filename().string().find(".vice-tmp-") == std::string::npos, "temporary files cleaned up");
    }
    write(path, std::string(platform::max_text_file_bytes + 1, ' '));
    require(!platform::read_text_file(path), "source byte bound");
    std::filesystem::remove_all(root);
    return 0;
}
