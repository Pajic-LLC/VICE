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
};

thread_local RuntimeRayDiffuseGiSceneState runtime_ray_diffuse_gi_scene_state;

void clear_diagnostics(std::string* const diagnostics) noexcept {
    if (diagnostics != nullptr) diagnostics->clear();
}

void set_diagnostics(std::string* const diagnostics, const char* const message) {
    if (diagnostics != nullptr) *diagnostics = message;
}

[[nodiscard]] RuntimeRayDiffuseGiPipelineManifests runtime_ray_diffuse_gi_manifests() {
    RuntimeRayDiffuseGiPipelineManifests manifests{};
#ifdef VICE_RUNTIME_RAY_DIFFUSE_GI_MANIFEST
    manifests.trace = VICE_RUNTIME_RAY_DIFFUSE_GI_MANIFEST;
#endif
#ifdef VICE_RUNTIME_RAY_DIFFUSE_GI_HISTORY_MANIFEST
    manifests.hit_history = VICE_RUNTIME_RAY_DIFFUSE_GI_HISTORY_MANIFEST;
#endif
#ifdef VICE_RUNTIME_RAY_DIFFUSE_GI_DENOISE_MANIFEST
    manifests.denoise = VICE_RUNTIME_RAY_DIFFUSE_GI_DENOISE_MANIFEST;
#endif
#ifdef VICE_RUNTIME_RAY_DIFFUSE_GI_TEMPORAL_MANIFEST
    manifests.temporal = VICE_RUNTIME_RAY_DIFFUSE_GI_TEMPORAL_MANIFEST;
#endif
    return manifests;
}

[[nodiscard]] bool ensure_runtime_ray_diffuse_gi_pipeline(
    const RuntimeSceneRenderer* const owner,
    render::Device& device,
    std::string* const diagnostics
) {
    auto& state = runtime_ray_diffuse_gi_scene_state;
    if (state.owner == owner && state.device == &device && state.pipeline != nullptr) {
        clear_diagnostics(diagnostics);
        return true;
    }

    state = {};
    state.owner = owner;
    state.device = &device;
    state.pipeline = std::make_unique<RuntimeRayDiffuseGiPipeline>();
    if (!state.pipeline->initialize(device, runtime_ray_diffuse_gi_manifests(), diagnostics)) {
        state.pipeline.reset();
        return false;
    }
    clear_diagnostics(diagnostics);
    return true;
}

} // namespace

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

    const RuntimeRayDiffuseGiSettings settings = owner->ray_diffuse_gi_settings();
    if (!is_valid(settings)) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI settings are invalid at render time.");
        return false;
    }
    if (!settings.enabled || !device.capabilities().ray_tracing) {
        clear_diagnostics(diagnostics);
        return true;
    }

    if (current_depth == nullptr || previous_depth == nullptr || motion_vectors == nullptr ||
        surface_guide == nullptr || previous_scene_color == nullptr || ray_scene == nullptr ||
        !environment.available || environment.specular == nullptr || environment.sampler == nullptr ||
        metrics.drawable_width == 0U || metrics.drawable_height == 0U) {
        clear_diagnostics(diagnostics);
        return true;
    }

    auto* const tlas = ray_scene->active_tlas();
    auto* const scene_index_buffer = ray_scene->active_scene_instance_index_buffer();
    const auto scene_indices = ray_scene->active_scene_instance_indices();
    if (tlas == nullptr || scene_index_buffer == nullptr || scene_indices.empty()) {
        clear_diagnostics(diagnostics);
        return true;
    }
    if (scene_indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(diagnostics, "Runtime diffuse GI scene-index mapping exceeds the supported range.");
        return false;
    }

    if (!ensure_runtime_ray_diffuse_gi_pipeline(owner, device, diagnostics)) return false;
    auto& state = runtime_ray_diffuse_gi_scene_state;
    if (state.pipeline == nullptr || !state.pipeline->available()) {
        clear_diagnostics(diagnostics);
        return true;
    }
    if (!state.pipeline->resize(metrics.drawable_width, metrics.drawable_height, diagnostics)) {
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
    if (!state.pipeline->dispatch(input, diagnostics)) return false;

    auto* const radiance = state.pipeline->radiance();
    if (radiance != nullptr) {
        runtime_ray_diffuse_gi_publish_gpu_pbr(owner, radiance);
    }
    clear_diagnostics(diagnostics);
    return true;
}

} // namespace vice::runtime::detail
