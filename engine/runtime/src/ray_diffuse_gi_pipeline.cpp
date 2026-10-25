#include "ray_diffuse_gi_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace vice::runtime::detail {
namespace {

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] bool finite_environment(const RuntimeRayDiffuseGiPipelineInput& input) noexcept {
    return std::isfinite(input.environment_intensity) && input.environment_intensity >= 0.0F &&
        std::isfinite(input.environment_rotation_radians);
}

} // namespace

bool RuntimeRayDiffuseGiPipeline::initialize(
    render::Device& device,
    const RuntimeRayDiffuseGiPipelineManifests& manifests,
    std::string* const diagnostics
) {
    initialized_ = false;
    valid_ = false;
    width_ = 0U;
    height_ = 0U;

    if (!trace_.initialize(device, manifests.trace, diagnostics) ||
        !hit_history_.initialize(device, manifests.hit_history, diagnostics) ||
        !denoise_.initialize(device, manifests.denoise, diagnostics) ||
        !temporal_.initialize(device, manifests.temporal, diagnostics)) {
        return false;
    }

    initialized_ = true;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayDiffuseGiPipeline::resize(
    const std::uint32_t width,
    const std::uint32_t height,
    std::string* const diagnostics
) {
    valid_ = false;
    if (!initialized_) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI pipeline is not initialized.");
        return false;
    }
    if (!trace_.resize(width, height, diagnostics) ||
        !denoise_.resize(width, height, diagnostics) ||
        !temporal_.resize(width, height, diagnostics)) {
        return false;
    }
    width_ = width;
    height_ = height;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayDiffuseGiPipeline::dispatch(
    const RuntimeRayDiffuseGiPipelineInput& input,
    std::string* const diagnostics
) {
    valid_ = false;
    if (!initialized_) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI pipeline is not initialized.");
        return false;
    }
    if (!is_valid(input.settings)) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI pipeline received invalid quality settings.");
        return false;
    }
    if (!input.settings.enabled || !available()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (input.commands == nullptr || input.current_depth == nullptr ||
        input.previous_depth == nullptr || input.motion_vectors == nullptr ||
        input.surface_guide == nullptr || input.previous_scene_color == nullptr ||
        input.scene_tlas == nullptr || input.scene_instance_indices == nullptr ||
        input.scene_instance_index_count == 0U || input.environment_specular == nullptr ||
        input.environment_sampler == nullptr || input.width == 0U || input.height == 0U ||
        input.width != width_ || input.height != height_ || !finite_environment(input)) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI pipeline inputs are incomplete or incompatible.");
        return false;
    }

    const float max_distance = std::min(input.settings.max_distance, input.camera.far_plane);
    if (!std::isfinite(max_distance) || max_distance <= input.settings.surface_bias) {
        set_diagnostics(
            diagnostics,
            "Runtime diffuse-GI ray range is incompatible with the active camera far plane."
        );
        return false;
    }

    const auto trace_constants = make_runtime_ray_diffuse_gi_constants(
        input.camera,
        input.width,
        input.height,
        input.environment_intensity,
        input.environment_rotation_radians,
        static_cast<std::uint32_t>(input.temporal.frame_index % runtime_ray_diffuse_gi_sample_period),
        max_distance,
        input.settings.surface_bias
    );
    if (!trace_constants.has_value() ||
        !trace_.dispatch(
            *input.commands,
            *input.current_depth,
            *input.surface_guide,
            *input.scene_tlas,
            *input.scene_instance_indices,
            input.scene_instance_index_count,
            *input.environment_specular,
            *input.environment_sampler,
            *trace_constants,
            diagnostics
        )) {
        if (trace_constants.has_value() || diagnostics == nullptr || diagnostics->empty()) {
            if (!trace_constants.has_value()) {
                set_diagnostics(diagnostics, "Runtime diffuse-GI trace constants are invalid.");
            }
        }
        return false;
    }

    auto* const raw_radiance = trace_.radiance();
    auto* const hits = trace_.hits();
    if (raw_radiance == nullptr || hits == nullptr) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI trace produced no current-frame outputs.");
        return false;
    }

    const auto history_constants = make_runtime_ray_hit_radiance_history_constants(
        input.temporal.previous_unjittered_view_projection,
        input.width,
        input.height,
        input.temporal.history_valid,
        input.temporal.current_camera_global_position,
        input.temporal.previous_camera_global_position
    );
    if (!history_constants.has_value()) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI hit-radiance history constants are invalid.");
        return false;
    }
    if (!hit_history_.dispatch(
            *input.commands,
            *raw_radiance,
            *hits,
            *input.previous_scene_color,
            *input.previous_depth,
            *history_constants,
            diagnostics
        )) {
        return false;
    }

    const auto denoise_constants = make_runtime_ray_diffuse_gi_denoise_constants(
        input.width,
        input.height,
        input.camera.near_plane,
        input.camera.far_plane,
        0.025F,
        24.0F,
        1.5F,
        input.settings.spatial_kernel_radius
    );
    if (!denoise_constants.has_value()) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI spatial reconstruction constants are invalid.");
        return false;
    }
    if (!denoise_.dispatch(
            *input.commands,
            *raw_radiance,
            *input.current_depth,
            *input.surface_guide,
            *denoise_constants,
            diagnostics
        )) {
        return false;
    }
    auto* const denoised_radiance = denoise_.radiance();
    if (denoised_radiance == nullptr) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI spatial reconstruction produced no output.");
        return false;
    }

    const auto temporal_constants = make_runtime_ray_diffuse_gi_temporal_constants(
        input.width,
        input.height,
        input.temporal.history_valid,
        input.settings.temporal_history_weight
    );
    if (!temporal_constants.has_value()) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI temporal reconstruction constants are invalid.");
        return false;
    }
    if (!temporal_.dispatch(
            *input.commands,
            *denoised_radiance,
            *input.motion_vectors,
            *input.current_depth,
            *input.previous_depth,
            *temporal_constants,
            input.temporal.frame_index,
            diagnostics
        )) {
        return false;
    }
    if (temporal_.radiance() == nullptr) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI temporal reconstruction produced no output.");
        return false;
    }

    valid_ = true;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayDiffuseGiPipeline::available() const noexcept {
    return initialized_ && trace_.available() && hit_history_.available() &&
        denoise_.available() && temporal_.available();
}

render::Texture* RuntimeRayDiffuseGiPipeline::radiance() const noexcept {
    return valid_ ? temporal_.radiance() : nullptr;
}

} // namespace vice::runtime::detail
