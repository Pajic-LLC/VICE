#include <vice/tools/world_cooker.hpp>

#include <vice/world/world.hpp>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace vice::tools::world_cooker {
namespace {

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

[[nodiscard]] const char* fidelity_name(const world::RegionFidelity fidelity) noexcept {
    switch (fidelity) {
    case world::RegionFidelity::inactive: return "inactive";
    case world::RegionFidelity::reduced: return "reduced";
    case world::RegionFidelity::full: return "full";
    }
    return "full";
}

[[nodiscard]] std::optional<world::ResourceRole> parse_resource_role(
    const std::string_view text
) noexcept {
    if (text == "render_mesh") return world::ResourceRole::render_mesh;
    return std::nullopt;
}

[[nodiscard]] const char* resource_role_name(const world::ResourceRole role) noexcept {
    switch (role) {
    case world::ResourceRole::render_mesh: return "render_mesh";
    }
    return "render_mesh";
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

        if (keyword == "component") {
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

    world::World canonical;
    if (!canonical.restore(parsed)) {
        result.diagnostics = "World source violates World validation rules.";
        return result;
    }
    result.snapshot = canonical.snapshot();
    return result;
}

std::string serialize_source_manifest(
    const world::WorldSnapshot& snapshot,
    std::string* const diagnostics
) {
    world::World canonical;
    if (!canonical.restore(snapshot)) {
        if (diagnostics != nullptr) *diagnostics = "World snapshot violates World validation rules.";
        return {};
    }

    auto value = canonical.snapshot();
    std::ranges::sort(value.component_types, {}, [](const world::ComponentTypeDesc& type) {
        return type.id.value.to_string();
    });
    std::ranges::sort(value.resources, {}, [](const world::ResourceId& resource) {
        return resource.value.to_string();
    });
    std::ranges::sort(value.regions, {}, [](const world::RegionDesc& region) {
        return region.id.value;
    });
    std::ranges::sort(value.entities, {}, [](const world::SnapshotEntity& entity) {
        return entity.persistent_id.to_string();
    });
    std::ranges::sort(value.relations, {}, [](const world::SnapshotRelation& relation) {
        return std::tuple{
            relation.subject.to_string(),
            relation.kind.to_string(),
            relation.object.to_string(),
        };
    });
    for (auto& entity : value.entities) {
        std::ranges::sort(entity.resource_references, {}, [](const world::ResourceReference& reference) {
            return std::tuple{static_cast<int>(reference.role), reference.resource.value.to_string()};
        });
    }

    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    output << "vice-world-source 1\n\n";

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
        if (entity.authored_transform.has_value()) {
            const auto& transform = *entity.authored_transform;
            output << " transform "
                << transform.rotation.x << ' ' << transform.rotation.y << ' '
                << transform.rotation.z << ' ' << transform.rotation.w << ' '
                << transform.scale.x << ' ' << transform.scale.y << ' ' << transform.scale.z;
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
