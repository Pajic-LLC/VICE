#include <vice/tools/world_authoring.hpp>
#include <vice/tools/world_cooker.hpp>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "World authoring test failed: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] vice::world::PersistentId id(const std::string_view value) {
    const auto parsed = vice::world::PersistentId::parse(value);
    require(parsed.has_value(), "parse persistent ID fixture");
    return *parsed;
}

[[nodiscard]] vice::world::WorldSnapshot fixture() {
    using namespace vice::world;

    WorldSnapshot snapshot;
    snapshot.resources = {
        ResourceId{id("90000000-0000-4000-8000-000000000101")},
        ResourceId{id("90000000-0000-4000-8000-000000000102")},
    };
    snapshot.regions = {
        RegionDesc{
            .id = RegionId{1U},
            .simulation_island = SimulationIslandId{10U},
            .origin = GlobalPosition{1000000000000.0, -2000000000000.0, 3000000000000.0},
            .streaming_cell = StreamingCellId{1, 2, 3},
            .fidelity = RegionFidelity::full,
        },
        RegionDesc{
            .id = RegionId{2U},
            .simulation_island = SimulationIslandId{20U},
            .origin = GlobalPosition{1000000000100.0, -2000000000100.0, 3000000000100.0},
            .streaming_cell = StreamingCellId{4, 5, 6},
            .fidelity = RegionFidelity::full,
        },
    };
    snapshot.entities = {
        SnapshotEntity{
            .persistent_id = id("90000000-0000-4000-8000-000000000201"),
            .spatial_state = SpatialState{
                .global_position = GlobalPosition{1000000000000.25, -2000000000000.5, 3000000000000.75},
                .region = RegionId{1U},
            },
        },
        SnapshotEntity{
            .persistent_id = id("90000000-0000-4000-8000-000000000202"),
            .spatial_state = SpatialState{
                .global_position = GlobalPosition{1000000000101.0, -2000000000102.0, 3000000000103.0},
                .region = RegionId{2U},
            },
        },
    };
    return snapshot;
}

} // namespace

int main() {
    using namespace vice;
    using namespace tools::world_authoring;
    using namespace world;

    const auto original = fixture();
    std::string original_diagnostics;
    const auto original_source = tools::world_cooker::serialize_source_manifest(
        original,
        &original_diagnostics
    );
    require(!original_source.empty(), "fixture serializes");

    const auto first = id("90000000-0000-4000-8000-000000000201");
    const auto second = id("90000000-0000-4000-8000-000000000202");
    const auto relation_kind = id("90000000-0000-4000-8000-000000000301");
    const ResourceReference mesh_reference{
        .role = ResourceRole::render_mesh,
        .resource = ResourceId{id("90000000-0000-4000-8000-000000000101")},
    };
    const SnapshotRelation relation{
        .subject = first,
        .kind = relation_kind,
        .object = second,
    };

    Transaction transaction;
    transaction.operations = {
        SetEntitySpatial{
            .entity = first,
            .spatial = SpatialState{
                .global_position = GlobalPosition{
                    1000000000123.25,
                    -2000000000456.5,
                    3000000000789.75,
                },
                .region = RegionId{2U},
            },
        },
        SetEntityTransform{
            .entity = first,
            .transform = AuthoredTransform{
                .rotation = AuthoredRotation{0.0F, 0.0F, 0.0F, 1.0F},
                .scale = AuthoredScale{2.0F, 3.0F, 4.0F},
            },
        },
        SetRegionFidelity{.region = RegionId{2U}, .fidelity = RegionFidelity::reduced},
        AddRelation{.relation = relation},
        AddResourceReference{.entity = first, .reference = mesh_reference},
    };

    const auto applied = apply(original, transaction);
    require(static_cast<bool>(applied), "multi-operation transaction succeeds");
    require(applied.changed, "multi-operation transaction changes World");
    require(applied.applied_operations == transaction.operations.size(), "all operations applied");
    require(!applied.serialized_source.empty(), "transaction returns deterministic source");

    const auto reparsed = tools::world_cooker::parse_source_manifest(applied.serialized_source);
    require(reparsed.snapshot.has_value(), "transaction output reparses");
    const auto& changed = *reparsed.snapshot;
    require(changed.entities[0].spatial_state.has_value(), "edited entity remains spatial");
    require(
        changed.entities[0].spatial_state->global_position.x == 1000000000123.25,
        "large-world X precision survives transaction"
    );
    require(
        changed.entities[0].spatial_state->global_position.y == -2000000000456.5,
        "large-world Y precision survives transaction"
    );
    require(changed.entities[0].spatial_state->region == RegionId{2U}, "region assignment changed");
    require(changed.entities[0].authored_transform.has_value(), "authored transform added");
    require(changed.regions[1].fidelity == RegionFidelity::reduced, "fidelity changed explicitly");
    require(changed.relations.size() == 1U && changed.relations.front() == relation, "relation added");
    require(
        changed.entities[0].resource_references.size() == 1U &&
            changed.entities[0].resource_references.front() == mesh_reference,
        "resource reference added"
    );

    Transaction invalid;
    invalid.operations = {
        SetRegionFidelity{.region = RegionId{1U}, .fidelity = RegionFidelity::inactive},
        SetEntityTransform{
            .entity = first,
            .transform = AuthoredTransform{
                .rotation = AuthoredRotation{0.0F, 0.0F, 0.0F, 0.0F},
                .scale = AuthoredScale{1.0F, 1.0F, 1.0F},
            },
        },
    };
    const auto rejected = apply(original, invalid);
    require(!static_cast<bool>(rejected), "invalid operation rejects whole transaction");
    require(!rejected.changed && rejected.applied_operations == 0U, "rejected transaction applies nothing");
    require(rejected.diagnostics.find("operation 1") != std::string::npos, "failure identifies operation index");
    std::string rejected_diagnostics;
    require(
        tools::world_cooker::serialize_source_manifest(rejected.snapshot, &rejected_diagnostics) == original_source,
        "rejected transaction preserves authoritative snapshot"
    );

    Transaction non_finite;
    non_finite.operations = {
        SetEntitySpatial{
            .entity = first,
            .spatial = SpatialState{
                .global_position = GlobalPosition{
                    std::numeric_limits<double>::infinity(),
                    0.0,
                    0.0,
                },
                .region = RegionId{1U},
            },
        },
    };
    require(!apply(original, non_finite), "non-finite global position rejected");

    Transaction idempotent;
    idempotent.operations = {
        AddRelation{.relation = relation},
        AddResourceReference{.entity = first, .reference = mesh_reference},
    };
    const auto first_idempotent = apply(original, idempotent);
    require(first_idempotent && first_idempotent.changed, "first idempotent add changes World");
    const auto second_idempotent = apply(first_idempotent.snapshot, idempotent);
    require(second_idempotent && !second_idempotent.changed, "repeated add is a deterministic no-op");

    Transaction remove;
    remove.operations = {
        RemoveRelation{.relation = relation},
        RemoveResourceReference{.entity = first, .reference = mesh_reference},
        SetEntityTransform{.entity = first, .transform = std::nullopt},
    };
    const auto removed = apply(applied.snapshot, remove);
    require(static_cast<bool>(removed), "remove transaction succeeds");
    require(removed.snapshot.relations.empty(), "relation removed");
    require(removed.snapshot.entities[0].resource_references.empty(), "resource reference removed");
    require(!removed.snapshot.entities[0].authored_transform.has_value(), "authored transform cleared");

    Transaction oversized;
    oversized.operations.resize(
        max_transaction_operations + 1U,
        SetRegionFidelity{.region = RegionId{1U}, .fidelity = RegionFidelity::full}
    );
    const auto oversized_result = apply(original, oversized);
    require(!oversized_result, "bounded operation count enforced");
    require(
        oversized_result.diagnostics.find("operation limit") != std::string::npos,
        "bounded transaction failure is explained"
    );

    return 0;
}
