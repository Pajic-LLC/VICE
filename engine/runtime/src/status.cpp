#include <vice/runtime/status.hpp>

#include <vice/runtime/scene.hpp>
#include <vice/world/snapshot.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace vice::runtime {
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
    if (!std::isfinite(value)) {
        output += "null";
        return;
    }
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

void append_profile_window(std::string& output, const RuntimeProfileWindow& profile) {
    output += "{\"units\":\"seconds\",\"capacity\":" +
        std::to_string(runtime_profile_frame_capacity);
    output += ",\"count\":" + std::to_string(profile.count);
    output += ",\"stages\":[\"frame\",\"update\",\"render\"],\"frames\":[";
    for (std::size_t index = 0U; index < profile.count; ++index) {
        if (index != 0U) output.push_back(',');
        const auto& frame = profile.frames[index];
        output += "{\"frame_index\":" + std::to_string(frame.frame_index);
        output += ",\"frame\":";
        append_double(output, frame.frame_seconds);
        output += ",\"update\":";
        append_double(output, frame.update_seconds);
        output += ",\"render\":";
        append_double(output, frame.render_seconds);
        output.push_back('}');
    }
    output += "]}";
}

[[nodiscard]] constexpr std::string_view execution_state_name(
    const RuntimeExecutionState state
) noexcept {
    switch (state) {
    case RuntimeExecutionState::starting: return "starting";
    case RuntimeExecutionState::running: return "running";
    case RuntimeExecutionState::stopping: return "stopping";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view runtime_backend_name(const render::Backend backend) noexcept {
    switch (backend) {
    case render::Backend::metal: return "metal";
    case render::Backend::direct3d12: return "direct3d12";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view shadow_mode_name(
    const RuntimeDirectionalShadowMode mode
) noexcept {
    switch (mode) {
    case RuntimeDirectionalShadowMode::automatic: return "automatic";
    case RuntimeDirectionalShadowMode::cascaded: return "cascaded";
    case RuntimeDirectionalShadowMode::ray_traced: return "ray_traced";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view shadow_path_name(
    const RuntimeDirectionalShadowPath path
) noexcept {
    switch (path) {
    case RuntimeDirectionalShadowPath::cascaded: return "cascaded";
    case RuntimeDirectionalShadowPath::ray_traced: return "ray_traced";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view shadow_fallback_reason(
    const RuntimeDirectionalShadowTelemetry& telemetry
) noexcept {
    if (telemetry.active_path == RuntimeDirectionalShadowPath::ray_traced ||
        telemetry.requested_mode == RuntimeDirectionalShadowMode::cascaded) {
        return {};
    }
    if (!telemetry.ray_tracing_supported) return "ray_tracing_unsupported";
    if (!telemetry.ray_query_visibility_available) return "ray_query_visibility_unavailable";
    return {};
}

void append_shadow_telemetry(
    std::string& output,
    const RuntimeDirectionalShadowTelemetry& telemetry
) {
    output += "{\"requested_mode\":";
    append_json_string(output, shadow_mode_name(telemetry.requested_mode));
    output += ",\"active_path\":";
    append_json_string(output, shadow_path_name(telemetry.active_path));
    output += ",\"ray_tracing_supported\":";
    output += telemetry.ray_tracing_supported ? "true" : "false";
    output += ",\"ray_query_visibility_available\":";
    output += telemetry.ray_query_visibility_available ? "true" : "false";
    output += ",\"fallback_reason\":";
    const auto fallback = shadow_fallback_reason(telemetry);
    if (fallback.empty()) output += "null"; else append_json_string(output, fallback);
    output.push_back('}');
}

void append_execution_counters(
    std::string& output,
    const RuntimeRayTracingExecutionCounters& counters
) {
    output += "{\"blas_creates\":" + std::to_string(counters.blas_creates);
    output += ",\"blas_reuses\":" + std::to_string(counters.blas_reuses);
    output += ",\"blas_rebuilds\":" + std::to_string(counters.blas_rebuilds);
    output += ",\"blas_evictions\":" + std::to_string(counters.blas_evictions);
    output += ",\"tlas_builds\":" + std::to_string(counters.tlas_builds);
    output += ",\"tlas_refits\":" + std::to_string(counters.tlas_refits);
    output += ",\"tlas_reuses\":" + std::to_string(counters.tlas_reuses) + "}";
}

void append_ray_tracing_telemetry(
    std::string& output,
    const RuntimeRayTracingTelemetry& telemetry
) {
    output += "{\"supported\":";
    output += telemetry.supported ? "true" : "false";
    output += ",\"scene_active\":";
    output += telemetry.scene_active ? "true" : "false";
    output += ",\"blas_count\":" + std::to_string(telemetry.blas_count);
    output += ",\"tlas_instance_count\":" + std::to_string(telemetry.tlas_instance_count);
    output += ",\"blas_bytes\":" + std::to_string(telemetry.blas_bytes);
    output += ",\"tlas_bytes\":" + std::to_string(telemetry.tlas_bytes);
    output += ",\"total_bytes\":" + std::to_string(telemetry.total_bytes);
    output += ",\"last_successful_execution\":";
    append_execution_counters(output, telemetry.last_successful_execution);
    output += ",\"cumulative\":";
    append_execution_counters(output, telemetry.cumulative);
    output.push_back('}');
}

void append_device_capabilities(
    std::string& output,
    const render::DeviceCapabilities& capabilities
) {
    output += "{\"unified_memory\":";
    output += capabilities.unified_memory ? "true" : "false";
    output += ",\"ray_tracing\":";
    output += capabilities.ray_tracing ? "true" : "false";
    output += ",\"mesh_shaders\":";
    output += capabilities.mesh_shaders ? "true" : "false";
    output += ",\"max_sampler_anisotropy\":" + std::to_string(capabilities.max_sampler_anisotropy);
    output += ",\"max_texture_dimension_1d\":" + std::to_string(capabilities.max_texture_dimension_1d);
    output += ",\"max_texture_dimension_2d\":" + std::to_string(capabilities.max_texture_dimension_2d);
    output += ",\"max_texture_dimension_3d\":" + std::to_string(capabilities.max_texture_dimension_3d);
    output += ",\"max_texture_array_layers\":" + std::to_string(capabilities.max_texture_array_layers);
    output += ",\"recommended_frames_in_flight\":" +
        std::to_string(capabilities.recommended_frames_in_flight) + "}";
}

void append_device_counters(
    std::string& output,
    const render::DeviceStats& counters
) {
    output += "{\"submitted_command_lists\":" + std::to_string(counters.submitted_command_lists);
    output += ",\"completed_submission\":" + std::to_string(counters.completed_submission);
    output += ",\"created_buffers\":" + std::to_string(counters.created_buffers);
    output += ",\"created_textures\":" + std::to_string(counters.created_textures);
    output += ",\"created_samplers\":" + std::to_string(counters.created_samplers);
    output += ",\"created_shader_modules\":" + std::to_string(counters.created_shader_modules);
    output += ",\"created_graphics_pipelines\":" +
        std::to_string(counters.created_graphics_pipelines) + "}";
}

} // namespace

RuntimeCameraDiagnostics runtime_camera_diagnostics(
    const world::World& world,
    const float aspect_ratio
) noexcept {
    RuntimeCameraDiagnostics result;
    const auto camera = primary_camera_view(world, aspect_ratio);
    if (!camera.has_value() || camera->entity.is_nil()) return result;

    result.entity = camera->entity;
    const auto snapshot = world.snapshot();
    const auto entity = std::find_if(
        snapshot.entities.begin(),
        snapshot.entities.end(),
        [&](const world::SnapshotEntity& candidate) {
            return candidate.persistent_id == camera->entity;
        }
    );
    if (entity == snapshot.entities.end() || !entity->spatial_state.has_value()) return result;

    result.global_position = entity->spatial_state->global_position;
    if (entity->spatial_state->region.valid()) result.region = entity->spatial_state->region;
    return result;
}

RuntimeStatusSnapshot make_runtime_status_snapshot(
    std::string project_id,
    std::string game_target,
    std::string startup_world,
    const RuntimeExecutionState execution_state,
    const std::uint64_t frame_index,
    const double frame_seconds,
    const render::Backend backend,
    const render::DeviceCapabilities device_capabilities,
    const render::DeviceStats device_stats,
    const platform::WindowMetrics metrics,
    const world::World& world,
    const RuntimeSceneRenderer* const scene_renderer
) {
    RuntimeStatusSnapshot result;
    result.project_id = std::move(project_id);
    result.game_target = std::move(game_target);
    result.startup_world = std::move(startup_world);
    result.execution_state = execution_state;
    result.frame_index = frame_index;
    if (std::isfinite(frame_seconds) && frame_seconds > 0.0) {
        result.frame_seconds = frame_seconds;
        result.frames_per_second = 1.0 / frame_seconds;
    }
    result.world = summarize_runtime_world(world);
    const float aspect_ratio = metrics.drawable_height == 0U
        ? 1.0F
        : static_cast<float>(metrics.drawable_width) /
            static_cast<float>(metrics.drawable_height);
    result.camera = runtime_camera_diagnostics(world, aspect_ratio);
    result.presentation = RuntimePresentationDiagnostics{
        .backend = backend,
        .width = metrics.drawable_width,
        .height = metrics.drawable_height,
        .drawable = metrics.drawable_width > 0U && metrics.drawable_height > 0U,
    };
    result.render_device = RuntimeRenderDeviceDiagnostics{
        .capabilities = device_capabilities,
        .counters = device_stats,
    };
    result.renderer = runtime_renderer_diagnostics(scene_renderer);
    return result;
}

std::string serialize_runtime_status_snapshot(
    const RuntimeStatusSnapshot& snapshot,
    const std::string_view session_id
) {
    std::string output;
    output.reserve(18432U);
    output += "{\"schema\":\"vice.dev.runtime.v1\",\"session_id\":";
    append_json_string(output, session_id);
    output += ",\"project_id\":";
    append_json_string(output, snapshot.project_id);
    output += ",\"game_target\":";
    append_json_string(output, snapshot.game_target);
    output += ",\"startup_world\":";
    append_json_string(output, snapshot.startup_world);
    output += ",\"execution_state\":";
    append_json_string(output, execution_state_name(snapshot.execution_state));
    output += ",\"frame\":{\"index\":" + std::to_string(snapshot.frame_index);
    output += ",\"seconds\":";
    append_double(output, snapshot.frame_seconds);
    output += ",\"frames_per_second\":";
    append_double(output, snapshot.frames_per_second);
    output += "},\"profile\":";
    append_profile_window(output, snapshot.profile);
    output += ",\"world\":{\"entities\":" + std::to_string(snapshot.world.entities);
    output += ",\"resources\":" + std::to_string(snapshot.world.resources);
    output += ",\"relations\":" + std::to_string(snapshot.world.relations);
    output += ",\"regions\":" + std::to_string(snapshot.world.regions);
    output += ",\"region_fidelity\":{\"full\":" + std::to_string(snapshot.world.full_regions);
    output += ",\"reduced\":" + std::to_string(snapshot.world.reduced_regions);
    output += ",\"inactive\":" + std::to_string(snapshot.world.inactive_regions) + "}}";

    output += ",\"camera\":{\"entity\":";
    if (snapshot.camera.entity.has_value()) {
        append_json_string(output, snapshot.camera.entity->to_string());
    } else {
        output += "null";
    }
    output += ",\"global_position\":";
    if (snapshot.camera.global_position.has_value()) {
        output.push_back('[');
        append_double(output, snapshot.camera.global_position->x);
        output.push_back(',');
        append_double(output, snapshot.camera.global_position->y);
        output.push_back(',');
        append_double(output, snapshot.camera.global_position->z);
        output.push_back(']');
    } else {
        output += "null";
    }
    output += ",\"region\":";
    if (snapshot.camera.region.has_value()) {
        output += std::to_string(snapshot.camera.region->value);
    } else {
        output += "null";
    }
    output.push_back('}');

    output += ",\"presentation\":{\"backend\":";
    append_json_string(output, runtime_backend_name(snapshot.presentation.backend));
    output += ",\"width\":" + std::to_string(snapshot.presentation.width);
    output += ",\"height\":" + std::to_string(snapshot.presentation.height);
    output += ",\"drawable\":";
    output += snapshot.presentation.drawable ? "true" : "false";
    output.push_back('}');

    output += ",\"render_device\":{\"capabilities\":";
    append_device_capabilities(output, snapshot.render_device.capabilities);
    output += ",\"counters\":";
    append_device_counters(output, snapshot.render_device.counters);
    output.push_back('}');

    output += ",\"renderer\":{\"available\":";
    output += snapshot.renderer.available() ? "true" : "false";
    output += ",\"directional_shadow\":";
    if (snapshot.renderer.directional_shadow.has_value()) {
        append_shadow_telemetry(output, *snapshot.renderer.directional_shadow);
    } else {
        output += "null";
    }
    output += ",\"ray_tracing\":";
    if (snapshot.renderer.ray_tracing.has_value()) {
        append_ray_tracing_telemetry(output, *snapshot.renderer.ray_tracing);
    } else {
        output += "null";
    }
    output += "}}\n";
    return output;
}

} // namespace vice::runtime
