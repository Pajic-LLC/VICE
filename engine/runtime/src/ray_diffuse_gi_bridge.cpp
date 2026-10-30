#include "builtin_shader_paths.hpp"
#include "ray_diffuse_gi_pipeline.hpp"
#include "reflection_surface_guide_bridge.hpp"

#include <limits>
#include <memory>
#include <string>

namespace vice::runtime::detail {
namespace {

struct RuntimeRayDiffuseGiSceneState final {
    const RuntimeSceneRenderer* owner = nullptr;
    render::Device* device = nullptr;
    std::unique_ptr<RuntimeRayDiffuseGiPipeline> pipeline;
    RuntimeRayDiffuseGiTelemetry telemetry{};
};

thread_local RuntimeRayDiffuseGiSceneState runtime_ray_diffuse_gi_scene_state;

void clear_diagnostics(std::string* const diagnostics) noexcept {
    if (diagnostics != nullptr) diagnostics->clear();
}

void set_diagnostics(std::string* const diagnostics, const char* const message) {
    if (diagnostics != nullptr) *diagnostics = message;
}

void prepare_runtime_ray_diffuse_gi_scene_state(
    const RuntimeSceneRenderer* const owner,
    render::Device& device
) {
    auto& state = runtime_ray_diffuse_gi_scene_state;
    if (state.owner == owner && state.device == &device) return;
    state = {};
    state.owner = owner;
    state.device = &device;
}

[[nodiscard]] RuntimeRayDiffuseGiPipelineManifests runtime_ray_diffuse_gi_manifests() {
    RuntimeRayDiffuseGiPipelineManifests manifests{};
#ifdef VICE_RUNTIME_RAY_DIFFUSE_GI_MANIFEST
    manifests.trace = ::vice::runtime::detail::builtin_shader_manifest(VICE_RUNTIME_RAY_DIFFUSE_GI_MANIFEST);
#endif
#ifdef VICE_RUNTIME_RAY_DIFFUSE_GI_HISTORY_MANIFEST
    manifests.hit_history = ::vice::runtime::detail::builtin_shader_manifest(VICE_RUNTIME_RAY_DIFFUSE_GI_HISTORY_MANIFEST);
#endif
#ifdef VICE_RUNTIME_RAY_DIFFUSE_GI_DENOISE_MANIFEST
    manifests.denoise = ::vice::runtime::detail::builtin_shader_manifest(VICE_RUNTIME_RAY_DIFFUSE_GI_DENOISE_MANIFEST);
#endif
#ifdef VICE_RUNTIME_RAY_DIFFUSE_GI_TEMPORAL_MANIFEST
    manifests.temporal = ::vice::runtime::detail::builtin_shader_manifest(VICE_RUNTIME_RAY_DIFFUSE_GI_TEMPORAL_MANIFEST);
#endif
    return manifests;
}

[[nodiscard]] bool ensure_runtime_ray_diffuse_gi_pipeline(
    render::Device& device,
    std::string* const diagnostics
) {
    auto& state = runtime_ray_diffuse_gi_scene_state;
    if (state.pipeline != nullptr) {
        clear_diagnostics(diagnostics);
        return true;
    }

    state.pipeline = std::make_unique<RuntimeRayDiffuseGiPipeline>();
    if (!state.pipeline->initialize(device, runtime_ray_diffuse_gi_manifests(), diagnostics)) {
        state.pipeline.reset();
        return false;
    }
    clear_diagnostics(diagnostics);
    return true;
}

} // namespace

RuntimeRayDiffuseGiTelemetry runtime_ray_diffuse_gi_telemetry_snapshot(
    const RuntimeSceneRenderer* const owner
) noexcept {
    const auto& state = runtime_ray_diffuse_gi_scene_state;
    if (owner != nullptr && state.owner == owner) return state.telemetry;

    RuntimeRayDiffuseGiTelemetry telemetry{};
    if (owner != nullptr) telemetry.settings = owner->ray_diffuse_gi_settings();
    return telemetry;
}

bool runtime_ray_diffuse_gi_after_surface_guide(
    const RuntimeSceneRenderer* const owner,
    render::Device& device,
    render::CommandList& commands,
    render::Texture* const current_depth,
    render::Texture* const previous_depth,
    render::Texture* const motion_vectors,
    render::Texture* const surface_guide,
    render::Texture* const previous_scene_color,
    RuntimeRayTracingSceneExecutor* const ray_scene,
    const RuntimeCameraView& camera,
    const float jitter_x_ndc,
    const float jitter_y_ndc,
    const RuntimeTemporalFrameState& temporal,
    const platform::WindowMetrics metrics,
    const RuntimeEnvironmentBinding environment,
    std::string* const diagnostics
) noexcept {
    if (owner == nullptr) {
        clear_diagnostics(diagnostics);
        return true;
    }

    prepare_runtime_ray_diffuse_gi_scene_state(owner, device);
    auto& state = runtime_ray_diffuse_gi_scene_state;
    const RuntimeRayDiffuseGiSettings settings = owner->ray_diffuse_gi_settings();
    state.telemetry = RuntimeRayDiffuseGiTelemetry{
        .settings = settings,
        .fallback_reason = RuntimeRayDiffuseGiFallbackReason::not_rendered,
        .frame_index = temporal.frame_index,
        .ray_tracing_supported = device.capabilities().ray_tracing,
        .pipeline_available = state.pipeline != nullptr && state.pipeline->available(),
        .scene_available = false,
        .history_valid = temporal.history_valid,
        .active = false,
    };

    if (!is_valid(settings)) {
        state.telemetry.fallback_reason = RuntimeRayDiffuseGiFallbackReason::invalid_settings;
        set_diagnostics(diagnostics, "Runtime diffuse-GI settings are invalid at render time.");
        return false;
    }
    if (!settings.enabled) {
        state.telemetry.fallback_reason = RuntimeRayDiffuseGiFallbackReason::disabled;
        clear_diagnostics(diagnostics);
        return true;
    }
    if (!state.telemetry.ray_tracing_supported) {
        state.telemetry.fallback_reason = RuntimeRayDiffuseGiFallbackReason::ray_tracing_unsupported;
        clear_diagnostics(diagnostics);
        return true;
    }

    if (current_depth == nullptr || previous_depth == nullptr || motion_vectors == nullptr ||
        surface_guide == nullptr || previous_scene_color == nullptr || ray_scene == nullptr ||
        metrics.drawable_width == 0U || metrics.drawable_height == 0U) {
        state.telemetry.fallback_reason = RuntimeRayDiffuseGiFallbackReason::frame_inputs_unavailable;
        clear_diagnostics(diagnostics);
        return true;
    }
    if (!environment.available || environment.specular == nullptr || environment.sampler == nullptr) {
        state.telemetry.fallback_reason = RuntimeRayDiffuseGiFallbackReason::environment_unavailable;
        clear_diagnostics(diagnostics);
        return true;
    }

    auto* const tlas = ray_scene->active_tlas();
    auto* const scene_index_buffer = ray_scene->active_scene_instance_index_buffer();
    const auto scene_indices = ray_scene->active_scene_instance_indices();
    if (tlas == nullptr || scene_index_buffer == nullptr || scene_indices.empty()) {
        state.telemetry.fallback_reason = RuntimeRayDiffuseGiFallbackReason::scene_unavailable;
        clear_diagnostics(diagnostics);
        return true;
    }
    if (scene_indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        state.telemetry.fallback_reason = RuntimeRayDiffuseGiFallbackReason::scene_unavailable;
        set_diagnostics(diagnostics, "Runtime diffuse GI scene-index mapping exceeds the supported range.");
        return false;
    }
    state.telemetry.scene_available = true;

    if (!ensure_runtime_ray_diffuse_gi_pipeline(device, diagnostics)) {
        state.telemetry.fallback_reason = RuntimeRayDiffuseGiFallbackReason::pipeline_failure;
        return false;
    }
    state.telemetry.pipeline_available = state.pipeline != nullptr && state.pipeline->available();
    if (!state.telemetry.pipeline_available) {
        state.telemetry.fallback_reason = RuntimeRayDiffuseGiFallbackReason::pipeline_unavailable;
        clear_diagnostics(diagnostics);
        return true;
    }
    if (!state.pipeline->resize(metrics.drawable_width, metrics.drawable_height, diagnostics)) {
        state.telemetry.fallback_reason = RuntimeRayDiffuseGiFallbackReason::pipeline_failure;
        return false;
    }

    const RuntimeRayDiffuseGiCamera ray_camera{
        .position = {},
        .forward = camera.forward,
        .up = camera.up,
        .vertical_fov_radians = camera.camera.vertical_fov_radians,
        .aspect_ratio = camera.aspect_ratio,
        .near_plane = camera.camera.near_plane,
        .far_plane = camera.camera.far_plane,
        .jitter_x_ndc = jitter_x_ndc,
        .jitter_y_ndc = jitter_y_ndc,
    };
    const RuntimeRayDiffuseGiPipelineInput input{
        .commands = &commands,
        .current_depth = current_depth,
        .previous_depth = previous_depth,
        .motion_vectors = motion_vectors,
        .surface_guide = surface_guide,
        .previous_scene_color = previous_scene_color,
        .scene_tlas = tlas,
        .scene_instance_indices = scene_index_buffer,
        .scene_instance_index_count = static_cast<std::uint32_t>(scene_indices.size()),
        .environment_specular = environment.specular,
        .environment_sampler = environment.sampler,
        .camera = ray_camera,
        .temporal = temporal,
        .settings = settings,
        .environment_intensity = environment.intensity,
        .environment_rotation_radians = environment.rotation_radians,
        .width = metrics.drawable_width,
        .height = metrics.drawable_height,
    };
    if (!state.pipeline->dispatch(input, diagnostics)) {
        state.telemetry.fallback_reason = RuntimeRayDiffuseGiFallbackReason::pipeline_failure;
        return false;
    }

    auto* const radiance = state.pipeline->radiance();
    if (radiance == nullptr) {
        state.telemetry.fallback_reason = RuntimeRayDiffuseGiFallbackReason::pipeline_failure;
        set_diagnostics(diagnostics, "Runtime diffuse-GI pipeline produced no temporal output.");
        return false;
    }

    runtime_ray_diffuse_gi_publish_gpu_pbr(owner, radiance);
    state.telemetry.active = true;
    state.telemetry.fallback_reason = RuntimeRayDiffuseGiFallbackReason::none;
    clear_diagnostics(diagnostics);
    return true;
}

} // namespace vice::runtime::detail

namespace vice::runtime {

RuntimeRayDiffuseGiTelemetry runtime_ray_diffuse_gi_telemetry(
    const RuntimeSceneRenderer& renderer
) noexcept {
    return detail::runtime_ray_diffuse_gi_telemetry_snapshot(&renderer);
}

} // namespace vice::runtime
