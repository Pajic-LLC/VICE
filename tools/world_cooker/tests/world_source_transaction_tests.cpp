#include <vice/tools/world_source_transaction.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "World source transaction test failed: " << message << '\n';
    std::exit(1);
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "open fixture for write");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    require(static_cast<bool>(output), "write fixture");
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    require(static_cast<bool>(input), "open fixture for read");
    return std::string{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

[[nodiscard]] vice::world::PersistentId id(const std::string_view text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    require(parsed.has_value(), "parse fixture persistent ID");
    return *parsed;
}

} // namespace

int main() {
    using namespace vice;
    using namespace tools::world_authoring;
    using namespace world;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        "vice_world_source_transaction_tests";
    require(!error, "resolve temp directory");
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    require(!error, "create temp directory");

    const auto source_path = root / "main.vworld";
    const std::string original_source =
        "vice-world-source 1\n\n"
        "resource 91000000-0000-4000-8000-000000000101\n"
        "region 1 10 1000000000000 -2000000000000 3000000000000 1 2 3 full none\n"
        "region 2 20 1000000000100 -2000000000100 3000000000100 4 5 6 full none\n"
        "entity 91000000-0000-4000-8000-000000000201 spatial 1000000000000.25 -2000000000000.5 3000000000000.75 1\n"
        "entity 91000000-0000-4000-8000-000000000202 spatial 1000000000101 -2000000000102 3000000000103 2\n";
    write_text(source_path, original_source);

    const auto loaded = load_source_document(source_path);
    require(loaded, "load valid authored World source");
    require(loaded.source == original_source, "source bytes preserved on load");
    require(
        loaded.revision == source_revision(original_source),
        "source revision is deterministic"
    );
    require(
        loaded.revision.starts_with("vice.world.source.v1-") && loaded.revision.size() == 53U,
        "source revision is versioned and fixed width"
    );

    const auto first_entity = id("91000000-0000-4000-8000-000000000201");
    Transaction edit;
    edit.operations = {
        SetEntitySpatial{
            .entity = first_entity,
            .spatial = SpatialState{
                .global_position = GlobalPosition{
                    1000000000123.25,
                    -2000000000456.5,
                    3000000000789.75,
                },
                .region = RegionId{2U},
            },
        },
        SetRegionFidelity{
            .region = RegionId{2U},
            .fidelity = RegionFidelity::reduced,
        },
    };

    const auto dry_run = apply_source_transaction(SourceTransactionRequest{
        .source_path = source_path,
        .expected_revision = loaded.revision,
        .transaction = edit,
        .dry_run = true,
    });
    require(dry_run, "dry-run transaction succeeds");
    require(dry_run.changed, "dry-run reports proposed change");
    require(!dry_run.published, "dry-run does not publish");
    require(dry_run.revision_before == loaded.revision, "dry-run reports original revision");
    require(dry_run.revision_after != loaded.revision, "dry-run reports proposed revision");
    require(read_text(source_path) == original_source, "dry-run leaves source byte-for-byte unchanged");

    const auto stale = apply_source_transaction(SourceTransactionRequest{
        .source_path = source_path,
        .expected_revision = "vice.world.source.v1-stale",
        .transaction = edit,
    });
    require(!stale, "stale source revision is rejected");
    require(stale.revision_before == loaded.revision, "stale result reports current revision");
    require(stale.diagnostics.find("stale") != std::string::npos, "stale rejection is explained");
    require(read_text(source_path) == original_source, "stale transaction leaves bytes unchanged");

    Transaction invalid;
    invalid.operations = {
        SetRegionFidelity{.region = RegionId{1U}, .fidelity = RegionFidelity::inactive},
        SetEntitySpatial{
            .entity = first_entity,
            .spatial = SpatialState{
                .global_position = GlobalPosition{1.0, 2.0, 3.0},
                .region = RegionId{999U},
            },
        },
    };
    const auto rejected = apply_source_transaction(SourceTransactionRequest{
        .source_path = source_path,
        .expected_revision = loaded.revision,
        .transaction = invalid,
    });
    require(!rejected, "invalid batch is rejected");
    require(
        rejected.diagnostics.find("operation 1") != std::string::npos,
        "invalid batch identifies rejected operation"
    );
    require(read_text(source_path) == original_source, "rejected batch is byte-preserving");

    auto lock_path = source_path;
    lock_path += ".vice-write-lock";
    std::filesystem::create_directory(lock_path, error);
    require(!error, "create simulated writer lock");
    const auto locked = apply_source_transaction(SourceTransactionRequest{
        .source_path = source_path,
        .expected_revision = loaded.revision,
        .transaction = edit,
    });
    require(!locked, "concurrent VICE writer lock is rejected");
    require(locked.diagnostics.find("locked") != std::string::npos, "writer lock rejection explained");
    std::filesystem::remove(lock_path, error);
    require(!error, "remove simulated writer lock");

    const auto published = apply_source_transaction(SourceTransactionRequest{
        .source_path = source_path,
        .expected_revision = loaded.revision,
        .transaction = edit,
    });
    require(published, "authored World transaction publishes");
    require(published.changed && published.published, "published transaction reports commit");
    require(published.applied_operations == 2U, "published operation count reported");
    require(published.revision_after == dry_run.revision_after, "dry-run and apply agree on revision");
    require(read_text(source_path) != original_source, "published source bytes changed");
    require(!std::filesystem::exists(lock_path), "writer lock released after publication");

    const auto after = load_source_document(source_path);
    require(after, "published source reparses");
    require(after.revision == published.revision_after, "published source revision matches result");
    const auto entity = std::find_if(
        after.snapshot.entities.begin(),
        after.snapshot.entities.end(),
        [&](const SnapshotEntity& value) { return value.persistent_id == first_entity; }
    );
    require(entity != after.snapshot.entities.end(), "edited entity still exists");
    require(entity->spatial_state.has_value(), "edited entity remains spatial");
    require(
        entity->spatial_state->global_position.x == 1000000000123.25 &&
        entity->spatial_state->global_position.y == -2000000000456.5 &&
        entity->spatial_state->global_position.z == 3000000000789.75,
        "large-world precision survives atomic publication"
    );
    require(entity->spatial_state->region == RegionId{2U}, "published entity region changed");
    const auto region = std::find_if(
        after.snapshot.regions.begin(),
        after.snapshot.regions.end(),
        [](const RegionDesc& value) { return value.id == RegionId{2U}; }
    );
    require(region != after.snapshot.regions.end(), "edited region still exists");
    require(region->fidelity == RegionFidelity::reduced, "published fidelity change preserved");

    const auto no_op = apply_source_transaction(SourceTransactionRequest{
        .source_path = source_path,
        .expected_revision = after.revision,
        .transaction = edit,
    });
    require(no_op, "idempotent repeated transaction succeeds");
    require(!no_op.changed && !no_op.published, "idempotent transaction avoids rewrite");
    require(no_op.revision_after == after.revision, "no-op preserves revision");

    const auto missing_precondition = apply_source_transaction(SourceTransactionRequest{
        .source_path = source_path,
        .transaction = edit,
    });
    require(!missing_precondition, "revision precondition is mandatory");
    require(
        missing_precondition.diagnostics.find("precondition") != std::string::npos,
        "missing revision precondition is explained"
    );

    std::filesystem::remove_all(root, error);
    require(!error, "clean temp directory");
    return 0;
}
