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

            if constexpr (std::is_same_v<Value, CreateSchedule>) {
                const auto &s = value.schedule;
                if (s.id.is_nil() || s.occurrence || s.failure != world::ScheduleFailure::none ||
                    s.event_result != world::WorldEventResult::success || s.event.data.schedule ||
                    (s.event.region.valid() && !find_region(candidate, s.event.region)) ||
                    !has_entity(candidate, s.event.subject) ||
                    (!s.event.target.is_nil() && !has_entity(candidate, s.event.target)) ||
                    (!s.event.object.is_nil() && !has_entity(candidate, s.event.object))) {
                    diagnostics = "Invalid fresh schedule or missing endpoint.";
                    return false;
                }
                if (std::any_of(candidate.schedules.begin(), candidate.schedules.end(),
                                [&](const auto &old) { return old.id == s.id; })) {
                    diagnostics = "Schedule ID already exists.";
                    return false;
                }
                for (const auto &parent : s.event.data.parents) {
                    const auto &h = candidate.causal_history;
                    const auto first = h.next_sequence - h.events.size();
                    if (parent.history != h.id || parent.sequence < first ||
                        parent.sequence >= h.next_sequence ||
                        h.events[static_cast<std::size_t>(parent.sequence - first)].id !=
                            parent.id) {
                        diagnostics = "Schedule creation requires retained causal parents.";
                        return false;
                    }
                }
                candidate.schedules.push_back(s);
                return true;
            } else if constexpr (std::is_same_v<Value, CancelSchedule>) {
                const auto found =
                    std::find_if(candidate.schedules.begin(), candidate.schedules.end(),
                                 [&](const auto &s) { return s.id == value.id; });
                if (found == candidate.schedules.end()) {
                    diagnostics = "Schedule does not exist.";
                    return false;
                }
                candidate.schedules.erase(found);
                return true;
            } else if constexpr (std::is_same_v<Value, SetWorldClock>) {
                if (!world::valid(value.clock)) {
                    diagnostics = "Invalid World clock.";
                    return false;
                }
                candidate.clock = value.clock;
                return true;
            } else if constexpr (std::is_same_v<Value, SetWorldMaterial>) {
                if(value.id.is_nil() || (value.definition && (value.definition->id!=value.id || !world::valid(*value.definition)))) {
                    diagnostics="Invalid World material definition.";return false;
                }
                const auto found=std::find_if(candidate.materials.begin(),candidate.materials.end(),[&](const auto& m){return m.id==value.id;});
                if(value.definition) {
                    if(found!=candidate.materials.end())*found=*value.definition;
                    else candidate.materials.push_back(*value.definition);
                } else if(found!=candidate.materials.end()) candidate.materials.erase(found);
                return true;
            } else if constexpr (std::is_same_v<Value, SetEntityMaterial>) {
                auto* entity=find_entity(candidate,value.entity);
                if(!entity){diagnostics="Material entity does not exist.";return false;}
                entity->material=value.material;return true;
            } else if constexpr (std::is_same_v<Value, SetEntitySpatial>) {
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
            } else if constexpr (std::is_same_v<Value, RegisterComponentType>) {
                if(std::any_of(candidate.component_types.begin(),candidate.component_types.end(),[&](const auto& s){return s.id==value.schema.id;})){diagnostics="Component schema already exists.";return false;}candidate.component_types.push_back(value.schema);return true;
            } else if constexpr (std::is_same_v<Value, SetComponent> ||
                                 std::is_same_v<Value, RemoveComponent>) {
                auto* e=find_entity(candidate,value.entity);if(!e){diagnostics="Component owner is missing.";return false;}
                const auto type=[&]{if constexpr(std::is_same_v<Value,SetComponent>)return value.payload.type;else return value.type;}();
                auto found=std::find_if(e->components.begin(),e->components.end(),[&](const auto& c){return c.type==type;});
                if constexpr(std::is_same_v<Value,SetComponent>){if(found==e->components.end())e->components.push_back(value.payload);else *found=value.payload;}
                else {if(found==e->components.end()){diagnostics="Component is missing.";return false;}e->components.erase(found);}return true;
            } else if constexpr (std::is_same_v<Value, SetCharacterAttachment>) {
                auto* entity=find_entity(candidate,value.entity);
                if(!entity || (value.attachment && !world::valid(*value.attachment))){diagnostics="Invalid character attachment.";return false;}
                entity->character_attachment=value.attachment;return true;
            } else if constexpr (std::is_same_v<Value, SetAnimation> ||
                                 std::is_same_v<Value, SetRigPart> ||
                                 std::is_same_v<Value, SetAudioEmitter>) {
                auto* e=find_entity(candidate,value.entity);if(!e || (value.value&&!world::valid(*value.value))){diagnostics="Invalid presentation edit.";return false;}
                if constexpr(std::is_same_v<Value,SetAnimation>)e->animation=value.value;
                else if constexpr(std::is_same_v<Value,SetRigPart>)e->rig_part=value.value;
                else e->audio_emitter=value.value;
                return true;
            } else if constexpr (std::is_same_v<Value, SetVehicle>) {
                auto* entity=find_entity(candidate,value.entity);
                if(!entity || (value.vehicle && !world::valid(*value.vehicle))){diagnostics="Invalid vehicle entity/profile.";return false;}
                entity->vehicle=value.vehicle;return true;
            } else if constexpr (std::is_same_v<Value, SetCharacter>) {
                auto* entity = find_entity(candidate,value.entity);
                if (!entity) { diagnostics = "Character entity does not exist."; return false; }
                if (value.character && !world::valid(*value.character)) { diagnostics = "Invalid character profile or motion."; return false; }
                entity->character = value.character; return true;
            } else if constexpr (std::is_same_v<Value, SetDependency>) {
                auto* entity = find_entity(candidate, value.entity);
                if (!entity) { diagnostics = "Dependency entity does not exist."; return false; }
                entity->dependency = value.dependency;
                return true;
            } else if constexpr (std::is_same_v<Value, SetDistanceLink>) {
                auto* entity = find_entity(candidate, value.entity);
                if (!entity) { diagnostics = "Entity does not exist: " + value.entity.to_string(); return false; }
                if (value.link && !world::valid(*value.link)) { diagnostics = "Distance link values are invalid."; return false; }
                entity->distance_link = value.link;
                return true;
            } else if constexpr (std::is_same_v<Value, SetRigidBox>) {
                auto* const entity = find_entity(candidate, value.entity);
                if (entity == nullptr || value.entity.is_nil()) {
                    diagnostics = "Rigid box entity does not exist: " + value.entity.to_string();
                    return false;
                }
                if (value.body && (!entity->spatial_state || !world::valid(*value.body))) {
                    diagnostics = "Rigid box requires a spatial entity and finite positive dimensions with valid motion/material data.";
                    return false;
                }
                entity->rigid_box = value.body;
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
            } else if constexpr (std::is_same_v<Value, SetRelationComponent> ||
                                 std::is_same_v<Value, RemoveRelationComponent>) {
                const auto edge=std::find_if(candidate.relations.begin(),candidate.relations.end(),[&](const auto& r){return r.subject==value.relation.subject&&r.kind==value.relation.kind&&r.object==value.relation.object;});
                if(edge==candidate.relations.end()){diagnostics="Relation does not exist.";return false;}
                const auto type=[&]{if constexpr(std::is_same_v<Value,SetRelationComponent>)return value.payload.type;else return value.type;}();
                const auto found=std::find_if(edge->components.begin(),edge->components.end(),[&](const auto& p){return p.type==type;});
                if constexpr(std::is_same_v<Value,SetRelationComponent>){if(found==edge->components.end())edge->components.push_back(value.payload);else *found=value.payload;}
                else {if(found==edge->components.end()){diagnostics="Relation payload does not exist.";return false;}edge->components.erase(found);}return true;
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
                const auto found=std::find_if(candidate.relations.begin(),candidate.relations.end(),[&](const auto& edge){return edge.subject==value.relation.subject&&edge.kind==value.relation.kind&&edge.object==value.relation.object;});
                if(found==candidate.relations.end())candidate.relations.push_back(value.relation);
                else if(!value.relation.components.empty()&&found->components!=value.relation.components){diagnostics="Relation already has different state.";return false;}
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
                std::erase_if(candidate.relations,[&](const auto& edge){return edge.subject==value.relation.subject&&edge.kind==value.relation.kind&&edge.object==value.relation.object;});
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
    std::string diagnostics,
    const std::optional<std::size_t> failed_operation = std::nullopt
) {
    return Result{
        .success = false,
        .changed = false,
        .applied_operations = 0U,
        .snapshot = authoritative,
        .diagnostics = std::move(diagnostics),
        .failed_operation = failed_operation,
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
                    operation_diagnostics,
                index
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
