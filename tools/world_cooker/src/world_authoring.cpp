#include <vice/tools/world_authoring.hpp>

#include <vice/tools/world_cooker.hpp>

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>

namespace vice::tools::world_authoring {
namespace {

[[nodiscard]] world::SnapshotEntity* find_entity(
    world::WorldSnapshot& snapshot,
    const world::PersistentId id
) {
    const auto iterator = std::find_if(
        snapshot.entities.begin(),
        snapshot.entities.end(),
        [&](const world::SnapshotEntity& entity) { return entity.persistent_id == id; }
    );
    return iterator == snapshot.entities.end() ? nullptr : &*iterator;
}

[[nodiscard]] bool has_entity(
    const world::WorldSnapshot& snapshot,
    const world::PersistentId id
) {
    return std::any_of(
        snapshot.entities.begin(),
        snapshot.entities.end(),
        [&](const world::SnapshotEntity& entity) { return entity.persistent_id == id; }
    );
}

[[nodiscard]] world::RegionDesc* find_region(
    world::WorldSnapshot& snapshot,
    const world::RegionId id
) {
    const auto iterator = std::find_if(
        snapshot.regions.begin(),
        snapshot.regions.end(),
        [&](const world::RegionDesc& region) { return region.id == id; }
    );
    return iterator == snapshot.regions.end() ? nullptr : &*iterator;
}

[[nodiscard]] bool has_resource(
    const world::WorldSnapshot& snapshot,
    const world::ResourceId id
) {
    return std::find(snapshot.resources.begin(), snapshot.resources.end(), id) != snapshot.resources.end();
}

[[nodiscard]] bool valid_fidelity(const world::RegionFidelity fidelity) noexcept {
    return fidelity == world::RegionFidelity::inactive ||
        fidelity == world::RegionFidelity::reduced ||
        fidelity == world::RegionFidelity::full;
}

[[nodiscard]] bool valid_relation_identity(const world::SnapshotRelation& relation) noexcept {
    return !relation.subject.is_nil() && !relation.kind.is_nil() && !relation.object.is_nil();
}

[[nodiscard]] bool apply_operation(
    world::WorldSnapshot& candidate,
    const Operation& operation,
    std::string& diagnostics
) {
    return std::visit(
        [&](const auto& value) -> bool {
            using Value = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<Value, SetEntitySpatial>) {
                if (value.entity.is_nil()) {
                    diagnostics = "Entity ID is nil.";
                    return false;
                }
                auto* const entity = find_entity(candidate, value.entity);
                if (entity == nullptr) {
                    diagnostics = "Entity does not exist: " + value.entity.to_string();
                    return false;
                }
                if (!world::finite(value.spatial.global_position)) {
                    diagnostics = "Entity global position must be finite.";
                    return false;
                }
                if (!value.spatial.region.valid() || find_region(candidate, value.spatial.region) == nullptr) {
                    diagnostics = "Entity region does not exist: " +
                        std::to_string(value.spatial.region.value);
                    return false;
                }
                entity->spatial_state = value.spatial;
                return true;
            } else if constexpr (std::is_same_v<Value, SetEntityTransform>) {
                if (value.entity.is_nil()) {
                    diagnostics = "Entity ID is nil.";
                    return false;
                }
                auto* const entity = find_entity(candidate, value.entity);
                if (entity == nullptr) {
                    diagnostics = "Entity does not exist: " + value.entity.to_string();
                    return false;
                }
                if (value.transform.has_value() && !world::valid(*value.transform)) {
                    diagnostics = "Authored transform is invalid.";
                    return false;
                }
                entity->authored_transform = value.transform;
                return true;
            } else if constexpr (std::is_same_v<Value, SetRegionFidelity>) {
                if (!value.region.valid()) {
                    diagnostics = "Region ID is invalid.";
                    return false;
                }
                auto* const region = find_region(candidate, value.region);
                if (region == nullptr) {
                    diagnostics = "Region does not exist: " + std::to_string(value.region.value);
                    return false;
                }
                if (!valid_fidelity(value.fidelity)) {
                    diagnostics = "Region fidelity is invalid.";
                    return false;
                }
                region->fidelity = value.fidelity;
                return true;
            } else if constexpr (std::is_same_v<Value, AddRelation>) {
                if (!valid_relation_identity(value.relation)) {
                    diagnostics = "Relation identity contains a nil persistent ID.";
                    return false;
                }
                if (!has_entity(candidate, value.relation.subject)) {
                    diagnostics = "Relation subject does not exist: " + value.relation.subject.to_string();
                    return false;
                }
                if (!has_entity(candidate, value.relation.object)) {
                    diagnostics = "Relation object does not exist: " + value.relation.object.to_string();
                    return false;
                }
                if (std::find(candidate.relations.begin(), candidate.relations.end(), value.relation) ==
                    candidate.relations.end()) {
                    candidate.relations.push_back(value.relation);
                }
                return true;
            } else if constexpr (std::is_same_v<Value, RemoveRelation>) {
                if (!valid_relation_identity(value.relation)) {
                    diagnostics = "Relation identity contains a nil persistent ID.";
                    return false;
                }
                if (!has_entity(candidate, value.relation.subject)) {
                    diagnostics = "Relation subject does not exist: " + value.relation.subject.to_string();
                    return false;
                }
                if (!has_entity(candidate, value.relation.object)) {
                    diagnostics = "Relation object does not exist: " + value.relation.object.to_string();
                    return false;
                }
                std::erase(candidate.relations, value.relation);
                return true;
            } else if constexpr (std::is_same_v<Value, AddResourceReference>) {
                if (value.entity.is_nil()) {
                    diagnostics = "Entity ID is nil.";
                    return false;
                }
                auto* const entity = find_entity(candidate, value.entity);
                if (entity == nullptr) {
                    diagnostics = "Entity does not exist: " + value.entity.to_string();
                    return false;
                }
                if (!world::valid(value.reference.role) || !value.reference.resource.valid()) {
                    diagnostics = "Resource reference is invalid.";
                    return false;
                }
                if (!has_resource(candidate, value.reference.resource)) {
                    diagnostics = "Referenced resource does not exist: " +
                        value.reference.resource.value.to_string();
                    return false;
                }
                if (std::find(
                        entity->resource_references.begin(),
                        entity->resource_references.end(),
                        value.reference
                    ) == entity->resource_references.end()) {
                    entity->resource_references.push_back(value.reference);
                }
                return true;
            } else if constexpr (std::is_same_v<Value, RemoveResourceReference>) {
                if (value.entity.is_nil()) {
                    diagnostics = "Entity ID is nil.";
                    return false;
                }
                auto* const entity = find_entity(candidate, value.entity);
                if (entity == nullptr) {
                    diagnostics = "Entity does not exist: " + value.entity.to_string();
                    return false;
                }
                if (!world::valid(value.reference.role) || !value.reference.resource.valid()) {
                    diagnostics = "Resource reference is invalid.";
                    return false;
                }
                std::erase(entity->resource_references, value.reference);
                return true;
            }
            diagnostics = "Unsupported World authoring operation.";
            return false;
        },
        operation
    );
}

[[nodiscard]] Result fail(
    const world::WorldSnapshot& authoritative,
    std::string diagnostics
) {
    return Result{
        .success = false,
        .changed = false,
        .applied_operations = 0U,
        .snapshot = authoritative,
        .diagnostics = std::move(diagnostics),
    };
}

} // namespace

Result apply(
    const world::WorldSnapshot& authoritative,
    const Transaction& transaction
) {
    if (transaction.operations.size() > max_transaction_operations) {
        return fail(
            authoritative,
            "World authoring transaction exceeds the operation limit of " +
                std::to_string(max_transaction_operations) + "."
        );
    }

    std::string original_diagnostics;
    const auto original_source = world_cooker::serialize_source_manifest(
        authoritative,
        &original_diagnostics
    );
    if (original_source.empty()) {
        return fail(
            authoritative,
            "Authoritative World is invalid before editing: " + original_diagnostics
        );
    }

    auto candidate = authoritative;
    for (std::size_t index = 0U; index < transaction.operations.size(); ++index) {
        std::string operation_diagnostics;
        if (!apply_operation(candidate, transaction.operations[index], operation_diagnostics)) {
            return fail(
                authoritative,
                "World authoring operation " + std::to_string(index) + " rejected: " +
                    operation_diagnostics
            );
        }
    }

    std::string candidate_diagnostics;
    auto serialized = world_cooker::serialize_source_manifest(candidate, &candidate_diagnostics);
    if (serialized.empty()) {
        return fail(
            authoritative,
            "World authoring transaction violates authoritative World invariants: " +
                candidate_diagnostics
        );
    }

    const auto canonical = world_cooker::parse_source_manifest(serialized);
    if (!canonical.snapshot.has_value()) {
        return fail(
            authoritative,
            "World authoring transaction could not be reparsed after serialization: " +
                canonical.diagnostics
        );
    }

    const bool changed = serialized != original_source;
    return Result{
        .success = true,
        .changed = changed,
        .applied_operations = transaction.operations.size(),
        .snapshot = *canonical.snapshot,
        .serialized_source = std::move(serialized),
    };
}

} // namespace vice::tools::world_authoring
