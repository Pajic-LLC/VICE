#include <vice/tools/world_cooker.hpp>
#include "rigid_box_source.hpp"
#include "distance_link_source.hpp"
#include "dependency_source.hpp"
#include "character_source.hpp"
#include "vehicle_source.hpp"
#include <vice/world/presentation_io.hpp>
#include "causal_source.hpp"
#include "material_source.hpp"
#include <algorithm>

#include <vice/world/world.hpp>

#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace vice::tools::world_cooker {
namespace {

constexpr float degrees_to_radians = 0.01745329251994329577F;

[[nodiscard]] std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::optional<world::PersistentId> parse_id(const std::string& text) {
    const auto parsed = world::PersistentId::parse(text);
    if (!parsed.has_value() || parsed->is_nil()) return std::nullopt;
    return parsed;
}

[[nodiscard]] std::optional<world::RegionFidelity> parse_fidelity(
    const std::string_view text
) noexcept {
    if (text == "inactive") return world::RegionFidelity::inactive;
    if (text == "reduced") return world::RegionFidelity::reduced;
    if (text == "full") return world::RegionFidelity::full;
    return std::nullopt;
}

[[nodiscard]] std::optional<world::ResourceRole> parse_resource_role(
    const std::string_view text
) noexcept {
    if (text == "render_mesh") return world::ResourceRole::render_mesh;
    if (text == "render_material") return world::ResourceRole::render_material;
    if (text == "reflection_environment") return world::ResourceRole::reflection_environment;
    return std::nullopt;
}

} // namespace

ParseResult parse_source_manifest(const std::string_view source) {
    ParseResult result;
    world::WorldSnapshot parsed;
    std::istringstream stream{std::string{source}};
    std::string line;
    std::size_t line_number = 0;
    bool header_seen = false;

    const auto fail = [&](const std::string& message) {
        result.diagnostics = "World source line " + std::to_string(line_number) + ": " + message;
    };

    while (std::getline(stream, line)) {
        ++line_number;
        if (const auto comment = line.find('#'); comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(std::move(line));
        if (line.empty()) continue;

        std::istringstream values{line};
        std::string keyword;
        values >> keyword;

        if (!header_seen) {
            std::uint32_t version = 0;
            if (keyword != "vice-world-source" || !(values >> version) || version != 1U) {
                fail("expected `vice-world-source 1`.");
                return result;
            }
            std::string trailing;
            if (values >> trailing) {
                fail("source header has trailing data.");
                return result;
            }
            header_seen = true;
            continue;
        }

        if(keyword=="material") {
            world::MaterialDefinition m;
            if(parsed.materials.size()>=world::max_world_materials || !read_material(values,m)) {fail("invalid or oversized material catalog.");return result;}
            parsed.materials.push_back(std::move(m));
        } else if(keyword=="causal_history") {
            if(!parsed.causal_history.id.is_nil() || !read_causal_id(values,parsed.causal_history.id) || parsed.causal_history.id.is_nil() || !read_causal_u64(values,parsed.causal_history.next_sequence)) {fail("invalid or duplicate causal history header.");return result;}
        } else if(keyword=="causal_event") {
            world::CausalEvent e;
            if(parsed.causal_history.events.size()>=world::causal_history_capacity || !read_causal_event(values,e)){fail("invalid or oversized causal event history.");return result;}
            parsed.causal_history.events.push_back(e);
        } else if (keyword == "component") {
            std::string id_text;
            world::ComponentTypeDesc type;
            if (!(values >> id_text >> type.version >> std::quoted(type.name))) {
                fail("invalid component declaration.");
                return result;
            }
            const auto id = parse_id(id_text);
            if (!id.has_value()) {
                fail("component ID is invalid.");
                return result;
            }
            type.id = world::ComponentTypeId{*id};
            std::string marker;if(values>>marker){if(marker!="fields"||!world::read_component_fields(values,type.fields)){fail("invalid component fields.");return result;}}
            if(!world::valid_component_type(type)){fail("invalid component schema.");return result;}
            parsed.component_types.push_back(std::move(type));
        } else if (keyword == "resource") {
            std::string id_text;
            if (!(values >> id_text)) {
                fail("resource requires an ID.");
                return result;
            }
            const auto id = parse_id(id_text);
            if (!id.has_value()) {
                fail("resource ID is invalid.");
                return result;
            }
            parsed.resources.push_back(world::ResourceId{*id});
        } else if (keyword == "region") {
            world::RegionDesc region;
            std::string fidelity_text;
            std::string bounds_mode;
            if (!(values >> region.id.value >> region.simulation_island.value >>
                    region.origin.x >> region.origin.y >> region.origin.z >>
                    region.streaming_cell.x >> region.streaming_cell.y >> region.streaming_cell.z >>
                    fidelity_text >> bounds_mode)) {
                fail("invalid region declaration.");
                return result;
            }
            const auto fidelity = parse_fidelity(fidelity_text);
            if (!fidelity.has_value()) {
                fail("region fidelity is invalid.");
                return result;
            }
            region.fidelity = *fidelity;
            if (bounds_mode == "bounds") {
                world::RegionBounds bounds;
                if (!(values >> bounds.min.x >> bounds.min.y >> bounds.min.z >>
                        bounds.max.x >> bounds.max.y >> bounds.max.z)) {
                    fail("region bounds are incomplete.");
                    return result;
                }
                region.bounds = bounds;
            } else if (bounds_mode != "none") {
                fail("region bounds mode must be `none` or `bounds`.");
                return result;
            }
            parsed.regions.push_back(region);
        } else if (keyword == "entity") {
            std::string id_text;
            std::string spatial_mode;
            if (!(values >> id_text >> spatial_mode)) {
                fail("invalid entity declaration.");
                return result;
            }
            const auto id = parse_id(id_text);
            if (!id.has_value()) {
                fail("entity persistent ID is invalid.");
                return result;
            }
            world::SnapshotEntity entity;
            entity.persistent_id = *id;
            if (spatial_mode == "spatial") {
                world::SpatialState spatial;
                if (!(values >> spatial.global_position.x >> spatial.global_position.y >>
                        spatial.global_position.z >> spatial.region.value)) {
                    fail("entity spatial state is incomplete.");
                    return result;
                }
                entity.spatial_state = spatial;
            } else if (spatial_mode != "none") {
                fail("entity mode must be `none` or `spatial`.");
                return result;
            }

            std::string metadata;
            while (values >> metadata) {
                if(metadata=="state"){world::ComponentPayload p;if(entity.components.size()>=world::max_entity_components||!world::read_component_payload(values,p)){fail("invalid component payload.");return result;}entity.components.push_back(std::move(p));continue;}
                if(metadata=="animation"){world::AnimationPlayer p;if(entity.animation||!world::read_animation(values,p)){fail("invalid animation.");return result;}entity.animation=p;continue;}
                if(metadata=="rig_part"){world::RigPart p;if(entity.rig_part||!world::read_rig_part(values,p)){fail("invalid rig part.");return result;}entity.rig_part=p;continue;}
                if(metadata=="audio_emitter"){world::AudioEmitter p;if(entity.audio_emitter||!world::read_audio_emitter(values,p)){fail("invalid audio emitter.");return result;}entity.audio_emitter=p;continue;}
                if(metadata=="character_attachment") {
                    world::CharacterAttachment a;std::string parent;
                    if(entity.character_attachment || !(values>>parent>>a.offset.x>>a.offset.y>>a.offset.z)) {fail("invalid character attachment.");return result;}
                    const auto id=world::PersistentId::parse(parent);if(!id){fail("invalid carrier UUID.");return result;}a.carrier=*id;
                    if(!world::valid(a)){fail("invalid attachment offset.");return result;}entity.character_attachment=a;continue;
                }
                if (metadata == "vehicle") {
                    world::Vehicle v;
                    if(entity.vehicle || !read_vehicle(values,v)){fail("invalid or duplicate vehicle record.");return result;}
                    entity.vehicle=v;continue;
                }
                if (metadata == "character") {
                    world::Character c;
                    if (entity.character || !read_character(values,c)) { fail("invalid or duplicate character record."); return result; }
                    entity.character = c; continue;
                }
                if (metadata == "dependency") {
                    world::Dependency d;
                    if (entity.dependency || !read_dependency(values, d)) { fail("invalid or duplicate dependency record."); return result; }
                    entity.dependency = d; continue;
                }
                if (metadata == "distance_link") {
                    world::DistanceLink link;
                    if (entity.distance_link || !read_distance_link(values, link)) {
                        fail("invalid or duplicate distance_link record."); return result;
                    }
                    entity.distance_link = link;
                    continue;
                }
                if (metadata == "rigid_box") {
                    world::RigidBox box;
                    if (entity.rigid_box || !entity.spatial_state || !read_rigid_box(values, box)) {
                        fail("invalid or duplicate rigid_box; a spatial entity and valid motion, extents, velocities and material values are required.");
                        return result;
                    }
                    entity.rigid_box = box;
                    continue;
                }
                if (metadata == "transform") {
                    if (entity.authored_transform.has_value()) {
                        fail("entity authored transform is duplicated.");
                        return result;
                    }
                    if (!entity.spatial_state.has_value()) {
                        fail("authored transform requires a spatial entity.");
                        return result;
                    }
                    world::AuthoredTransform transform;
                    if (!(values >> transform.rotation.x >> transform.rotation.y >>
                            transform.rotation.z >> transform.rotation.w >>
                            transform.scale.x >> transform.scale.y >> transform.scale.z)) {
                        fail("entity authored transform is incomplete.");
                        return result;
                    }
                    entity.authored_transform = transform;
                    continue;
                }

                if (metadata == "camera") {
                    if (entity.camera.has_value()) {
                        fail("entity camera is duplicated.");
                        return result;
                    }
                    if (!entity.spatial_state.has_value()) {
                        fail("camera requires a spatial entity.");
                        return result;
                    }

                    std::string projection_text;
                    std::string primary_text;
                    float fov_degrees = 0.0F;
                    world::Camera camera;
                    if (!(values >> projection_text >> fov_degrees >> camera.near_plane >>
                            camera.far_plane >> primary_text)) {
                        fail("entity camera is incomplete.");
                        return result;
                    }
                    if (projection_text != "perspective") {
                        fail("camera projection must be `perspective`.");
                        return result;
                    }
                    if (primary_text == "primary") {
                        camera.primary = true;
                    } else if (primary_text == "secondary") {
                        camera.primary = false;
                    } else {
                        fail("camera role must be `primary` or `secondary`.");
                        return result;
                    }
                    camera.projection = world::CameraProjection::perspective;
                    camera.vertical_fov_radians = fov_degrees * degrees_to_radians;
                    if (!world::valid(camera)) {
                        fail("entity camera values are invalid.");
                        return result;
                    }
                    entity.camera = camera;
                    continue;
                }

                if (metadata == "light") {
                    if (entity.light.has_value()) {
                        fail("entity light is duplicated.");
                        return result;
                    }
                    if (!entity.spatial_state.has_value()) {
                        fail("light requires a spatial entity.");
                        return result;
                    }
                    std::string type_text;
                    std::string enabled_text;
                    world::Light light;
                    if (!(values >> type_text >> light.color.red >> light.color.green >>
                            light.color.blue >> light.intensity)) {
                        fail("entity light is incomplete.");
                        return result;
                    }
                    if (type_text == "directional") {
                        light.type = world::LightType::directional;
                        light.range = 0.0F;
                    } else if (type_text == "point") {
                        light.type = world::LightType::point;
                        if (!(values >> light.range)) {
                            fail("point light range is missing.");
                            return result;
                        }
                    } else {
                        fail("light type must be `directional` or `point`.");
                        return result;
                    }
                    if (!(values >> enabled_text)) {
                        fail("light enabled state is missing.");
                        return result;
                    }
                    if (enabled_text == "enabled") light.enabled = true;
                    else if (enabled_text == "disabled") light.enabled = false;
                    else {
                        fail("light state must be `enabled` or `disabled`.");
                        return result;
                    }
                    if (!world::valid(light)) {
                        fail("entity light values are invalid.");
                        return result;
                    }
                    entity.light = light;
                    continue;
                }

                if (metadata == "reflection_probe") {
                    if (entity.reflection_probe.has_value()) {
                        fail("entity reflection probe is duplicated.");
                        return result;
                    }
                    if (!entity.spatial_state.has_value()) {
                        fail("reflection probe requires a spatial entity.");
                        return result;
                    }
                    std::string enabled_text;
                    world::ReflectionProbe probe;
                    if (!(values >> probe.half_extents.x >> probe.half_extents.y >> probe.half_extents.z >>
                            probe.blend_distance >> probe.priority >> enabled_text)) {
                        fail("entity reflection probe is incomplete.");
                        return result;
                    }
                    if (enabled_text == "enabled") probe.enabled = true;
                    else if (enabled_text == "disabled") probe.enabled = false;
                    else {
                        fail("reflection probe state must be `enabled` or `disabled`.");
                        return result;
                    }
                    if (!world::valid(probe)) {
                        fail("entity reflection probe values are invalid.");
                        return result;
                    }
                    entity.reflection_probe = probe;
                    continue;
                }

                if(metadata=="material") {
                    if(!entity.material.is_nil() || !read_causal_id(values,entity.material) || entity.material.is_nil()) {fail("invalid or duplicate entity material.");return result;}
                    continue;
                }
                if (metadata == "resource") {
                    std::string role_text;
                    std::string resource_text;
                    if (!(values >> role_text >> resource_text)) {
                        fail("entity resource reference is incomplete.");
                        return result;
                    }
                    const auto role = parse_resource_role(role_text);
                    const auto resource_id = parse_id(resource_text);
                    if (!role.has_value() || !resource_id.has_value()) {
                        fail("entity resource reference is invalid.");
                        return result;
                    }
                    entity.resource_references.push_back(world::ResourceReference{
                        .role = *role,
                        .resource = world::ResourceId{*resource_id},
                    });
                    continue;
                }

                fail("unknown entity metadata `" + metadata + "`.");
                return result;
            }
            parsed.entities.push_back(std::move(entity));
        } else if (keyword == "relation") {
            std::string subject_text;
            std::string kind_text;
            std::string object_text;
            if (!(values >> subject_text >> kind_text >> object_text)) {
                fail("invalid relation declaration.");
                return result;
            }
            const auto subject = parse_id(subject_text);
            const auto kind = parse_id(kind_text);
            const auto object = parse_id(object_text);
            if (!subject.has_value() || !kind.has_value() || !object.has_value()) {
                fail("relation contains an invalid persistent ID.");
                return result;
            }
            parsed.relations.push_back(world::SnapshotRelation{*subject, *kind, *object});
        } else {
            fail("unknown keyword `" + keyword + "`.");
            return result;
        }

        std::string trailing;
        if (values >> trailing) {
            fail("statement has trailing data.");
            return result;
        }
    }

    if (!header_seen) {
        result.diagnostics = "World source is missing its header.";
        return result;
    }

    if(!world::validate_materials(parsed,&result.diagnostics))return result;
    std::sort(parsed.causal_history.events.begin(),parsed.causal_history.events.end(),[](const auto& a,const auto& b){return a.sequence<b.sequence;});
    if(!world::validate_causal_history(parsed.causal_history,&result.diagnostics))return result;
    if (!world::validate_distance_links(parsed, &result.diagnostics) || !world::validate_dependencies(parsed, &result.diagnostics) || !world::validate_character_attachments(parsed, &result.diagnostics) || !world::validate_presentation(parsed, &result.diagnostics) || !world::validate_vehicles(parsed, &result.diagnostics) || !world::validate_characters(parsed, &result.diagnostics)) return result;
    world::World canonical;
    if (!canonical.restore(parsed)) {
        result.diagnostics = "World source violates World validation rules.";
        return result;
    }
    result.snapshot = canonical.snapshot();
    return result;
}

} // namespace vice::tools::world_cooker
