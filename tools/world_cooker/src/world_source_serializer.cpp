#include <vice/tools/world_cooker.hpp>
#include "rigid_box_source.hpp"
#include "distance_link_source.hpp"
#include "dependency_source.hpp"
#include "character_source.hpp"
#include "vehicle_source.hpp"
#include "causal_source.hpp"
#include "material_source.hpp"

#include <vice/world/world.hpp>

#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace vice::tools::world_cooker {
namespace {

constexpr double radians_to_degrees = 57.2957795130823208768;

[[nodiscard]] const char* fidelity_name(const world::RegionFidelity fidelity) noexcept {
    switch (fidelity) {
    case world::RegionFidelity::inactive: return "inactive";
    case world::RegionFidelity::reduced: return "reduced";
    case world::RegionFidelity::full: return "full";
    }
    return "full";
}

[[nodiscard]] const char* resource_role_name(const world::ResourceRole role) noexcept {
    switch (role) {
    case world::ResourceRole::render_mesh: return "render_mesh";
    case world::ResourceRole::render_material: return "render_material";
    case world::ResourceRole::reflection_environment: return "reflection_environment";
    }
    return "render_mesh";
}

[[nodiscard]] const char* enabled_name(const bool enabled) noexcept {
    return enabled ? "enabled" : "disabled";
}

} // namespace

std::string serialize_source_manifest(
    const world::WorldSnapshot& snapshot,
    std::string* const diagnostics
) {
    if (!world::validate_distance_links(snapshot, diagnostics) || !world::validate_dependencies(snapshot, diagnostics) || !world::validate_character_attachments(snapshot, diagnostics) || !world::validate_vehicles(snapshot, diagnostics) || !world::validate_characters(snapshot, diagnostics)) return {};
    if(!world::validate_causal_history(snapshot.causal_history,diagnostics))return {};
    world::World canonical;
    if (!canonical.restore(snapshot)) {
        if (diagnostics != nullptr) *diagnostics = "World snapshot violates World validation rules.";
        return {};
    }

    const auto value = canonical.snapshot();
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    output << "vice-world-source 1\n\n";
    write_causal_history(output,value.causal_history);
    for(const auto& m:value.materials) {output<<"material ";write_material(output,m);output<<'\n';}

    for (const auto& type : value.component_types) {
        output << "component " << type.id.value.to_string() << ' ' << type.version << ' '
            << std::quoted(type.name) << '\n';
    }
    for (const auto& resource : value.resources) {
        output << "resource " << resource.value.to_string() << '\n';
    }
    for (const auto& region : value.regions) {
        output << "region " << region.id.value << ' ' << region.simulation_island.value << ' '
            << region.origin.x << ' ' << region.origin.y << ' ' << region.origin.z << ' '
            << region.streaming_cell.x << ' ' << region.streaming_cell.y << ' ' << region.streaming_cell.z << ' '
            << fidelity_name(region.fidelity) << ' ';
        if (region.bounds.has_value()) {
            output << "bounds "
                << region.bounds->min.x << ' ' << region.bounds->min.y << ' ' << region.bounds->min.z << ' '
                << region.bounds->max.x << ' ' << region.bounds->max.y << ' ' << region.bounds->max.z;
        } else {
            output << "none";
        }
        output << '\n';
    }

    for (const auto& entity : value.entities) {
        output << "entity " << entity.persistent_id.to_string() << ' ';
        if (entity.spatial_state.has_value()) {
            output << "spatial "
                << entity.spatial_state->global_position.x << ' '
                << entity.spatial_state->global_position.y << ' '
                << entity.spatial_state->global_position.z << ' '
                << entity.spatial_state->region.value;
        } else {
            output << "none";
        }

        if(!entity.material.is_nil()) output<<" material "<<entity.material.to_string();
        if (entity.authored_transform.has_value()) {
            const auto& transform = *entity.authored_transform;
            output << " transform "
                << transform.rotation.x << ' ' << transform.rotation.y << ' '
                << transform.rotation.z << ' ' << transform.rotation.w << ' '
                << transform.scale.x << ' ' << transform.scale.y << ' ' << transform.scale.z;
        }

        if (entity.camera.has_value()) {
            const auto& camera = *entity.camera;
            output << " camera perspective "
                << static_cast<double>(camera.vertical_fov_radians) * radians_to_degrees << ' '
                << camera.near_plane << ' ' << camera.far_plane << ' '
                << (camera.primary ? "primary" : "secondary");
        }

        if (entity.light.has_value()) {
            const auto& light = *entity.light;
            output << " light "
                << (light.type == world::LightType::directional ? "directional" : "point") << ' '
                << light.color.red << ' ' << light.color.green << ' ' << light.color.blue << ' '
                << light.intensity;
            if (light.type == world::LightType::point) output << ' ' << light.range;
            output << ' ' << enabled_name(light.enabled);
        }

        if (entity.reflection_probe.has_value()) {
            const auto& probe = *entity.reflection_probe;
            output << " reflection_probe "
                << probe.half_extents.x << ' ' << probe.half_extents.y << ' ' << probe.half_extents.z << ' '
                << probe.blend_distance << ' ' << probe.priority << ' ' << enabled_name(probe.enabled);
        }

        if (entity.rigid_box) {
            output << " rigid_box ";
            write_rigid_box(output, *entity.rigid_box);
        }
        if(entity.character_attachment){const auto& a=*entity.character_attachment;output<<" character_attachment "<<a.carrier.to_string()<<' '<<a.offset.x<<' '<<a.offset.y<<' '<<a.offset.z;}
        if (entity.vehicle) { output << " vehicle "; write_vehicle(output,*entity.vehicle); }
        if (entity.character) { output << " character "; write_character(output,*entity.character); }
        if (entity.dependency) {
            output << " dependency "; write_dependency(output, *entity.dependency);
        }
        if (entity.distance_link) {
            output << " distance_link ";
            write_distance_link(output, *entity.distance_link);
        }
        for (const auto& reference : entity.resource_references) {
            output << " resource " << resource_role_name(reference.role) << ' '
                << reference.resource.value.to_string();
        }
        output << '\n';
    }

    for (const auto& relation : value.relations) {
        output << "relation " << relation.subject.to_string() << ' '
            << relation.kind.to_string() << ' ' << relation.object.to_string() << '\n';
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return output.str();
}

} // namespace vice::tools::world_cooker
