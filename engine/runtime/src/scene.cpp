#include <vice/runtime/directional_ray_shadow.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/gpu_virtual_geometry.hpp>
#include <vice/runtime/gpu_virtual_geometry_feedback.hpp>
#include <vice/runtime/gpu_virtual_geometry_feedback_ring.hpp>
#include <vice/runtime/ray_ambient_occlusion.hpp>
#include <vice/runtime/ray_tracing_scene_executor.hpp>
#include <vice/runtime/render_streaming.hpp>
#include <vice/runtime/scene.hpp>
#include <vice/runtime/virtual_geometry.hpp>
#include <vice/runtime/virtual_geometry_gpu_arena.hpp>
#include <vice/runtime/virtual_geometry_streaming.hpp>
#include <vice/runtime/virtual_geometry_submission.hpp>
#include <vice/runtime/visibility.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace vice::runtime::detail {
[[nodiscard]] RuntimeRenderResidentSceneBuildResult
build_runtime_render_resident_scene_with_virtual_geometry(const world::World& world);

struct RuntimeDirectionalRayShadowRenderContext {
    RuntimeDirectionalRayShadowPass* pass = nullptr;
    RuntimeRayTracingSceneExecutor* ray_scene = nullptr;
    render::Texture* camera_depth = nullptr;
    render::Buffer* pbr_frame = nullptr;
    render::Texture* cascade_shadow = nullptr;
    RuntimeDirectionalRayShadowConstants constants{};
    RuntimeDirectionalShadowMode requested_mode = RuntimeDirectionalShadowMode::automatic;
    RuntimeDirectionalShadowTelemetry* telemetry = nullptr;
    std::string* diagnostics = nullptr;
    bool camera_depth_active = false;
    bool constants_ready = false;
    bool visibility_ready = false;
    bool gpu_pbr_active = false;
    bool gpu_pbr_visibility_marker_ready = false;
    bool direct_visibility_marker_ready = false;
};

struct RuntimeRayAmbientOcclusionRenderContext {
    RuntimeRayAmbientOcclusionPass* pass = nullptr;
    RuntimeRayTracingSceneExecutor* ray_scene = nullptr;
    render::Texture* camera_depth = nullptr;
    render::Buffer* pbr_frame = nullptr;
    render::Texture* cascade_shadow = nullptr;
    render::Buffer* direct_frame = nullptr;
    std::uint64_t direct_frame_offset = 0U;
    RuntimeRayAmbientOcclusionConstants constants{};
    std::string* diagnostics = nullptr;
    bool enabled = false;
    bool camera_depth_active = false;
    bool constants_ready = false;
    bool visibility_ready = false;
    bool gpu_pbr_visibility_marker_ready = false;
    bool binding_failed = false;
};

extern thread_local RuntimeDirectionalRayShadowRenderContext*
    active_directional_ray_shadow_render_context;
extern thread_local RuntimeRayAmbientOcclusionRenderContext*
    active_ray_ambient_occlusion_render_context;

[[nodiscard]] RuntimeDirectionalShadowPath runtime_resolve_directional_shadow_path(
    RuntimeDirectionalShadowMode requested_mode,
    bool ray_tracing_supported
) noexcept;

[[nodiscard]] bool runtime_ray_ambient_occlusion_needs_camera_depth_prepass() noexcept;

[[nodiscard]] bool runtime_directional_ray_shadow_after_begin_depth(
    render::Texture& depth_attachment,
    float clear_depth,
    bool clear,
    render::Texture* current_depth,
    RuntimeRayTracingSceneExecutor* ray_scene,
    render::Buffer* pbr_frame,
    render::Texture* cascade_shadow,
    const RuntimeCameraView& camera,
    std::span<const RuntimeLight> lights,
    float jitter_x_ndc,
    float jitter_y_ndc,
    platform::WindowMetrics metrics,
    bool has_gpu_pbr
) noexcept;

[[nodiscard]] bool runtime_ray_ambient_occlusion_after_begin_depth(
    render::Texture& depth_attachment,
    float clear_depth,
    bool clear,
    render::Texture* current_depth,
    RuntimeRayTracingSceneExecutor* ray_scene,
    const RuntimeCameraView& camera,
    float jitter_x_ndc,
    float jitter_y_ndc,
    platform::WindowMetrics metrics
) noexcept;

[[nodiscard]] bool runtime_directional_ray_shadow_after_end_render_pass(
    render::CommandList& commands
) noexcept;

[[nodiscard]] bool runtime_ray_ambient_occlusion_after_end_render_pass(
    render::CommandList& commands
) noexcept;

[[nodiscard]] render::Buffer& runtime_ray_ambient_occlusion_prepare_graphics_constants(
    const render::ShaderBindingKey& binding,
    render::Buffer& buffer,
    std::uint64_t offset,
    std::uint64_t size
) noexcept;

[[nodiscard]] render::Texture& runtime_ray_ambient_occlusion_prepare_graphics_texture(
    render::CommandList& commands,
    const render::ShaderBindingKey& binding,
    render::Texture& texture
) noexcept;

[[nodiscard]] render::Buffer& runtime_directional_ray_shadow_prepare_graphics_constants(
    const render::ShaderBindingKey& binding,
    render::Buffer& buffer,
    std::uint64_t offset,
    std::uint64_t size
) noexcept;

[[nodiscard]] render::Texture& runtime_directional_shadow_texture_override(
    const render::ShaderBindingKey& binding,
    render::Texture& texture
) noexcept;
} // namespace vice::runtime::detail

#define build_runtime_render_resident_scene(...) \
    detail::build_runtime_render_resident_scene_with_virtual_geometry(__VA_ARGS__)
#define render(...) render_legacy(__VA_ARGS__)
#define set_vertex_buffer(...) set_vertex_buffer_resolved(__VA_ARGS__)
#define set_index_buffer(...) set_index_buffer_resolved(__VA_ARGS__)
#define resolve_runtime_directional_shadow_path(requested_mode, ray_tracing_supported) \
    detail::runtime_resolve_directional_shadow_path((requested_mode), (ray_tracing_supported))
// Intentionally unparenthesized. The legacy expression is
// `shadow_view.has_value() && runtime_directional_shadow_requires_depth_prepass(path)`;
// `&&` binds before this `||`, so RTAO can request current depth even without a shadow view.
#define runtime_directional_shadow_requires_depth_prepass(path) \
    ::vice::runtime::runtime_directional_shadow_requires_depth_prepass((path)) || \
        detail::runtime_ray_ambient_occlusion_needs_camera_depth_prepass()
#define begin_depth_render_pass(...) \
    begin_depth_render_pass(__VA_ARGS__) || \
        !detail::runtime_directional_ray_shadow_after_begin_depth( \
            __VA_ARGS__, \
            current_depth, \
            &state_->ray_tracing_scene, \
            state_->gpu_pbr_frame.get(), \
            state_->shadow_maps[0].get(), \
            runtime_camera, \
            std::span<const RuntimeLight>{authored_lights}, \
            jitter.x_ndc, \
            jitter.y_ndc, \
            metrics, \
            !gpu_pbr_plan.batches.empty() || !gpu_cluster_pbr_plan.batches.empty() \
        ) || \
        !detail::runtime_ray_ambient_occlusion_after_begin_depth( \
            __VA_ARGS__, \
            current_depth, \
            &state_->ray_tracing_scene, \
            runtime_camera, \
            jitter.x_ndc, \
            jitter.y_ndc, \
            metrics \
        )
#define end_render_pass(...) \
    end_render_pass(__VA_ARGS__) || \
        !detail::runtime_directional_ray_shadow_after_end_render_pass(*commands) || \
        !detail::runtime_ray_ambient_occlusion_after_end_render_pass(*commands)
#define set_graphics_constant_buffer(binding, buffer, offset, size) \
    set_graphics_constant_buffer( \
        (binding), \
        detail::runtime_ray_ambient_occlusion_prepare_graphics_constants( \
            (binding), \
            detail::runtime_directional_ray_shadow_prepare_graphics_constants( \
                (binding), (buffer), (offset), (size) \
            ), \
            (offset), \
            (size) \
        ), \
        (offset), \
        (size) \
    )
#define set_graphics_texture(binding, texture) \
    set_graphics_texture( \
        (binding), \
        detail::runtime_directional_shadow_texture_override( \
            (binding), \
            detail::runtime_ray_ambient_occlusion_prepare_graphics_texture( \
                *commands, (binding), (texture) \
            ) \
        ) \
    )
#include "scene_legacy.inc"
#undef set_graphics_texture
#undef set_graphics_constant_buffer
#undef end_render_pass
#undef begin_depth_render_pass
#undef runtime_directional_shadow_requires_depth_prepass
#undef resolve_runtime_directional_shadow_path
#undef set_index_buffer
#undef set_vertex_buffer
#undef render
#undef build_runtime_render_resident_scene

namespace vice::runtime::detail {

thread_local RuntimeDirectionalRayShadowRenderContext*
    active_directional_ray_shadow_render_context = nullptr;
thread_local RuntimeRayAmbientOcclusionRenderContext*
    active_ray_ambient_occlusion_render_context = nullptr;

class RuntimeDirectionalRayShadowRenderScope final {
public:
    explicit RuntimeDirectionalRayShadowRenderScope(
        RuntimeDirectionalRayShadowRenderContext& context
    ) noexcept
        : previous_{active_directional_ray_shadow_render_context} {
        active_directional_ray_shadow_render_context = &context;
    }

    ~RuntimeDirectionalRayShadowRenderScope() {
        active_directional_ray_shadow_render_context = previous_;
    }

    RuntimeDirectionalRayShadowRenderScope(const RuntimeDirectionalRayShadowRenderScope&) = delete;
    RuntimeDirectionalRayShadowRenderScope& operator=(const RuntimeDirectionalRayShadowRenderScope&) = delete;

private:
    RuntimeDirectionalRayShadowRenderContext* previous_ = nullptr;
};

class RuntimeRayAmbientOcclusionRenderScope final {
public:
    explicit RuntimeRayAmbientOcclusionRenderScope(
        RuntimeRayAmbientOcclusionRenderContext& context
    ) noexcept
        : previous_{active_ray_ambient_occlusion_render_context} {
        active_ray_ambient_occlusion_render_context = &context;
    }

    ~RuntimeRayAmbientOcclusionRenderScope() {
        active_ray_ambient_occlusion_render_context = previous_;
    }

    RuntimeRayAmbientOcclusionRenderScope(const RuntimeRayAmbientOcclusionRenderScope&) = delete;
    RuntimeRayAmbientOcclusionRenderScope& operator=(const RuntimeRayAmbientOcclusionRenderScope&) = delete;

private:
    RuntimeRayAmbientOcclusionRenderContext* previous_ = nullptr;
};

RuntimeDirectionalShadowPath runtime_resolve_directional_shadow_path(
    const RuntimeDirectionalShadowMode requested_mode,
    const bool ray_tracing_supported
) noexcept {
    const auto* const context = active_directional_ray_shadow_render_context;
    return ::vice::runtime::resolve_runtime_directional_shadow_path(
        context == nullptr ? requested_mode : context->requested_mode,
        ray_tracing_supported
    );
}

bool runtime_ray_ambient_occlusion_needs_camera_depth_prepass() noexcept {
    const auto* const context = active_ray_ambient_occlusion_render_context;
    return context != nullptr &&
        ::vice::runtime::runtime_ray_ambient_occlusion_needs_camera_depth_prepass(
            context->enabled,
            context->pass != nullptr && context->pass->available()
        );
}

bool runtime_directional_ray_shadow_after_begin_depth(
    render::Texture& depth_attachment,
    const float,
    const bool,
    render::Texture* const current_depth,
    RuntimeRayTracingSceneExecutor* const ray_scene,
    render::Buffer* const pbr_frame,
    render::Texture* const cascade_shadow,
    const RuntimeCameraView& camera,
    const std::span<const RuntimeLight> lights,
    const float jitter_x_ndc,
    const float jitter_y_ndc,
    const platform::WindowMetrics metrics,
    const bool has_gpu_pbr
) noexcept {
    auto* const context = active_directional_ray_shadow_render_context;
    if (context == nullptr || context->pass == nullptr || &depth_attachment != current_depth) {
        return true;
    }

    context->camera_depth_active = true;
    context->constants_ready = false;
    context->visibility_ready = false;
    context->gpu_pbr_active = has_gpu_pbr;
    context->gpu_pbr_visibility_marker_ready = false;
    context->direct_visibility_marker_ready = false;
    context->ray_scene = ray_scene;
    context->camera_depth = current_depth;
    context->pbr_frame = pbr_frame;
    context->cascade_shadow = cascade_shadow;

    if (!context->pass->available() || ray_scene == nullptr ||
        current_depth == nullptr || cascade_shadow == nullptr ||
        metrics.drawable_width == 0U || metrics.drawable_height == 0U) {
        return true;
    }

    const auto shadow_view = directional_shadow_view(camera, lights);
    if (!shadow_view.has_value() || shadow_view->light_index >= lights.size()) {
        return true;
    }
    const auto& shadow_light = lights[shadow_view->light_index];
    const RuntimeDirectionalRayShadowCamera ray_camera{
        .position = camera.position,
        .forward = camera.forward,
        .up = camera.up,
        .vertical_fov_radians = camera.camera.vertical_fov_radians,
        .aspect_ratio = camera.aspect_ratio,
        .near_plane = camera.camera.near_plane,
        .far_plane = camera.camera.far_plane,
        .jitter_x_ndc = jitter_x_ndc,
        .jitter_y_ndc = jitter_y_ndc,
    };
    const auto constants = make_runtime_directional_ray_shadow_constants(
        ray_camera,
        math::Vec3{
            -shadow_light.direction.x,
            -shadow_light.direction.y,
            -shadow_light.direction.z,
        },
        metrics.drawable_width,
        metrics.drawable_height
    );
    if (!constants.has_value()) {
        if (context->diagnostics != nullptr) {
            *context->diagnostics =
                "VICE Runtime could not build directional ray-shadow camera constants.";
        }
        return false;
    }

    context->constants = *constants;
    context->constants_ready = true;
    return true;
}

bool runtime_ray_ambient_occlusion_after_begin_depth(
    render::Texture& depth_attachment,
    const float,
    const bool,
    render::Texture* const current_depth,
    RuntimeRayTracingSceneExecutor* const ray_scene,
    const RuntimeCameraView& camera,
    const float jitter_x_ndc,
    const float jitter_y_ndc,
    const platform::WindowMetrics metrics
) noexcept {
    auto* const context = active_ray_ambient_occlusion_render_context;
    if (context == nullptr || !context->enabled || context->pass == nullptr ||
        &depth_attachment != current_depth) {
        return true;
    }

    context->camera_depth_active = true;
    context->constants_ready = false;
    context->visibility_ready = false;
    context->ray_scene = ray_scene;
    context->camera_depth = current_depth;

    if (!context->pass->available() || ray_scene == nullptr || current_depth == nullptr ||
        metrics.drawable_width == 0U || metrics.drawable_height == 0U) {
        return true;
    }

    const RuntimeRayAmbientOcclusionCamera ray_camera{
        .position = camera.position,
        .forward = camera.forward,
        .up = camera.up,
        .vertical_fov_radians = camera.camera.vertical_fov_radians,
        .aspect_ratio = camera.aspect_ratio,
        .near_plane = camera.camera.near_plane,
        .far_plane = camera.camera.far_plane,
        .jitter_x_ndc = jitter_x_ndc,
        .jitter_y_ndc = jitter_y_ndc,
    };
    const auto constants = make_runtime_ray_ambient_occlusion_constants(
        ray_camera,
        metrics.drawable_width,
        metrics.drawable_height
    );
    if (!constants.has_value()) {
        if (context->diagnostics != nullptr) {
            *context->diagnostics = "VICE Runtime could not build ray ambient-occlusion camera constants.";
        }
        return false;
    }

    context->constants = *constants;
    context->constants_ready = true;
    return true;
}

bool runtime_directional_ray_shadow_after_end_render_pass(
    render::CommandList& commands
) noexcept {
    auto* const context = active_directional_ray_shadow_render_context;
    if (context == nullptr || !context->camera_depth_active) return true;
    context->camera_depth_active = false;

    if (!context->constants_ready || context->pass == nullptr ||
        context->ray_scene == nullptr || context->camera_depth == nullptr) {
        return true;
    }
    auto* const tlas = context->ray_scene->active_tlas();
    if (tlas == nullptr) return true;

    if (!context->pass->dispatch(
            commands,
            *context->camera_depth,
            *tlas,
            context->constants,
            context->diagnostics
        )) {
        return false;
    }
    if (context->pass->visibility() == nullptr) {
        if (context->diagnostics != nullptr) {
            *context->diagnostics = "VICE Runtime directional ray-shadow visibility is unavailable.";
        }
        return false;
    }

    context->visibility_ready = true;
    if (context->telemetry != nullptr) {
        context->telemetry->active_path = RuntimeDirectionalShadowPath::ray_traced;
        context->telemetry->ray_query_visibility_available = true;
    }

    if (context->gpu_pbr_active) {
        if (context->pbr_frame == nullptr) {
            if (context->diagnostics != nullptr) {
                *context->diagnostics =
                    "VICE Runtime GPU PBR frame constants are unavailable for ray-traced shadows.";
            }
            return false;
        }
        constexpr std::size_t mode_offset =
            offsetof(GpuPbrSceneConstants, shadow_camera_forward) + sizeof(float) * 3U;
        const float ray_traced_mode = 1.0F;
        if (!context->pbr_frame->write(
                mode_offset,
                std::as_bytes(std::span{&ray_traced_mode, 1})
            )) {
            if (context->diagnostics != nullptr) {
                *context->diagnostics =
                    "VICE Runtime could not select ray-traced directional shadows for GPU PBR.";
            }
            return false;
        }
        context->gpu_pbr_visibility_marker_ready = true;
    }

    return true;
}

bool runtime_ray_ambient_occlusion_after_end_render_pass(
    render::CommandList& commands
) noexcept {
    auto* const context = active_ray_ambient_occlusion_render_context;
    if (context == nullptr) return true;
    if (context->binding_failed) return false;
    if (!context->camera_depth_active) return true;
    context->camera_depth_active = false;

    if (!context->constants_ready || context->pass == nullptr ||
        context->ray_scene == nullptr || context->camera_depth == nullptr) {
        return true;
    }
    auto* const tlas = context->ray_scene->active_tlas();
    if (tlas == nullptr) return true;

    if (!context->pass->dispatch(
            commands,
            *context->camera_depth,
            *tlas,
            context->constants,
            context->diagnostics
        )) {
        return false;
    }
    if (context->pass->visibility() == nullptr) {
        if (context->diagnostics != nullptr) {
            *context->diagnostics = "VICE Runtime ray ambient-occlusion visibility is unavailable.";
        }
        return false;
    }

    context->visibility_ready = true;
    if (context->pbr_frame != nullptr) {
        constexpr std::size_t ao_offset =
            offsetof(GpuPbrSceneConstants, ambient_occlusion_metadata);
        const float ao_enabled = 1.0F;
        if (!context->pbr_frame->write(
                ao_offset,
                std::as_bytes(std::span{&ao_enabled, 1})
            )) {
            if (context->diagnostics != nullptr) {
                *context->diagnostics =
                    "VICE Runtime could not enable ray ambient occlusion for GPU PBR.";
            }
            return false;
        }
        context->gpu_pbr_visibility_marker_ready = true;
    }
    return true;
}

render::Buffer& runtime_ray_ambient_occlusion_prepare_graphics_constants(
    const render::ShaderBindingKey& binding,
    render::Buffer& buffer,
    const std::uint64_t offset,
    const std::uint64_t size
) noexcept {
    auto* const context = active_ray_ambient_occlusion_render_context;
    if (context == nullptr ||
        binding.register_class != render::ShaderRegisterClass::constant_buffer ||
        binding.binding != 0U || binding.space != 0U || binding.array_element != 0U) {
        return buffer;
    }

    context->direct_frame = nullptr;
    context->direct_frame_offset = 0U;
    if (size == sizeof(SceneConstants)) {
        context->direct_frame = &buffer;
        context->direct_frame_offset = offset;
    }
    return buffer;
}

render::Texture& runtime_ray_ambient_occlusion_prepare_graphics_texture(
    render::CommandList& commands,
    const render::ShaderBindingKey& binding,
    render::Texture& texture
) noexcept {
    auto* const context = active_ray_ambient_occlusion_render_context;
    if (context == nullptr || context->cascade_shadow != &texture ||
        binding.register_class != render::ShaderRegisterClass::shader_resource ||
        binding.array_element != 0U) {
        return texture;
    }

    const bool gpu_pbr_binding = binding.binding == 0U && binding.space == 2U;
    const bool direct_material_binding = binding.space == 0U;
    if (!gpu_pbr_binding && !direct_material_binding) return texture;

    render::Texture* ao_texture = nullptr;
    const bool visibility_marker_ready = gpu_pbr_binding
        ? context->gpu_pbr_visibility_marker_ready
        : true;
    if (context->visibility_ready && visibility_marker_ready && context->pass != nullptr) {
        ao_texture = context->pass->visibility();
    }
    render::Texture& resource = ao_texture == nullptr ? texture : *ao_texture;
    const render::ShaderBindingKey ao_binding{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = gpu_pbr_binding ? 25U : 17U,
        .space = gpu_pbr_binding ? 2U : 0U,
        .array_element = 0U,
    };

    if (!commands.set_graphics_texture(ao_binding, resource)) {
        if (gpu_pbr_binding) {
            context->binding_failed = true;
            if (context->diagnostics != nullptr) {
                *context->diagnostics =
                    "VICE Runtime could not bind ray ambient occlusion for GPU PBR.";
            }
        }
        return texture;
    }

    if (direct_material_binding && ao_texture != nullptr && context->direct_frame != nullptr) {
        constexpr std::size_t ao_offset = offsetof(SceneConstants, ambient_occlusion_metadata);
        const float ao_enabled = 1.0F;
        (void)context->direct_frame->write(
            context->direct_frame_offset + ao_offset,
            std::as_bytes(std::span{&ao_enabled, 1})
        );
    }
    return texture;
}

render::Buffer& runtime_directional_ray_shadow_prepare_graphics_constants(
    const render::ShaderBindingKey& binding,
    render::Buffer& buffer,
    const std::uint64_t offset,
    const std::uint64_t size
) noexcept {
    auto* const context = active_directional_ray_shadow_render_context;
    if (context == nullptr ||
        binding.register_class != render::ShaderRegisterClass::constant_buffer ||
        binding.binding != 0U || binding.space != 0U || binding.array_element != 0U) {
        return buffer;
    }

    context->direct_visibility_marker_ready = false;
    if (!context->visibility_ready || size != sizeof(SceneConstants)) return buffer;

    constexpr std::size_t mode_offset =
        offsetof(SceneConstants, shadow_camera_forward) + sizeof(float) * 3U;
    const float ray_traced_mode = 1.0F;
    if (buffer.write(
            offset + mode_offset,
            std::as_bytes(std::span{&ray_traced_mode, 1})
        )) {
        context->direct_visibility_marker_ready = true;
    }
    return buffer;
}

render::Texture& runtime_directional_shadow_texture_override(
    const render::ShaderBindingKey& binding,
    render::Texture& texture
) noexcept {
    auto* const context = active_directional_ray_shadow_render_context;
    if (context == nullptr || !context->visibility_ready || context->pass == nullptr ||
        context->cascade_shadow != &texture ||
        binding.register_class != render::ShaderRegisterClass::shader_resource ||
        binding.array_element != 0U) {
        return texture;
    }

    const bool gpu_pbr_binding = context->gpu_pbr_visibility_marker_ready &&
        binding.binding == 0U && binding.space == 2U;
    const bool direct_material_binding = context->direct_visibility_marker_ready &&
        binding.space == 0U;
    if (!gpu_pbr_binding && !direct_material_binding) return texture;

    auto* const visibility = context->pass->visibility();
    return visibility == nullptr ? texture : *visibility;
}

} // namespace vice::runtime::detail

namespace vice::runtime {
namespace {

using InstallPackedVirtualGeometryFn = bool(*)(
    void*,
    std::span<const RuntimeVirtualGeometrySubmissionGeometry>,
    RuntimeVirtualGeometryGpuArena&,
    std::string*
);

struct RuntimeVirtualGeometrySceneContext {
    render::Device* device = nullptr;
    CookedMeshCatalog* catalog = nullptr;
    RuntimeGpuMeshletTablePlan* gpu_meshlet_plan = nullptr;
    RuntimeRenderResourceBudgets resource_budgets{};
    platform::WindowMetrics metrics{};
    void* renderer_state = nullptr;
    InstallPackedVirtualGeometryFn install_packed_geometry = nullptr;
};

thread_local RuntimeVirtualGeometrySceneContext* active_virtual_geometry_context = nullptr;

class RuntimeVirtualGeometrySceneScope final {
public:
    explicit RuntimeVirtualGeometrySceneScope(RuntimeVirtualGeometrySceneContext& context) noexcept
        : previous_{active_virtual_geometry_context} {
        active_virtual_geometry_context = &context;
    }

    ~RuntimeVirtualGeometrySceneScope() {
        active_virtual_geometry_context = previous_;
    }

    RuntimeVirtualGeometrySceneScope(const RuntimeVirtualGeometrySceneScope&) = delete;
    RuntimeVirtualGeometrySceneScope& operator=(const RuntimeVirtualGeometrySceneScope&) = delete;

private:
    RuntimeVirtualGeometrySceneContext* previous_ = nullptr;
};

struct RuntimeVirtualGeometryStreamingState {
    explicit RuntimeVirtualGeometryStreamingState(const std::uint64_t budget_bytes)
        : resident_pool{budget_bytes} {}

    render::Device* device = nullptr;
    const CookedMeshCatalog* catalog = nullptr;
    std::array<RuntimeGpuInstanceTable, runtime_gpu_virtual_geometry_feedback_lane_count> models;
    std::array<RuntimeGpuVirtualGeometrySelector, runtime_gpu_virtual_geometry_feedback_lane_count> selectors;
    RuntimeGpuVirtualGeometryFeedbackRing feedback;
    RuntimeVirtualGeometryResidentPool resident_pool;
    RuntimeVirtualGeometryGpuArena arena;
};

thread_local std::unique_ptr<RuntimeVirtualGeometryStreamingState> virtual_geometry_streaming_state;

struct RuntimeDirectionalRayShadowSceneState {
    void* owner = nullptr;
    render::Device* device = nullptr;
    std::unique_ptr<RuntimeDirectionalRayShadowPass> pass;
};

struct RuntimeRayAmbientOcclusionSceneState {
    void* owner = nullptr;
    render::Device* device = nullptr;
    std::unique_ptr<RuntimeRayAmbientOcclusionPass> pass;
};

thread_local RuntimeDirectionalRayShadowSceneState directional_ray_shadow_scene_state;
thread_local RuntimeRayAmbientOcclusionSceneState ray_ambient_occlusion_scene_state;

[[nodiscard]] bool ensure_directional_ray_shadow_pass(
    void* const owner,
    render::Device& device,
    std::string* const diagnostics
) {
    auto& state = directional_ray_shadow_scene_state;
    if (state.owner == owner && state.device == &device && state.pass != nullptr) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    state.pass.reset();
    state.owner = owner;
    state.device = &device;
    state.pass = std::make_unique<RuntimeDirectionalRayShadowPass>();
#ifdef VICE_RUNTIME_DIRECTIONAL_RAY_SHADOW_MANIFEST
    if (!state.pass->initialize(
            device,
            VICE_RUNTIME_DIRECTIONAL_RAY_SHADOW_MANIFEST,
            diagnostics
        )) {
        return false;
    }
#else
    if (!state.pass->initialize(device, {}, diagnostics)) return false;
#endif
    return true;
}

[[nodiscard]] bool ensure_ray_ambient_occlusion_pass(
    void* const owner,
    render::Device& device,
    std::string* const diagnostics
) {
    auto& state = ray_ambient_occlusion_scene_state;
    if (state.owner == owner && state.device == &device && state.pass != nullptr) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    state.pass.reset();
    state.owner = owner;
    state.device = &device;
    state.pass = std::make_unique<RuntimeRayAmbientOcclusionPass>();
#ifdef VICE_RUNTIME_RAY_AMBIENT_OCCLUSION_MANIFEST
    if (!state.pass->initialize(
            device,
            VICE_RUNTIME_RAY_AMBIENT_OCCLUSION_MANIFEST,
            diagnostics
        )) {
        return false;
    }
#else
    if (!state.pass->initialize(device, {}, diagnostics)) return false;
#endif
    return true;
}

[[nodiscard]] world::GlobalPosition runtime_camera_global_position(
    const world::World& world,
    const RuntimeCameraView& camera
) noexcept {
    world::GlobalPosition result{
        static_cast<double>(camera.position.x),
        static_cast<double>(camera.position.y),
        static_cast<double>(camera.position.z),
    };
    if (const auto camera_entity = world.primary_camera(); camera_entity.has_value()) {
        if (const auto spatial = world.spatial_state(*camera_entity); spatial.has_value()) {
            result = spatial->global_position;
        }
    }
    return result;
}

[[nodiscard]] bool ensure_runtime_virtual_geometry_meshlet_alias(
    RuntimeGpuMeshletTablePlan& plan,
    const world::ResourceId resource,
    std::string* const diagnostics
) {
    if (const auto existing = plan.mesh_index(resource); existing.has_value()) {
        if (*existing >= plan.meshes.size() || plan.meshes[*existing].meshlet_count != 0U) {
            set_diagnostics(
                diagnostics,
                "Runtime virtual geometry resource collides with authoritative meshlet metadata."
            );
            return false;
        }
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (plan.mesh_ids.size() != plan.meshes.size() ||
        plan.mesh_ids.size() >= std::numeric_limits<std::uint32_t>::max() ||
        plan.meshlets.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(
            diagnostics,
            "Runtime virtual geometry meshlet alias exceeds the CPU meshlet table contract."
        );
        return false;
    }
    plan.mesh_ids.push_back(resource);
    plan.meshes.push_back(RuntimeGpuMeshRecord{
        .first_meshlet = static_cast<std::uint32_t>(plan.meshlets.size()),
        .meshlet_count = 0U,
    });
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

[[nodiscard]] bool streamable_v7_virtual_geometry(
    const std::span<const RenderMeshInstance> instances,
    const CookedMeshCatalog& catalog
) noexcept {
    for (const auto& instance : instances) {
        const auto* const source = catalog.find(instance.mesh);
        if (source == nullptr) return false;
        if (source->asset.virtual_geometry_proxies.hierarchy.nodes.empty()) continue;
        if (source->runtime_generated || source->path.empty() ||
            source->asset.version < render::mesh_asset_format_version_v7) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ensure_streaming_state(
    render::Device& device,
    const CookedMeshCatalog& catalog,
    const std::uint64_t budget_bytes,
    std::string* const diagnostics
) {
    if (!virtual_geometry_streaming_state ||
        virtual_geometry_streaming_state->device != &device ||
        virtual_geometry_streaming_state->catalog != &catalog) {
        auto next = std::make_unique<RuntimeVirtualGeometryStreamingState>(budget_bytes);
        for (std::size_t lane = 0U; lane < next->models.size(); ++lane) {
            if (!next->models[lane].initialize(device, diagnostics) ||
                !next->selectors[lane].initialize(device, diagnostics)) {
                return false;
            }
        }
        if (!next->feedback.initialize(device, diagnostics) ||
            !next->arena.initialize(device, diagnostics)) {
            return false;
        }
        next->device = &device;
        next->catalog = &catalog;
        virtual_geometry_streaming_state = std::move(next);
    } else if (!virtual_geometry_streaming_state->resident_pool.set_budget(budget_bytes)) {
        virtual_geometry_streaming_state->resident_pool =
            RuntimeVirtualGeometryResidentPool{budget_bytes};
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

[[nodiscard]] bool materialize_cpu_virtual_geometry_scene(
    RuntimeRenderResidentSceneBuildResult& result,
    CookedMeshCatalog& catalog,
    RuntimeGpuMeshletTablePlan& gpu_meshlet_plan,
    const RuntimeVisibilityFrustum& frustum,
    const world::GlobalPosition camera_global_position,
    const RuntimeCameraView& camera,
    const platform::WindowMetrics metrics
) {
    const auto selected = select_runtime_virtual_geometry_instances(
        result.scene.meshes,
        catalog.records(),
        frustum,
        camera_global_position,
        camera.camera.vertical_fov_radians,
        static_cast<float>(metrics.drawable_height)
    );
    if (!selected) {
        result.scene = {};
        result.diagnostics = selected.diagnostics;
        return false;
    }

    auto selected_instances = selected.instances;
    for (auto& instance : selected_instances) {
        if (instance.geometry_node_index == render::virtual_geometry_invalid_node) continue;

        const auto source_mesh = instance.mesh;
        const auto node_index = instance.geometry_node_index;
        const auto variant_id = runtime_virtual_geometry_variant_resource_id(source_mesh, node_index);
        const auto* existing = catalog.find(variant_id);
        if (existing == nullptr) {
            const auto* const source = catalog.find(source_mesh);
            if (source == nullptr || source->runtime_generated) {
                result.scene = {};
                result.diagnostics = "Runtime virtual geometry could not resolve its authored source mesh.";
                return false;
            }
            auto variant = build_runtime_virtual_geometry_variant_asset(source->asset, node_index);
            if (!variant) {
                result.scene = {};
                result.diagnostics = variant.diagnostics;
                return false;
            }
            const auto bytes = variant_id.value.bytes();
            std::copy(bytes.begin(), bytes.end(), variant.asset->stable_id.begin());
            if (!catalog.upsert_runtime_record(CookedMeshRecord{
                    .resource_id = variant_id,
                    .path = {},
                    .asset = std::move(*variant.asset),
                    .runtime_generated = true,
                }, &result.diagnostics)) {
                result.scene = {};
                return false;
            }
        } else if (!existing->runtime_generated) {
            result.scene = {};
            result.diagnostics = "Runtime virtual geometry variant collides with an authored mesh resource.";
            return false;
        }
        if (!ensure_runtime_virtual_geometry_meshlet_alias(
                gpu_meshlet_plan,
                variant_id,
                &result.diagnostics
            )) {
            result.scene = {};
            return false;
        }

        instance.mesh = variant_id;
        instance.geometry_node_index = render::virtual_geometry_invalid_node;
    }
    result.scene.meshes = std::move(selected_instances);
    return true;
}

[[nodiscard]] const render::VirtualGeometryHierarchyNode* leaf_node_for_page(
    const render::MeshAsset& asset,
    const std::uint32_t page_index
) noexcept {
    const auto& hierarchy = asset.virtual_geometry_proxies.hierarchy;
    const auto found = std::find_if(hierarchy.nodes.begin(), hierarchy.nodes.end(), [&](const auto& node) {
        return node.child_count == 0U && node.page_count == 1U && node.page_offset == page_index;
    });
    return found == hierarchy.nodes.end() ? nullptr : &*found;
}

[[nodiscard]] bool build_arena_sources(
    const RuntimeVirtualGeometryResidentPool& resident_pool,
    const std::span<const RuntimeVirtualGeometrySubmissionGeometry> selected_geometry,
    const CookedMeshCatalog& catalog,
    std::vector<RuntimeVirtualGeometryGpuArenaSource>& sources,
    std::string& diagnostics
) {
    sources.clear();
    sources.reserve(resident_pool.pages().size() + selected_geometry.size());
    for (const auto& page : resident_pool.pages()) {
        const auto* const source = catalog.find(page.key.mesh);
        if (source == nullptr || source->runtime_generated) {
            diagnostics = "Runtime packed virtual geometry could not resolve a resident page source mesh.";
            return false;
        }
        const auto* const leaf = leaf_node_for_page(source->asset, page.key.page_index);
        if (leaf == nullptr) {
            diagnostics = "Runtime packed virtual geometry resident page has no canonical leaf node.";
            return false;
        }
        const auto leaf_index = static_cast<std::uint32_t>(leaf -
            source->asset.virtual_geometry_proxies.hierarchy.nodes.data());
        sources.push_back(RuntimeVirtualGeometryGpuArenaSource{
            .resource_id = runtime_virtual_geometry_variant_resource_id(page.key.mesh, leaf_index),
            .vertices = page.geometry.vertices,
            .indices = page.geometry.indices,
        });
    }
    for (const auto& geometry : selected_geometry) {
        if (geometry.page_index.has_value()) continue;
        sources.push_back(RuntimeVirtualGeometryGpuArenaSource{
            .resource_id = geometry.resource_id,
            .vertices = geometry.asset.vertices,
            .indices = geometry.asset.indices,
        });
    }
    std::sort(sources.begin(), sources.end(), [](const auto& left, const auto& right) {
        return std::lexicographical_compare(
            left.resource_id.value.bytes().begin(),
            left.resource_id.value.bytes().end(),
            right.resource_id.value.bytes().begin(),
            right.resource_id.value.bytes().end()
        );
    });
    const auto duplicate = std::adjacent_find(sources.begin(), sources.end(), [](const auto& left, const auto& right) {
        return left.resource_id == right.resource_id;
    });
    if (duplicate != sources.end()) {
        diagnostics = "Runtime packed virtual geometry source set contains a duplicate resource.";
        return false;
    }
    diagnostics.clear();
    return true;
}

} // namespace

bool CookedMeshCatalog::upsert_runtime_record(
    CookedMeshRecord record,
    std::string* const diagnostics
) {
    if (!record.resource_id.valid() || record.asset.vertices.empty() || record.asset.indices.empty()) {
        set_diagnostics(diagnostics, "Runtime-generated mesh record is incomplete.");
        return false;
    }
    record.runtime_generated = true;
    const auto found = std::find_if(records_.begin(), records_.end(), [&](const auto& existing) {
        return existing.resource_id == record.resource_id;
    });
    if (found != records_.end()) {
        if (!found->runtime_generated) {
            set_diagnostics(
                diagnostics,
                "Runtime virtual geometry resource ID collides with an authored cooked mesh `" +
                    record.resource_id.value.to_string() + "`."
            );
            return false;
        }
        *found = std::move(record);
    } else {
        records_.push_back(std::move(record));
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

namespace detail {

RuntimeRenderResidentSceneBuildResult build_runtime_render_resident_scene_with_virtual_geometry(
    const world::World& world
) {
    auto result = ::vice::runtime::build_runtime_render_resident_scene(world);
    if (!result || active_virtual_geometry_context == nullptr ||
        active_virtual_geometry_context->device == nullptr ||
        active_virtual_geometry_context->catalog == nullptr ||
        active_virtual_geometry_context->gpu_meshlet_plan == nullptr ||
        result.scene.meshes.empty()) {
        return result;
    }

    auto& catalog = *active_virtual_geometry_context->catalog;
    auto& gpu_meshlet_plan = *active_virtual_geometry_context->gpu_meshlet_plan;
    auto& device = *active_virtual_geometry_context->device;
    const auto metrics = active_virtual_geometry_context->metrics;
    const float aspect = static_cast<float>(metrics.drawable_width) /
        static_cast<float>(std::max(1U, metrics.drawable_height));
    const auto authored_camera = primary_camera_view(world, aspect);
    const RuntimeCameraView camera = authored_camera.has_value()
        ? *authored_camera
        : fallback_camera(result.scene.meshes, catalog, aspect);
    const auto camera_global_position = runtime_camera_global_position(world, camera);
    const auto frustum = runtime_visibility_frustum(
        camera.forward,
        camera.up,
        camera.camera.vertical_fov_radians,
        camera.aspect_ratio,
        camera.camera.near_plane,
        camera.camera.far_plane
    );
    if (!valid(frustum)) {
        result.scene = {};
        result.diagnostics = "Runtime virtual geometry could not build the camera selection frustum.";
        return result;
    }

    if (!streamable_v7_virtual_geometry(result.scene.meshes, catalog)) {
        (void)materialize_cpu_virtual_geometry_scene(
            result,
            catalog,
            gpu_meshlet_plan,
            frustum,
            camera_global_position,
            camera,
            metrics
        );
        return result;
    }

    const auto gpu_plan = build_runtime_gpu_virtual_geometry_plan(
        result.scene.meshes,
        catalog.records()
    );
    if (!gpu_plan) {
        result.scene = {};
        result.diagnostics = gpu_plan.diagnostics;
        return result;
    }
    if (gpu_plan.plan.candidates.empty()) return result;

    if (!ensure_streaming_state(
            device,
            catalog,
            active_virtual_geometry_context->resource_budgets.geometry_bytes,
            &result.diagnostics
        )) {
        result.scene = {};
        return result;
    }
    auto& streaming = *virtual_geometry_streaming_state;

    auto polled = streaming.feedback.poll(gpu_plan.plan, result.scene.meshes);
    if (!polled) {
        result.scene = {};
        result.diagnostics = polled.diagnostics;
        return result;
    }
    RuntimeGpuVirtualGeometryFeedbackResult render_feedback = polled.feedback.has_value()
        ? std::move(*polled.feedback)
        : build_runtime_gpu_virtual_geometry_root_feedback(gpu_plan.plan);
    if (!render_feedback) {
        result.scene = {};
        result.diagnostics = render_feedback.diagnostics;
        return result;
    }

    auto acquired = streaming.feedback.acquire(gpu_plan.plan, result.scene.meshes);
    if (!acquired) {
        result.scene = {};
        result.diagnostics = acquired.diagnostics;
        return result;
    }
    if (acquired.ticket.has_value()) {
        const auto ticket = *acquired.ticket;
        if (ticket.slot >= streaming.models.size()) {
            streaming.feedback.cancel(ticket);
            result.scene = {};
            result.diagnostics = "Runtime virtual geometry feedback lane exceeds the selection lane count.";
            return result;
        }
        std::vector<math::Mat4> camera_relative_models;
        camera_relative_models.reserve(result.scene.meshes.size());
        for (const auto& instance : result.scene.meshes) {
            camera_relative_models.push_back(
                runtime_model_matrix_relative_to(instance, camera_global_position)
            );
        }
        auto commands = device.create_command_list();
        if (!commands ||
            !streaming.models[ticket.slot].upload(
                *commands,
                camera_relative_models,
                &result.diagnostics
            ) || !streaming.selectors[ticket.slot].select(
                *commands,
                gpu_plan.plan,
                streaming.models[ticket.slot].result(),
                frustum,
                camera.camera.vertical_fov_radians,
                static_cast<float>(metrics.drawable_height),
                runtime_virtual_geometry_default_max_error_pixels,
                &result.diagnostics
            ) || !streaming.feedback.capture(
                *commands,
                streaming.selectors[ticket.slot].result(),
                ticket,
                &result.diagnostics
            )) {
            streaming.feedback.cancel(ticket);
            result.scene = {};
            if (result.diagnostics.empty()) {
                result.diagnostics = "Runtime virtual geometry could not encode asynchronous GPU frontier feedback.";
            }
            return result;
        }
        const auto submission = device.submit(std::move(commands));
        if (submission == render::invalid_submission) {
            streaming.feedback.cancel(ticket);
            result.scene = {};
            result.diagnostics = "Runtime virtual geometry asynchronous GPU frontier submission failed.";
            return result;
        }
        if (!streaming.feedback.commit(ticket, submission, &result.diagnostics)) {
            streaming.feedback.cancel(ticket);
            result.scene = {};
            return result;
        }
    }

    auto streamed = build_runtime_virtual_geometry_submission(
        result.scene.meshes,
        catalog.records(),
        gpu_plan.plan,
        render_feedback,
        streaming.resident_pool
    );
    if (!streamed) {
        result.scene = {};
        result.diagnostics = streamed.diagnostics;
        return result;
    }

    std::vector<RuntimeVirtualGeometryGpuArenaSource> arena_sources;
    if (!build_arena_sources(
            streaming.resident_pool,
            streamed.geometry,
            catalog,
            arena_sources,
            result.diagnostics
        )) {
        result.scene = {};
        return result;
    }
    const auto arena_update = streaming.arena.update(arena_sources);
    if (!arena_update) {
        result.scene = {};
        result.diagnostics = arena_update.diagnostics;
        return result;
    }
    if (active_virtual_geometry_context->install_packed_geometry == nullptr ||
        active_virtual_geometry_context->renderer_state == nullptr ||
        !active_virtual_geometry_context->install_packed_geometry(
            active_virtual_geometry_context->renderer_state,
            streamed.geometry,
            streaming.arena,
            &result.diagnostics
        )) {
        result.scene = {};
        if (result.diagnostics.empty()) {
            result.diagnostics = "Runtime virtual geometry could not install packed GPU arena bindings.";
        }
        return result;
    }

    for (auto& geometry : streamed.geometry) {
        const auto* existing = catalog.find(geometry.resource_id);
        if (existing != nullptr && !existing->runtime_generated) {
            result.scene = {};
            result.diagnostics = "Runtime streamed virtual geometry collides with an authored mesh resource.";
            return result;
        }
        if (!catalog.upsert_runtime_record(CookedMeshRecord{
                .resource_id = geometry.resource_id,
                .path = {},
                .asset = std::move(geometry.asset),
                .runtime_generated = true,
            }, &result.diagnostics) ||
            !ensure_runtime_virtual_geometry_meshlet_alias(
                gpu_meshlet_plan,
                geometry.resource_id,
                &result.diagnostics
            )) {
            result.scene = {};
            return result;
        }
    }

    result.scene.meshes = std::move(streamed.instances);
    return result;
}

} // namespace detail

void RuntimeSceneRenderer::set_directional_shadow_mode(
    const RuntimeDirectionalShadowMode mode
) noexcept {
    directional_shadow_mode_ = mode;
    directional_shadow_telemetry_.requested_mode = mode;
}

RuntimeDirectionalShadowMode RuntimeSceneRenderer::directional_shadow_mode() const noexcept {
    return directional_shadow_mode_;
}

RuntimeDirectionalShadowTelemetry RuntimeSceneRenderer::directional_shadow_telemetry() const noexcept {
    return directional_shadow_telemetry_;
}

bool RuntimeSceneRenderer::render(
    const world::World& world,
    render::Swapchain& swapchain,
    const platform::WindowMetrics metrics,
    const render::Color clear_color,
    std::string* const diagnostics
) {
    directional_shadow_telemetry_.requested_mode = directional_shadow_mode_;
    directional_shadow_telemetry_.active_path = RuntimeDirectionalShadowPath::cascaded;
    directional_shadow_telemetry_.ray_tracing_supported = false;
    directional_shadow_telemetry_.ray_query_visibility_available = false;

    if (state_ == nullptr) {
        return render_legacy(world, swapchain, metrics, clear_color, diagnostics);
    }
    const auto install_packed = [](
        void* const opaque_state,
        const std::span<const RuntimeVirtualGeometrySubmissionGeometry> geometry,
        RuntimeVirtualGeometryGpuArena& arena,
        std::string* const install_diagnostics
    ) -> bool {
        auto* const render_state = static_cast<State*>(opaque_state);
        if (render_state == nullptr) {
            set_diagnostics(install_diagnostics, "Runtime packed virtual geometry renderer state is unavailable.");
            return false;
        }
        for (const auto& item : geometry) {
            const auto binding = arena.find(item.resource_id);
            if (!binding.has_value() || !binding->valid()) {
                set_diagnostics(
                    install_diagnostics,
                    "Runtime packed virtual geometry resource has no arena binding."
                );
                return false;
            }
            auto vertices = std::make_unique<RuntimeVirtualGeometryBufferSlice>(
                *binding->vertex_buffer,
                binding->vertex_offset,
                binding->vertex_size,
                render::BufferUsage::vertex
            );
            auto indices = std::make_unique<RuntimeVirtualGeometryBufferSlice>(
                *binding->index_buffer,
                binding->index_offset,
                binding->index_size,
                render::BufferUsage::index
            );
            const auto bounds = runtime_mesh_bounding_sphere(item.asset.vertices);
            render_state->meshes.erase(item.resource_id);
            render_state->meshes.emplace(
                item.resource_id,
                State::MeshGpu{
                    .vertices = std::move(vertices),
                    .indices = std::move(indices),
                    .index_count = binding->index_count,
                    .bounds = bounds,
                }
            );
        }
        if (install_diagnostics != nullptr) install_diagnostics->clear();
        return true;
    };

    RuntimeVirtualGeometrySceneContext context{
        .device = state_->device,
        .catalog = &state_->catalog,
        .gpu_meshlet_plan = &state_->gpu_meshlet_plan,
        .resource_budgets = state_->resource_budgets,
        .metrics = metrics,
        .renderer_state = state_.get(),
        .install_packed_geometry = install_packed,
    };
    const RuntimeVirtualGeometrySceneScope scope{context};

    const bool ray_tracing_supported = state_->device != nullptr &&
        state_->device->capabilities().ray_tracing;
    directional_shadow_telemetry_.ray_tracing_supported = ray_tracing_supported;

    detail::RuntimeRayAmbientOcclusionRenderContext ambient_occlusion_context{
        .pbr_frame = state_->gpu_pbr_frame.get(),
        .cascade_shadow = state_->shadow_maps[0].get(),
        .diagnostics = diagnostics,
        .enabled = ray_tracing_supported,
    };
    if (metrics.drawable_width != 0U && metrics.drawable_height != 0U &&
        ray_tracing_supported && state_->device != nullptr) {
        if (!ensure_ray_ambient_occlusion_pass(state_.get(), *state_->device, diagnostics)) {
            return false;
        }
        auto& ambient_occlusion_state = ray_ambient_occlusion_scene_state;
        if (ambient_occlusion_state.pass != nullptr && ambient_occlusion_state.pass->available()) {
            if (!ambient_occlusion_state.pass->resize(
                    metrics.drawable_width,
                    metrics.drawable_height,
                    diagnostics
                )) {
                return false;
            }
            ambient_occlusion_context.pass = ambient_occlusion_state.pass.get();
        }
    }
    const detail::RuntimeRayAmbientOcclusionRenderScope ambient_occlusion_scope{
        ambient_occlusion_context
    };

    detail::RuntimeDirectionalRayShadowRenderContext ray_shadow_context{
        .requested_mode = directional_shadow_mode_,
        .telemetry = &directional_shadow_telemetry_,
        .diagnostics = diagnostics,
    };
    const auto requested_path = resolve_runtime_directional_shadow_path(
        directional_shadow_mode_,
        ray_tracing_supported
    );
    if (metrics.drawable_width != 0U && metrics.drawable_height != 0U &&
        requested_path == RuntimeDirectionalShadowPath::ray_traced && state_->device != nullptr) {
        if (!ensure_directional_ray_shadow_pass(state_.get(), *state_->device, diagnostics)) {
            return false;
        }
        auto& ray_shadow_state = directional_ray_shadow_scene_state;
        if (ray_shadow_state.pass != nullptr && ray_shadow_state.pass->available()) {
            if (!ray_shadow_state.pass->resize(
                    metrics.drawable_width,
                    metrics.drawable_height,
                    diagnostics
                )) {
                return false;
            }
            ray_shadow_context.pass = ray_shadow_state.pass.get();
        }
    }
    const detail::RuntimeDirectionalRayShadowRenderScope ray_shadow_scope{ray_shadow_context};

    if (!render_legacy(world, swapchain, metrics, clear_color, diagnostics)) {
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::runtime
