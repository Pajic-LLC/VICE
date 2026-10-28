#include <vice/tools/world_edit.hpp>
#include "rigid_box_source.hpp"
#include "distance_link_source.hpp"
#include "dependency_source.hpp"
#include "character_source.hpp"
#include "vehicle_source.hpp"
#include <vice/world/presentation_io.hpp>
#include "material_source.hpp"

#include <vice/platform/file.hpp>
#include <vice/tools/world_cooker.hpp>

#include <algorithm>
#include <charconv>
#include <iomanip>
#include <locale>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace vice::tools::world_authoring {
namespace {
bool valid_revision(const std::string_view value) {
    return value.size() == 71U && value.starts_with("sha256:") &&
        std::all_of(value.begin() + 7, value.end(), [](const char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
}

bool read_id(std::istream& input, world::PersistentId& id) {
    std::string text;
    if (!(input >> text)) return false;
    const auto parsed = world::PersistentId::parse(text);
    if (!parsed || parsed->is_nil()) return false;
    id = *parsed;
    return true;
}

bool read_unsigned(std::istream& input, std::uint64_t& value) {
    std::string text;
    if (!(input >> text) || text.empty() ||
        !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value != 0;
}

bool read_fidelity(std::istream& input, world::RegionFidelity& value) {
    std::string text;
    if (!(input >> text)) return false;
    if (text == "inactive") value = world::RegionFidelity::inactive;
    else if (text == "reduced") value = world::RegionFidelity::reduced;
    else if (text == "full") value = world::RegionFidelity::full;
    else return false;
    return true;
}

bool read_reference(std::istream& input, world::ResourceReference& reference) {
    std::string role;
    if (!(input >> role)) return false;
    if (role == "render_mesh") reference.role = world::ResourceRole::render_mesh;
    else if (role == "render_material") reference.role = world::ResourceRole::render_material;
    else if (role == "reflection_environment") reference.role = world::ResourceRole::reflection_environment;
    else return false;
    return read_id(input, reference.resource.value);
}

std::optional<Operation> read_operation(const std::string& name, std::istream& input) {
    if(name=="set_material") {
        world::MaterialDefinition m;
        if(world_cooker::read_material(input,m))return SetWorldMaterial{m.id,m};
    } else if(name=="remove_material") {
        SetWorldMaterial op;if(read_id(input,op.id))return op;
    } else if(name=="set_entity_material" || name=="clear_entity_material") {
        SetEntityMaterial op;
        if(read_id(input,op.entity) && (name=="clear_entity_material" || read_id(input,op.material)))return op;
    } else if (name == "set_spatial") {
        SetEntitySpatial op;
        if (read_id(input, op.entity) &&
            (input >> op.spatial.global_position.x >> op.spatial.global_position.y >> op.spatial.global_position.z) &&
            read_unsigned(input, op.spatial.region.value)) return op;
    } else if (name == "set_transform") {
        SetEntityTransform op;
        world::AuthoredTransform transform;
        if (read_id(input, op.entity) &&
            (input >> transform.rotation.x >> transform.rotation.y >> transform.rotation.z >> transform.rotation.w >>
                transform.scale.x >> transform.scale.y >> transform.scale.z)) {
            op.transform = transform;
            return op;
        }
    } else if (name == "clear_transform") {
        SetEntityTransform op;
        if (read_id(input, op.entity)) return op;
    } else if(name=="set_animation"||name=="clear_animation"){
        SetAnimation op;if(!read_id(input,op.entity))return std::nullopt;if(name=="set_animation"){world::AnimationPlayer p;if(!world::read_animation(input,p))return std::nullopt;op.value=p;}return op;
    } else if(name=="set_rig_part"||name=="clear_rig_part"){
        SetRigPart op;if(!read_id(input,op.entity))return std::nullopt;if(name=="set_rig_part"){world::RigPart p;if(!world::read_rig_part(input,p))return std::nullopt;op.value=p;}return op;
    } else if(name=="set_audio_emitter"||name=="clear_audio_emitter"){
        SetAudioEmitter op;if(!read_id(input,op.entity))return std::nullopt;if(name=="set_audio_emitter"){world::AudioEmitter p;if(!world::read_audio_emitter(input,p))return std::nullopt;op.value=p;}return op;
    } else if(name=="set_character_attachment" || name=="clear_character_attachment") {
        SetCharacterAttachment op;if(!read_id(input,op.entity))return std::nullopt;
        if(name=="set_character_attachment"){world::CharacterAttachment a;if(!read_id(input,a.carrier) || !(input>>a.offset.x>>a.offset.y>>a.offset.z) || !world::valid(a))return std::nullopt;op.attachment=a;}return op;
    } else if (name == "set_vehicle" || name == "clear_vehicle") {
        SetVehicle op;
        if(!read_id(input,op.entity))return std::nullopt;
        if(name=="set_vehicle"){world::Vehicle v;if(!world_cooker::read_vehicle(input,v))return std::nullopt;op.vehicle=v;}
        return op;
    } else if (name == "set_character" || name == "clear_character") {
        SetCharacter op;
        if (!read_id(input,op.entity)) return std::nullopt;
        if (name == "set_character") { world::Character c; if (!world_cooker::read_character(input,c)) return std::nullopt; op.character = c; }
        return op;
    } else if (name == "set_dependency" || name == "clear_dependency") {
        SetDependency op;
        if (!read_id(input, op.entity)) return std::nullopt;
        if (name == "set_dependency") {
            world::Dependency d;
            if (!world_cooker::read_dependency(input, d)) return std::nullopt;
            op.dependency = d;
        }
        return op;
    } else if (name == "set_distance_link" || name == "clear_distance_link") {
        SetDistanceLink op;
        if (!read_id(input, op.entity)) return std::nullopt;
        if (name == "set_distance_link") {
            world::DistanceLink link;
            if (!world_cooker::read_distance_link(input, link)) return std::nullopt;
            op.link = link;
        }
        return op;
    } else if (name == "set_rigid_box" || name == "clear_rigid_box") {
        SetRigidBox op;
        if (!read_id(input, op.entity)) return std::nullopt;
        if (name == "set_rigid_box") {
            world::RigidBox body;
            if (!world_cooker::read_rigid_box(input, body)) return std::nullopt;
            op.body = body;
        }
        return op;
    } else if (name == "set_fidelity") {
        SetRegionFidelity op;
        if (read_unsigned(input, op.region.value) && read_fidelity(input, op.fidelity)) return op;
    } else if (name == "add_relation" || name == "remove_relation") {
        world::SnapshotRelation relation;
        if (read_id(input, relation.subject) && read_id(input, relation.kind) && read_id(input, relation.object)) {
            if (name == "add_relation") return AddRelation{relation};
            return RemoveRelation{relation};
        }
    } else if (name == "add_resource" || name == "remove_resource") {
        world::PersistentId entity;
        world::ResourceReference reference;
        if (read_id(input, entity) && read_reference(input, reference)) {
            if (name == "add_resource") return AddResourceReference{entity, reference};
            return RemoveResourceReference{entity, reference};
        }
    }
    return std::nullopt;
}

void affected(FileEditResult& result, const Transaction& transaction, const world::WorldSnapshot& before, const world::WorldSnapshot& after) {
    for (const auto& op : transaction.operations) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SetWorldMaterial>) {
                const auto add=[&](const world::WorldSnapshot& s) {
                    for(const auto& e:s.entities) if(e.material==value.id) {
                        result.affected_entities.push_back(e.persistent_id);
                        if(e.spatial_state)result.affected_regions.push_back(e.spatial_state->region);
                    }
                };add(before);add(after);
            } else if constexpr (std::is_same_v<T, SetRegionFidelity>) {
                result.affected_regions.push_back(value.region);
            } else if constexpr (std::is_same_v<T, AddRelation> || std::is_same_v<T, RemoveRelation>) {
                result.affected_entities.push_back(value.relation.subject);
                result.affected_entities.push_back(value.relation.object);
            } else if constexpr (std::is_same_v<T, SetDependency>) {
                result.affected_entities.push_back(value.entity);
                const auto add = [&](const world::Dependency& d) {
                    if (!d.provider.is_nil()) result.affected_entities.push_back(d.provider);
                    if (!d.gate.is_nil()) result.affected_entities.push_back(d.gate);
                };
                if (value.dependency) add(*value.dependency);
                const auto old = std::find_if(before.entities.begin(), before.entities.end(), [&](const auto& e) { return e.persistent_id == value.entity; });
                if (old != before.entities.end() && old->dependency) add(*old->dependency);
            } else if constexpr (std::is_same_v<T, SetDistanceLink>) {
                result.affected_entities.push_back(value.entity);
                const auto add_endpoints = [&](const world::DistanceLink& link) {
                    for (const auto id : {link.first, link.second}) {
                        result.affected_entities.push_back(id);
                        const auto found = std::find_if(before.entities.begin(), before.entities.end(), [&](const auto& e) { return e.persistent_id == id; });
                        if (found != before.entities.end() && found->spatial_state) result.affected_regions.push_back(found->spatial_state->region);
                    }
                };
                if (value.link) add_endpoints(*value.link);
                const auto previous = std::find_if(before.entities.begin(), before.entities.end(), [&](const auto& e) { return e.persistent_id == value.entity; });
                if (previous != before.entities.end() && previous->distance_link) add_endpoints(*previous->distance_link);
            } else {
                result.affected_entities.push_back(value.entity);
                if constexpr (std::is_same_v<T, SetCharacterAttachment>) {
                    const auto add=[&](const world::WorldSnapshot& snapshot){
                        const auto e=std::find_if(snapshot.entities.begin(),snapshot.entities.end(),[&](const auto& item){return item.persistent_id==value.entity;});
                        if(e!=snapshot.entities.end() && e->character_attachment) {
                            result.affected_entities.push_back(e->character_attachment->carrier);
                            const auto parent=std::find_if(snapshot.entities.begin(),snapshot.entities.end(),[&](const auto& item){return item.persistent_id==e->character_attachment->carrier;});
                            if(parent!=snapshot.entities.end() && parent->spatial_state)result.affected_regions.push_back(parent->spatial_state->region);
                        }
                    };add(before);add(after);
                }
                if constexpr (std::is_same_v<T,SetAnimation> || std::is_same_v<T,SetRigPart> || std::is_same_v<T,SetAudioEmitter> || std::is_same_v<T, SetCharacterAttachment> || std::is_same_v<T, SetVehicle> || std::is_same_v<T, SetCharacter> || std::is_same_v<T, SetEntityMaterial>) {
                    const auto previous = std::find_if(before.entities.begin(),before.entities.end(),[&](const auto& e) { return e.persistent_id == value.entity; });
                    if (previous != before.entities.end() && previous->spatial_state) result.affected_regions.push_back(previous->spatial_state->region);
                }
                if constexpr (std::is_same_v<T, SetEntitySpatial>) {
                    result.affected_regions.push_back(value.spatial.region);
                    const auto previous = std::find_if(before.entities.begin(), before.entities.end(),
                        [&](const auto& entity) { return entity.persistent_id == value.entity; });
                    if (previous != before.entities.end() && previous->spatial_state) {
                        result.affected_regions.push_back(previous->spatial_state->region);
                    }
                }
            }
        }, op);
    }
    // Include conservative downstream supply impact from both sides of a rewire.
    std::unordered_map<world::PersistentId, std::vector<world::PersistentId>, world::PersistentIdHash> users;
    const auto index = [&](const world::WorldSnapshot& snapshot) {
        for (const auto& e : snapshot.entities) {
            if (!e.dependency) continue;
            for (const auto ref : {e.dependency->provider, e.dependency->gate}) {
                if (!ref.is_nil()) users[ref].push_back(e.persistent_id);
            }
        }
    };
    index(before); index(after);
    if (!users.empty()) {
        std::unordered_set<world::PersistentId, world::PersistentIdHash> impacted(result.affected_entities.begin(), result.affected_entities.end());
        std::vector<world::PersistentId> queue(impacted.begin(), impacted.end());
        for (std::size_t i = 0; i < queue.size(); ++i) {
            const auto found = users.find(queue[i]);
            if (found == users.end()) continue;
            for (const auto dependent : found->second) if (impacted.insert(dependent).second) queue.push_back(dependent);
        }
        result.affected_entities.assign(queue.begin(), queue.end());
        const auto regions = [&](const world::WorldSnapshot& snapshot) {
            for (const auto& e : snapshot.entities) {
                if (impacted.contains(e.persistent_id) && e.spatial_state) result.affected_regions.push_back(e.spatial_state->region);
            }
        };
        regions(before); regions(after);
    }
    std::sort(result.affected_entities.begin(), result.affected_entities.end(), [](const auto& a, const auto& b) {
        return a.bytes() < b.bytes();
    });
    result.affected_entities.erase(std::unique(result.affected_entities.begin(), result.affected_entities.end()), result.affected_entities.end());
    std::sort(result.affected_regions.begin(), result.affected_regions.end(), [](auto a, auto b) { return a.value < b.value; });
    result.affected_regions.erase(std::unique(result.affected_regions.begin(), result.affected_regions.end()), result.affected_regions.end());
}
} // namespace

EditRequestResult parse_edit_request(const std::string_view source) {
    EditRequestResult result;
    if (source.size() > max_edit_request_bytes) {
        result.diagnostics = "World edit request exceeds 65536 bytes.";
        return result;
    }
    EditRequest request;
    std::istringstream lines{std::string{source}};
    lines.imbue(std::locale::classic());
    bool header = false;
    bool revision = false;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(lines, line)) {
        ++line_number;
        std::istringstream input{line};
        input.imbue(std::locale::classic());
        std::string name;
        if (!(input >> name) || name.starts_with('#')) continue;
        bool valid = false;
        if (!header) {
            std::string version;
            valid = name == "vice-world-edit" && (input >> version) && version == "1";
            header = valid;
        } else if (!revision) {
            valid = name == "revision" && (input >> std::quoted(request.expected_revision)) &&
                valid_revision(request.expected_revision);
            revision = valid;
        } else {
            if (request.transaction.operations.size() >= max_transaction_operations) {
                result.diagnostics = "World edit request exceeds 256 operations.";
                result.failed_operation = request.transaction.operations.size();
                return result;
            }
            const auto operation = read_operation(name, input);
            valid = operation.has_value();
            if (valid) request.transaction.operations.push_back(*operation);
            else result.failed_operation = request.transaction.operations.size();
        }
        std::string trailing;
        if (!valid || (input >> trailing)) {
            result.diagnostics = "Invalid World edit record at line " + std::to_string(line_number) + ".";
            if (revision && !result.failed_operation && !request.transaction.operations.empty()) {
                result.failed_operation = request.transaction.operations.size() - 1;
            }
            return result;
        }
    }
    if (!header || !revision) {
        result.diagnostics = "World edit request requires vice-world-edit 1 and a SHA-256 revision.";
        return result;
    }
    result.request = std::move(request);
    return result;
}

FileEditResult edit_file(const std::filesystem::path& path, const EditRequest& request, const bool dry_run) {
    FileEditResult result;
    result.dry_run = dry_run;
    auto reject = [&](std::string code, std::string message) {
        result.success = false;
        result.changed = false;
        result.code = std::move(code);
        result.diagnostics = std::move(message);
        return result;
    };
    if (path.extension() != ".vworld") return reject("invalid_path", "Only authored .vworld source can be edited.");
    if (!valid_revision(request.expected_revision)) return reject("invalid_revision", "A valid source revision is required.");
    std::error_code error;
    const auto canonical_path = std::filesystem::canonical(path, error);
    if (error || canonical_path.extension() != ".vworld") return reject("source_unavailable", "Authored World source is unavailable.");
    const auto source = platform::read_text_file(canonical_path);
    if (!source) return reject("source_unavailable", source.diagnostics);
    result.previous_revision = platform::text_revision(source.contents);
    result.revision = result.previous_revision;
    if (result.revision.empty()) return reject("source_unavailable", "Could not compute source revision.");
    if (result.revision != request.expected_revision) return reject("revision_conflict", "Source revision changed. Inspect the World and rebase the transaction.");
    const auto parsed = world_cooker::parse_source_manifest(source.contents);
    if (!parsed.snapshot) return reject("invalid_world", parsed.diagnostics);
    const auto proposed = apply(*parsed.snapshot, request.transaction);
    if (!proposed) {
        result.failed_operation = proposed.failed_operation;
        return reject("transaction_rejected", proposed.diagnostics);
    }
    const auto& next_source = proposed.changed ? proposed.serialized_source : source.contents;
    const auto next_revision = platform::text_revision(next_source);
    if (next_revision.empty()) return reject("source_unavailable", "Could not compute proposed source revision.");
    if (!dry_run) {
        const auto publication = platform::replace_text_file_if_unchanged(canonical_path, source.contents, next_source);
        using State = platform::FileReplaceState;
        if (publication.state == State::conflict) return reject("revision_conflict", publication.diagnostics);
        if (publication.state == State::busy) return reject("source_busy", publication.diagnostics);
        if (publication.state == State::unavailable) return reject("publication_failed", publication.diagnostics);
        result.published = publication.state == State::updated;
    }
    result.success = true;
    result.changed = proposed.changed;
    result.code = dry_run ? "previewed" : (result.published ? "applied" : "unchanged");
    result.revision = next_revision;
    result.proposed_source = next_source;
    result.validated_operations = proposed.applied_operations;
    affected(result, request.transaction, *parsed.snapshot, proposed.snapshot);
    return result;
}
} // namespace vice::tools::world_authoring
