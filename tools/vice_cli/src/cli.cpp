#include <vice/dev/cli.hpp>

#include <vice/platform/file.hpp>
#include <vice/project/project.hpp>
#include <vice/project/workspace.hpp>
#include <vice/runtime/dev_session.hpp>
#include <vice/tools/project_workflow.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot.hpp>
#include <vice/world/world.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace vice::dev {
namespace {

void append_json_string(std::string& output, const std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20U) {
                output += "\\u00";
                output.push_back(hex[(character >> 4U) & 0x0fU]);
                output.push_back(hex[character & 0x0fU]);
            } else {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output.push_back('"');
}

void append_double(std::string& output, const double value) {
    std::array<char, 64> buffer{};
    const auto converted = std::to_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        value,
        std::chars_format::general,
        std::numeric_limits<double>::max_digits10
    );
    if (converted.ec != std::errc{}) {
        output += "null";
        return;
    }
    output.append(buffer.data(), converted.ptr);
}

[[nodiscard]] std::string normalized_path(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        return path.lexically_normal().generic_string();
    }
    return absolute.lexically_normal().generic_string();
}

[[nodiscard]] std::optional<std::string> read_text(
    const std::filesystem::path& path,
    std::string& diagnostics
) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        diagnostics = "Could not open `" + path.generic_string() + "`.";
        return std::nullopt;
    }
    std::string contents{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
    if (!input.eof() && input.fail()) {
        diagnostics = "Could not read `" + path.generic_string() + "`.";
        return std::nullopt;
    }
    return contents;
}

[[nodiscard]] std::string response_json(
    const std::string_view command,
    const bool ok,
    const std::vector<std::string>& diagnostics,
    const std::string_view data_json
) {
    std::string output;
    output.reserve(256U + data_json.size());
    output += "{\"schema\":";
    append_json_string(output, structured_schema);
    output += ",\"command\":";
    append_json_string(output, command);
    output += ",\"ok\":";
    output += ok ? "true" : "false";
    output += ",\"diagnostics\":[";
    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        if (index != 0U) output.push_back(',');
        append_json_string(output, diagnostics[index]);
    }
    output += "],\"data\":";
    output += data_json;
    output += "}\n";
    return output;
}

[[nodiscard]] CommandResult failure(
    const int exit_code,
    const std::string_view command,
    std::string diagnostics
) {
    std::vector<std::string> messages;
    messages.push_back(std::move(diagnostics));
    return CommandResult{
        .exit_code = exit_code,
        .json = response_json(command, false, messages, "{}"),
    };
}

[[nodiscard]] std::string_view fidelity_name(const world::RegionFidelity fidelity) noexcept {
    switch (fidelity) {
    case world::RegionFidelity::inactive: return "inactive";
    case world::RegionFidelity::reduced: return "reduced";
    case world::RegionFidelity::full: return "full";
    }
    return "unknown";
}

[[nodiscard]] std::string_view resource_role_name(const world::ResourceRole role) noexcept {
    switch (role) {
    case world::ResourceRole::render_mesh: return "render_mesh";
    case world::ResourceRole::render_material: return "render_material";
    case world::ResourceRole::reflection_environment: return "reflection_environment";
    }
    return "unknown";
}

[[nodiscard]] CommandResult inspect_project(
    const std::string_view command,
    const std::filesystem::path& manifest_path,
    const bool validate_startup_world
) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(manifest_path, error)) {
        return failure(
            exit_unavailable,
            command,
            "Project manifest is not available at `" + manifest_path.generic_string() + "`."
        );
    }

    const auto loaded = project::load_manifest(manifest_path);
    if (!loaded.manifest.has_value()) {
        return failure(exit_validation, command, loaded.diagnostics);
    }

    const auto& manifest = *loaded.manifest;
    const auto asset_root = project::resolve_asset_root(manifest_path, manifest);
    const auto startup_world = project::resolve_startup_world(manifest_path, manifest);
    const auto game_output_root = project::resolve_game_output_root(manifest_path, manifest);

    error.clear();
    const bool startup_exists = std::filesystem::is_regular_file(startup_world, error);
    if (validate_startup_world && !startup_exists) {
        return failure(
            exit_validation,
            command,
            "Configured startup World is not available at `" + startup_world.generic_string() + "`."
        );
    }
    if (validate_startup_world) {
        std::string read_diagnostics;
        const auto source = read_text(startup_world, read_diagnostics);
        if (!source.has_value()) {
            return failure(
                exit_unavailable,
                command,
                "Could not validate configured startup World: " + read_diagnostics
            );
        }
        const auto parsed_world = tools::world_cooker::parse_source_manifest(*source);
        if (!parsed_world.snapshot.has_value()) {
            return failure(
                exit_validation,
                command,
                "Configured startup World is invalid: " + parsed_world.diagnostics
            );
        }
    }

    std::string data;
    data.reserve(768U);
    data += "{\"manifest_version\":" + std::to_string(manifest.version);
    data += ",\"project_id\":";
    append_json_string(data, manifest.id.to_string());
    data += ",\"name\":";
    append_json_string(data, manifest.name);
    data += ",\"game_name\":";
    append_json_string(data, manifest.game_name);
    data += ",\"game_target\":";
    append_json_string(data, manifest.game_target);
    data += ",\"manifest_path\":";
    append_json_string(data, normalized_path(manifest_path));
    data += ",\"asset_root\":";
    append_json_string(data, normalized_path(asset_root));
    data += ",\"startup_world_relative\":";
    append_json_string(data, manifest.startup_world.generic_string());
    data += ",\"startup_world\":";
    append_json_string(data, normalized_path(startup_world));
    data += ",\"startup_world_exists\":";
    data += startup_exists ? "true" : "false";
    data += ",\"game_output_root\":";
    append_json_string(data, normalized_path(game_output_root));
    data.push_back('}');

    return CommandResult{
        .exit_code = exit_success,
        .json = response_json(command, true, {}, data),
    };
}

[[nodiscard]] std::filesystem::path project_cache_base(
    const std::filesystem::path& manifest_path
) {
    auto parent = manifest_path.parent_path();
    if (parent.empty()) parent = ".";
    return (parent / ".vice" / "cache").lexically_normal();
}

[[nodiscard]] tools::ProjectRuntimeSession project_runtime_session(
    const std::filesystem::path& manifest_path,
    const CommandEnvironment& environment
) {
    auto root = environment.runtime_session_root;
    if (root.empty()) {
        root = (project_cache_base(manifest_path).parent_path() / "runtime-sessions").lexically_normal();
    }
    auto id = environment.runtime_session_id;
    if (id.empty()) id = world::PersistentId::random().to_string();
    return tools::ProjectRuntimeSession{
        .id = std::move(id),
        .root = std::move(root),
    };
}

void append_optional_path(
    std::string& data,
    const std::filesystem::path& path
) {
    if (path.empty()) {
        data += "null";
        return;
    }
    append_json_string(data, normalized_path(path));
}

[[nodiscard]] std::string project_build_data(
    const std::string_view state,
    const project::Workspace& workspace,
    const tools::ProjectBuildResult& build,
    const std::optional<tools::ProjectRuntimeSession>& runtime_session = std::nullopt
) {
    std::error_code error;
    const auto game_source = (
        workspace.manifest_path.parent_path() / "game" / "game.cpp"
    ).lexically_normal();
    const bool source_present = std::filesystem::is_regular_file(game_source, error) && !error;

    std::string data;
    data.reserve(1792U);
    data += "{\"state\":";
    append_json_string(data, state);
    data += ",\"project_id\":";
    append_json_string(data, workspace.manifest.id.to_string());
    data += ",\"game_target\":";
    append_json_string(data, workspace.manifest.game_target);
    data += ",\"manifest_path\":";
    append_json_string(data, normalized_path(workspace.manifest_path));
    data += ",\"startup_world\":";
    append_json_string(data, normalized_path(workspace.startup_world_source));
    data += ",\"cooked_startup_world\":";
    append_json_string(data, normalized_path(workspace.cooked_startup_world));
    data += ",\"counts\":{\"entities\":" + std::to_string(build.assets.entity_count);
    data += ",\"regions\":" + std::to_string(build.assets.region_count);
    data += ",\"meshes\":" + std::to_string(build.assets.mesh_count);
    data += ",\"textures\":" + std::to_string(build.assets.texture_count);
    data += ",\"environments\":" + std::to_string(build.assets.environment_count);
    data += ",\"materials\":" + std::to_string(build.assets.material_count) + "}";
    data += ",\"game_module\":{\"source_present\":";
    data += source_present ? "true" : "false";
    data += ",\"source\":";
    if (source_present) append_json_string(data, normalized_path(game_source)); else data += "null";
    data += ",\"output\":";
    append_optional_path(data, build.game_module);
    data += "}";
    data += ",\"game_output_root\":";
    append_json_string(data, normalized_path(workspace.game_output_root));
    data += ",\"game_executable\":";
    append_optional_path(data, build.game_executable);
    data += ",\"runtime_manifest\":";
    append_optional_path(data, build.runtime_manifest);
    data += ",\"runtime_content_root\":";
    append_optional_path(data, build.runtime_content_root);
    data += ",\"runtime_session\":";
    if (!runtime_session.has_value()) {
        data += "null";
    } else {
        data += "{\"id\":";
        append_json_string(data, runtime_session->id);
        data += ",\"root\":";
        append_json_string(data, normalized_path(runtime_session->root));
        data += ",\"path\":";
        append_json_string(data, normalized_path(runtime_session->root / runtime_session->id));
        data.push_back('}');
    }
    data.push_back('}');
    return data;
}

[[nodiscard]] CommandResult project_workflow_command(
    const std::string_view command,
    const std::filesystem::path& manifest_path,
    const bool launch,
    const CommandEnvironment& environment
) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(manifest_path, error)) {
        return failure(
            exit_unavailable,
            command,
            "Project manifest is not available at `" + manifest_path.generic_string() + "`."
        );
    }

    const auto opened = project::open_workspace(manifest_path, project_cache_base(manifest_path));
    if (!opened) {
        return failure(exit_validation, command, opened.diagnostics);
    }
    const auto& workspace = *opened.workspace;

    if (environment.runtime_host_executable.empty()) {
        return failure(
            exit_unavailable,
            command,
            "VICE runtime host is not available in this developer build."
        );
    }

    if (!launch) {
        const auto built = tools::build_project(workspace, environment.runtime_host_executable);
        const auto state = built ? "built" : "build_failed";
        std::vector<std::string> diagnostics;
        if (!built.diagnostics.empty()) diagnostics.push_back(built.diagnostics);
        return CommandResult{
            .exit_code = built ? exit_success : exit_build,
            .json = response_json(command, static_cast<bool>(built), diagnostics,
                project_build_data(state, workspace, built)),
        };
    }

    const auto requested_session = project_runtime_session(manifest_path, environment);
    // Reject malformed launch identity before a build can replace a staged
    // executable that a preceding session may still be using on Windows.
    if (!runtime::valid_runtime_dev_session_id(requested_session.id)) {
        return CommandResult{
            .exit_code = exit_launch,
            .json = response_json(command, false,
                {"Runtime development session ID is invalid."},
                project_build_data("launch_failed", workspace, tools::ProjectBuildResult{})),
        };
    }
    const auto run = tools::run_project_with_session(
        workspace,
        environment.runtime_host_executable,
        requested_session
    );
    if (!run.build) {
        std::vector<std::string> diagnostics;
        if (!run.diagnostics.empty()) diagnostics.push_back(run.diagnostics);
        return CommandResult{
            .exit_code = exit_build,
            .json = response_json(
                command,
                false,
                diagnostics,
                project_build_data("build_failed", workspace, run.build)
            ),
        };
    }
    if (!run.launched) {
        std::vector<std::string> diagnostics;
        if (!run.diagnostics.empty()) diagnostics.push_back(run.diagnostics);
        return CommandResult{
            .exit_code = exit_launch,
            .json = response_json(
                command,
                false,
                diagnostics,
                project_build_data("launch_failed", workspace, run.build)
            ),
        };
    }

    return CommandResult{
        .exit_code = exit_success,
        .json = response_json(
            command,
            true,
            {},
            project_build_data("launched", workspace, run.build, run.runtime_session)
        ),
    };
}

void append_component_types(std::string& data, const world::WorldSnapshot& snapshot) {
    std::vector<const world::ComponentTypeDesc*> values;
    values.reserve(snapshot.component_types.size());
    for (const auto& value : snapshot.component_types) values.push_back(&value);
    std::sort(values.begin(), values.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->id.value.bytes() < rhs->id.value.bytes();
    });

    data += "\"component_types\":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) data.push_back(',');
        const auto& value = *values[index];
        data += "{\"id\":";
        append_json_string(data, value.id.value.to_string());
        data += ",\"name\":";
        append_json_string(data, value.name);
        data += ",\"version\":" + std::to_string(value.version) + "}";
    }
    data.push_back(']');
}

void append_resources(std::string& data, const world::WorldSnapshot& snapshot) {
    auto values = snapshot.resources;
    std::sort(values.begin(), values.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.value.bytes() < rhs.value.bytes();
    });

    data += ",\"resources\":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) data.push_back(',');
        append_json_string(data, values[index].value.to_string());
    }
    data.push_back(']');
}

void append_regions(std::string& data, const world::WorldSnapshot& snapshot) {
    std::vector<const world::RegionDesc*> values;
    values.reserve(snapshot.regions.size());
    for (const auto& value : snapshot.regions) values.push_back(&value);
    std::sort(values.begin(), values.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->id.value < rhs->id.value;
    });

    data += ",\"regions\":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) data.push_back(',');
        const auto& value = *values[index];
        data += "{\"id\":" + std::to_string(value.id.value);
        data += ",\"simulation_island\":" + std::to_string(value.simulation_island.value);
        data += ",\"origin\":[";
        append_double(data, value.origin.x);
        data.push_back(',');
        append_double(data, value.origin.y);
        data.push_back(',');
        append_double(data, value.origin.z);
        data += "],\"streaming_cell\":[" + std::to_string(value.streaming_cell.x) + "," +
            std::to_string(value.streaming_cell.y) + "," + std::to_string(value.streaming_cell.z) + "]";
        data += ",\"fidelity\":";
        append_json_string(data, fidelity_name(value.fidelity));
        data += ",\"bounds\":";
        if (!value.bounds.has_value()) {
            data += "null";
        } else {
            data += "{\"min\":[";
            append_double(data, value.bounds->min.x);
            data.push_back(',');
            append_double(data, value.bounds->min.y);
            data.push_back(',');
            append_double(data, value.bounds->min.z);
            data += "],\"max\":[";
            append_double(data, value.bounds->max.x);
            data.push_back(',');
            append_double(data, value.bounds->max.y);
            data.push_back(',');
            append_double(data, value.bounds->max.z);
            data += "]}";
        }
        data.push_back('}');
    }
    data.push_back(']');
}

void append_entities(std::string& data, const world::WorldSnapshot& snapshot) {
    world::World observed; (void)observed.restore(snapshot);
    std::vector<const world::SnapshotEntity*> values;
    values.reserve(snapshot.entities.size());
    for (const auto& value : snapshot.entities) values.push_back(&value);
    std::sort(values.begin(), values.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->persistent_id.bytes() < rhs->persistent_id.bytes();
    });

    data += ",\"entities\":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) data.push_back(',');
        const auto& value = *values[index];
        data += "{\"id\":";
        append_json_string(data, value.persistent_id.to_string());
        data += ",\"spatial\":";
        if (!value.spatial_state.has_value()) {
            data += "null";
        } else {
            data += "{\"global_position\":[";
            append_double(data, value.spatial_state->global_position.x);
            data.push_back(',');
            append_double(data, value.spatial_state->global_position.y);
            data.push_back(',');
            append_double(data, value.spatial_state->global_position.z);
            data += "],\"region\":" + std::to_string(value.spatial_state->region.value) + "}";
        }
        data += ",\"authored_transform\":";
        data += value.authored_transform.has_value() ? "true" : "false";
        data += ",\"camera\":";
        data += value.camera.has_value() ? "true" : "false";
        data += ",\"light\":";
        data += value.light.has_value() ? "true" : "false";
        data += ",\"reflection_probe\":";
        data += value.reflection_probe.has_value() ? "true" : "false";
        data += ",\"rigid_box\":";
        if (!value.rigid_box) data += "null";
        else {
            const auto& box = *value.rigid_box;
            data += "{\"motion\":";
            append_json_string(data, box.motion == world::RigidMotion::static_body ? "static" :
                (box.motion == world::RigidMotion::kinematic_body ? "kinematic" : "dynamic"));
            auto vector = [&](std::string_view name, math::Vec3 v) {
                data += ",\"" + std::string{name} + "\":[";
                append_double(data, v.x); data += ','; append_double(data, v.y); data += ','; append_double(data, v.z);
                data += ']';
            };
            vector("half_extents", box.half_extents);
            vector("linear_velocity", box.linear_velocity);
            vector("angular_velocity", box.angular_velocity);
            data += ",\"friction\":"; append_double(data, box.friction);
            data += ",\"restitution\":"; append_double(data, box.restitution);
            data += ",\"continuous\":"; data += box.continuous ? "true" : "false";
            data += ",\"trigger\":"; data += box.trigger ? "true" : "false";
            data += '}';
        }

    data += ",\"distance_link\":";
    if (!value.distance_link) data += "null";
    else {
        const auto& link = *value.distance_link;
        data += "{\"first\":"; append_json_string(data, link.first.to_string());
        data += ",\"second\":"; append_json_string(data, link.second.to_string());
        data += ",\"min_distance\":"; append_double(data, link.min_distance);
        data += ",\"max_distance\":"; append_double(data, link.max_distance);
        data += ",\"frequency_hz\":"; append_double(data, link.frequency_hz);
        data += ",\"damping_ratio\":"; append_double(data, link.damping_ratio);
        data += ",\"break_threshold\":"; append_double(data, link.break_threshold);
        data += ",\"broken\":"; data += link.broken ? "true" : "false";
        data += ",\"last_break_impulse\":"; append_double(data, link.last_break_impulse);
        data += ",\"break_step\":" + std::to_string(link.break_step) + "}";
    }

        const auto owner = observed.find(value.persistent_id);
        data += ",\"dependency\":" + world::serialize_dependency_state(owner ? observed.dependency_state(*owner) : std::nullopt);
        auto references = value.resource_references;
        std::sort(references.begin(), references.end(), [](const auto& lhs, const auto& rhs) {
            const auto lhs_role = static_cast<std::uint8_t>(lhs.role);
            const auto rhs_role = static_cast<std::uint8_t>(rhs.role);
            if (lhs_role != rhs_role) return lhs_role < rhs_role;
            return lhs.resource.value.bytes() < rhs.resource.value.bytes();
        });
        data += ",\"resource_references\":[";
        for (std::size_t reference_index = 0; reference_index < references.size(); ++reference_index) {
            if (reference_index != 0U) data.push_back(',');
            data += "{\"role\":";
            append_json_string(data, resource_role_name(references[reference_index].role));
            data += ",\"resource\":";
            append_json_string(data, references[reference_index].resource.value.to_string());
            data.push_back('}');
        }
        data += "]}";
    }
    data.push_back(']');
}

void append_relations(std::string& data, const world::WorldSnapshot& snapshot) {
    std::vector<const world::SnapshotRelation*> values;
    values.reserve(snapshot.relations.size());
    for (const auto& value : snapshot.relations) values.push_back(&value);
    std::sort(values.begin(), values.end(), [](const auto* lhs, const auto* rhs) {
        return std::tie(lhs->subject.bytes(), lhs->kind.bytes(), lhs->object.bytes()) <
            std::tie(rhs->subject.bytes(), rhs->kind.bytes(), rhs->object.bytes());
    });

    data += ",\"relations\":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) data.push_back(',');
        const auto& value = *values[index];
        data += "{\"subject\":";
        append_json_string(data, value.subject.to_string());
        data += ",\"kind\":";
        append_json_string(data, value.kind.to_string());
        data += ",\"object\":";
        append_json_string(data, value.object.to_string());
        data.push_back('}');
    }
    data.push_back(']');
}

[[nodiscard]] CommandResult inspect_world(
    const std::string_view command,
    const std::filesystem::path& world_path
) {
    if (world_path.extension() != ".vworld") {
        return failure(exit_validation, command, "Authored World path must use the `.vworld` extension.");
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(world_path, error)) {
        return failure(
            exit_unavailable,
            command,
            "Authored World is not available at `" + world_path.generic_string() + "`."
        );
    }

    const auto source = platform::read_text_file(world_path);
    if (!source) {
        return failure(exit_unavailable, command, source.diagnostics);
    }

    const auto revision = platform::text_revision(source.contents);
    if (revision.empty()) return failure(exit_unavailable, command, "Could not compute source revision.");
    const auto parsed = tools::world_cooker::parse_source_manifest(source.contents);
    if (!parsed.snapshot.has_value()) {
        return failure(exit_validation, command, parsed.diagnostics);
    }

    const auto& snapshot = *parsed.snapshot;
    std::size_t inactive_regions = 0U;
    std::size_t reduced_regions = 0U;
    std::size_t full_regions = 0U;
    for (const auto& region : snapshot.regions) {
        switch (region.fidelity) {
        case world::RegionFidelity::inactive: ++inactive_regions; break;
        case world::RegionFidelity::reduced: ++reduced_regions; break;
        case world::RegionFidelity::full: ++full_regions; break;
        }
    }

    std::string data;
    data.reserve(2048U + snapshot.entities.size() * 256U);
    data += "{\"path\":";
    append_json_string(data, normalized_path(world_path));
    data += ",\"source_revision\":";
    append_json_string(data, revision);
    data += ",\"summary\":{\"component_types\":" + std::to_string(snapshot.component_types.size());
    data += ",\"resources\":" + std::to_string(snapshot.resources.size());
    data += ",\"regions\":" + std::to_string(snapshot.regions.size());
    data += ",\"entities\":" + std::to_string(snapshot.entities.size());
    data += ",\"relations\":" + std::to_string(snapshot.relations.size());
    data += ",\"region_fidelity\":{\"inactive\":" + std::to_string(inactive_regions);
    data += ",\"reduced\":" + std::to_string(reduced_regions);
    data += ",\"full\":" + std::to_string(full_regions) + "}},";
    append_component_types(data, snapshot);
    append_resources(data, snapshot);
    append_regions(data, snapshot);
    append_entities(data, snapshot);
    append_relations(data, snapshot);
    data.push_back('}');

    return CommandResult{
        .exit_code = exit_success,
        .json = response_json(command, true, {}, data),
    };
}

[[nodiscard]] std::string_view runtime_snapshot_state_name(
    const runtime::RuntimeDevSnapshotState state
) noexcept {
    switch (state) {
    case runtime::RuntimeDevSnapshotState::available: return "available";
    case runtime::RuntimeDevSnapshotState::disconnected: return "disconnected";
    case runtime::RuntimeDevSnapshotState::invalid: return "invalid";
    case runtime::RuntimeDevSnapshotState::timed_out: return "timed_out";
    }
    return "invalid";
}

[[nodiscard]] CommandResult inspect_runtime_session(
    const std::string_view command,
    const std::filesystem::path& session_path
) {
    const auto normalized = session_path.lexically_normal();
    const auto id = normalized.filename().string();
    const auto root = normalized.parent_path();
    const runtime::RuntimeDevSession session{
        .id = id,
        .root = root,
    };
    if (root.empty() || !runtime::valid_runtime_dev_session_id(id)) {
        return failure(exit_validation, command, "Runtime session path is invalid.");
    }

    const auto snapshot = runtime::read_latest_runtime_dev_snapshot(session);
    std::string data;
    data.reserve(256U + snapshot.payload.size());
    data += "{\"state\":";
    append_json_string(data, runtime_snapshot_state_name(snapshot.state));
    data += ",\"session_id\":";
    append_json_string(data, session.id);
    data += ",\"session_path\":";
    append_json_string(data, normalized_path(runtime::runtime_dev_session_path(session)));
    data += ",\"generation\":" + std::to_string(snapshot.generation);
    data += ",\"payload\":";
    if (snapshot.payload.empty()) data += "null"; else append_json_string(data, snapshot.payload);
    data.push_back('}');

    if (snapshot.state == runtime::RuntimeDevSnapshotState::available) {
        return CommandResult{
            .exit_code = exit_success,
            .json = response_json(command, true, {}, data),
        };
    }

    std::vector<std::string> diagnostics;
    if (!snapshot.diagnostics.empty()) diagnostics.push_back(snapshot.diagnostics);
    return CommandResult{
        .exit_code = snapshot.state == runtime::RuntimeDevSnapshotState::invalid
            ? exit_validation
            : exit_unavailable,
        .json = response_json(command, false, diagnostics, data),
    };
}

} // namespace

CommandEnvironment configured_command_environment() {
    CommandEnvironment environment;
#if defined(VICE_DEV_RUNTIME_HOST_EXECUTABLE)
    environment.runtime_host_executable = std::filesystem::path{VICE_DEV_RUNTIME_HOST_EXECUTABLE};
#endif
    return environment;
}

CommandResult execute(
    const std::span<const std::string_view> arguments,
    const CommandEnvironment& environment
) {
    if (arguments.size() >= 2U && arguments[0] == "world" && arguments[1] == "edit") {
        if (arguments.size() != 5U || (arguments[4] != "--dry-run" && arguments[4] != "--apply")) {
            return failure(exit_usage, "world.edit",
                "Usage: vice world edit <world.vworld> <request.viceedit> <--dry-run|--apply>");
        }
        return execute_world_edit(std::filesystem::path{std::string{arguments[2]}},
            std::filesystem::path{std::string{arguments[3]}}, arguments[4] == "--dry-run");
    }
    if (arguments.size() == 3U && arguments[0] == "runtime" && arguments[1] == "profile") {
        return execute_runtime_profile(std::filesystem::path{std::string{arguments[2]}});
    }
    if (arguments.size() == 4U && arguments[0] == "runtime" && arguments[1] == "debug") {
        return execute_runtime_debug(std::filesystem::path{std::string{arguments[2]}}, arguments[3]);
    }
    if (arguments.size() != 3U) {
        return failure(
            exit_usage,
            "usage",
            "Usage: vice <project|world|runtime> <inspect|validate|build|run> <path>"
        );
    }

    const auto domain = arguments[0];
    const auto operation = arguments[1];
    const std::string command = std::string{domain} + "." + std::string{operation};
    const std::filesystem::path path{std::string{arguments[2]}};

    if (domain == "project") {
        if (operation == "inspect" || operation == "validate") {
            return inspect_project(command, path, operation == "validate");
        }
        if (operation == "build") {
            return project_workflow_command(command, path, false, environment);
        }
        if (operation == "run") {
            return project_workflow_command(command, path, true, environment);
        }
        return failure(
            exit_usage,
            command,
            "Project operation must be `inspect`, `validate`, `build`, or `run`."
        );
    }

    if (domain == "world") {
        if (operation != "inspect" && operation != "validate") {
            return failure(
                exit_usage,
                command,
                "World operation must be `inspect` or `validate`."
            );
        }
        return inspect_world(command, path);
    }

    if (domain == "runtime") {
        if (operation != "inspect") {
            return failure(exit_usage, command, "Runtime operation must be `inspect`.");
        }
        return inspect_runtime_session(command, path);
    }

    return failure(
        exit_usage,
        command,
        "Domain must be `project`, `world`, or `runtime`."
    );
}

CommandResult execute(const std::span<const std::string_view> arguments) {
    return execute(arguments, configured_command_environment());
}

} // namespace vice::dev
