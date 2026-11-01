#include "builtin_shader_paths.hpp"
#include "reflection_surface_guide_bridge.hpp"

#include <vice/runtime/ray_reflection_history.hpp>

#include <memory>
#include <string>
#include <utility>

namespace vice::runtime::detail {
namespace {

struct RuntimeRayReflectionHistorySceneState {
    const RuntimeSceneRenderer* owner = nullptr;
    render::Device* device = nullptr;
    std::unique_ptr<RuntimeRayReflectionHistoryPass> pass;
};

thread_local RuntimeRayReflectionHistorySceneState reflection_history_scene_state;

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] bool ensure_reflection_history_pass(
    const RuntimeSceneRenderer* const owner,
    render::Device& device,
    std::string* const diagnostics
) {
    auto& state = reflection_history_scene_state;
    if (state.owner == owner && state.device == &device && state.pass != nullptr) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    state.pass.reset();
    state.owner = owner;
    state.device = &device;
    state.pass = std::make_unique<RuntimeRayReflectionHistoryPass>();
#ifdef VICE_RUNTIME_RAY_REFLECTION_HISTORY_MANIFEST
    return state.pass->initialize(
        device,
        ::vice::runtime::detail::builtin_shader_manifest(VICE_RUNTIME_RAY_REFLECTION_HISTORY_MANIFEST),
        diagnostics
    );
#else
    return state.pass->initialize(device, {}, diagnostics);
#endif
}

} // namespace

bool runtime_ray_reflection_history_after_trace(
    const RuntimeSceneRenderer* const owner,
    render::Device& device,
    render::CommandList& commands,
    render::Texture* const reflection_radiance,
    render::Buffer* const reflection_hits,
    render::Texture* const previous_scene_color,
    render::Texture* const previous_depth,
    const RuntimeTemporalFrameState& temporal,
    const platform::WindowMetrics metrics,
    std::string* const diagnostics
) noexcept {
    if (!temporal.history_valid || owner == nullptr || reflection_radiance == nullptr ||
        reflection_hits == nullptr || metrics.drawable_width == 0U ||
        metrics.drawable_height == 0U) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (previous_scene_color == nullptr || previous_depth == nullptr) {
        set_diagnostics(
            diagnostics,
            "VICE Runtime reflection history is marked valid but its frame resources are unavailable."
        );
        return false;
    }
    if (!ensure_reflection_history_pass(owner, device, diagnostics)) return false;

    auto& state = reflection_history_scene_state;
    if (state.pass == nullptr || !state.pass->available()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    const auto constants = make_runtime_ray_reflection_history_constants(
        temporal.previous_view_projection,
        metrics.drawable_width,
        metrics.drawable_height,
        true,
        temporal.current_camera_global_position,
        temporal.previous_camera_global_position
    );
    if (!constants.has_value()) {
        set_diagnostics(
            diagnostics,
            "VICE Runtime could not build origin-aware reflection-history reprojection constants."
        );
        return false;
    }

    return state.pass->dispatch(
        commands,
        *reflection_radiance,
        *reflection_hits,
        *previous_scene_color,
        *previous_depth,
        *constants,
        diagnostics
    );
}

} // namespace vice::runtime::detail
