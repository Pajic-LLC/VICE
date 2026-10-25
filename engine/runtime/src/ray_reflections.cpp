#include <vice/runtime/ray_reflections.hpp>

#include <vice/render/shader_asset.hpp>
#include <vice/runtime/ray_tracing_scene_executor.hpp>
#include <vice/runtime/scene.hpp>

#include "environment_pipeline.hpp"
#include "reflection_surface_guide_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace vice::runtime {
namespace {

constexpr float background_depth = 0.999999F;

constexpr render::ShaderBindingKey reflection_constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey reflection_tlas_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey reflection_depth_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey reflection_guide_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey reflection_environment_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 3U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey reflection_scene_indices_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 4U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey reflection_environment_sampler_binding{
    .register_class = render::ShaderRegisterClass::sampler,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey reflection_radiance_binding{
    .register_class = render::ShaderRegisterClass::unordered_access,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey reflection_hits_binding{
    .register_class = render::ShaderRegisterClass::unordered_access,
    .binding = 1U,
    .space = 0U,
    .array_element = 0U,
};

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] bool finite_vec3(const math::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool usable_direction(const math::Vec3 value) noexcept {
    return finite_vec3(value) && math::dot(value, value) > 1.0e-8F;
}

[[nodiscard]] bool usable_device_depth(const float depth) noexcept {
    return std::isfinite(depth) && depth >= 0.0F && depth < background_depth;
}

[[nodiscard]] std::uint32_t dispatch_groups(
    const std::uint32_t extent,
    const std::uint32_t group_size
) noexcept {
    return group_size == 0U ? 0U : (extent + group_size - 1U) / group_size;
}

} // namespace

float runtime_ray_reflections_linear_depth(
    const float device_depth,
    const float near_plane,
    const float far_plane
) noexcept {
    if (!std::isfinite(device_depth) || !std::isfinite(near_plane) ||
        !std::isfinite(far_plane) || near_plane <= 0.0F || far_plane <= near_plane) {
        return 0.0F;
    }
    const float depth = std::clamp(device_depth, 0.0F, 1.0F);
    const float denominator = far_plane - depth * (far_plane - near_plane);
    if (!std::isfinite(denominator) || denominator <= 0.0F) return 0.0F;
    return (near_plane * far_plane) / denominator;
}

std::optional<math::Vec3> runtime_ray_reflections_world_position(
    const RuntimeRayReflectionsCamera& camera,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t pixel_x,
    const std::uint32_t pixel_y,
    const float device_depth
) noexcept {
    if (width == 0U || height == 0U || pixel_x >= width || pixel_y >= height ||
        !usable_device_depth(device_depth) || !finite_vec3(camera.position) ||
        !usable_direction(camera.forward) || !usable_direction(camera.up) ||
        !std::isfinite(camera.vertical_fov_radians) || !std::isfinite(camera.aspect_ratio) ||
        !std::isfinite(camera.near_plane) || !std::isfinite(camera.far_plane) ||
        !std::isfinite(camera.jitter_x_ndc) || !std::isfinite(camera.jitter_y_ndc) ||
        camera.vertical_fov_radians <= 0.0F || camera.vertical_fov_radians >= 3.13F ||
        camera.aspect_ratio <= 0.0F || camera.near_plane <= 0.0F ||
        camera.far_plane <= camera.near_plane) {
        return std::nullopt;
    }

    const math::Vec3 forward = math::normalize(camera.forward);
    const math::Vec3 up_input = math::normalize(camera.up);
    const math::Vec3 right_raw = math::cross(forward, up_input);
    if (!usable_direction(right_raw)) return std::nullopt;
    const math::Vec3 right = math::normalize(right_raw);
    const math::Vec3 up = math::normalize(math::cross(right, forward));
    const float tan_half_fov = std::tan(camera.vertical_fov_radians * 0.5F);
    const float axial_depth = runtime_ray_reflections_linear_depth(
        device_depth, camera.near_plane, camera.far_plane
    );
    if (!std::isfinite(tan_half_fov) || tan_half_fov <= 0.0F || axial_depth <= 0.0F) {
        return std::nullopt;
    }

    const float uv_x = (static_cast<float>(pixel_x) + 0.5F) / static_cast<float>(width);
    const float uv_y = (static_cast<float>(pixel_y) + 0.5F) / static_cast<float>(height);
    const float ndc_x = uv_x * 2.0F - 1.0F - camera.jitter_x_ndc;
    const float ndc_y = 1.0F - uv_y * 2.0F - camera.jitter_y_ndc;
    const float right_scale = ndc_x * camera.aspect_ratio * tan_half_fov;
    const float up_scale = ndc_y * tan_half_fov;
    const math::Vec3 ray{
        forward.x + right.x * right_scale + up.x * up_scale,
        forward.y + right.y * right_scale + up.y * up_scale,
        forward.z + right.z * right_scale + up.z * up_scale,
    };
    return math::Vec3{
        camera.position.x + ray.x * axial_depth,
        camera.position.y + ray.y * axial_depth,
        camera.position.z + ray.z * axial_depth,
    };
}

std::optional<math::Vec3> runtime_ray_reflections_direction(
    const math::Vec3 surface_position,
    const math::Vec3 camera_position,
    const math::Vec3 world_normal
) noexcept {
    if (!finite_vec3(surface_position) || !finite_vec3(camera_position) ||
        !usable_direction(world_normal)) {
        return std::nullopt;
    }
    const math::Vec3 to_camera{
        camera_position.x - surface_position.x,
        camera_position.y - surface_position.y,
        camera_position.z - surface_position.z,
    };
    if (!usable_direction(to_camera)) return std::nullopt;
    const math::Vec3 view = math::normalize(to_camera);
    const math::Vec3 normal = math::normalize(world_normal);
    const float incident_dot_normal = math::dot(
        math::Vec3{-view.x, -view.y, -view.z}, normal
    );
    const math::Vec3 reflected{
        -view.x - 2.0F * incident_dot_normal * normal.x,
        -view.y - 2.0F * incident_dot_normal * normal.y,
        -view.z - 2.0F * incident_dot_normal * normal.z,
    };
    if (!usable_direction(reflected)) return std::nullopt;
    const math::Vec3 result = math::normalize(reflected);
    if (math::dot(result, normal) <= 1.0e-5F) return std::nullopt;
    return result;
}

bool runtime_ray_reflections_eligible(
    const float roughness,
    const float max_roughness
) noexcept {
    return std::isfinite(roughness) && std::isfinite(max_roughness) &&
        max_roughness >= 0.0F && max_roughness <= 1.0F &&
        roughness >= 0.0F && roughness <= max_roughness;
}

std::optional<RuntimeRayReflectionsConstants> make_runtime_ray_reflections_constants(
    const RuntimeRayReflectionsCamera& camera,
    const std::uint32_t width,
    const std::uint32_t height,
    const float environment_intensity,
    const float environment_rotation_radians,
    const float environment_max_specular_lod,
    const float max_distance,
    const float surface_bias,
    const float max_roughness
) noexcept {
    if (width == 0U || height == 0U || !finite_vec3(camera.position) ||
        !usable_direction(camera.forward) || !usable_direction(camera.up) ||
        !std::isfinite(camera.vertical_fov_radians) || !std::isfinite(camera.aspect_ratio) ||
        !std::isfinite(camera.near_plane) || !std::isfinite(camera.far_plane) ||
        !std::isfinite(camera.jitter_x_ndc) || !std::isfinite(camera.jitter_y_ndc) ||
        !std::isfinite(environment_intensity) || !std::isfinite(environment_rotation_radians) ||
        !std::isfinite(environment_max_specular_lod) || !std::isfinite(max_distance) ||
        !std::isfinite(surface_bias) || !std::isfinite(max_roughness) ||
        camera.vertical_fov_radians <= 0.0F || camera.vertical_fov_radians >= 3.13F ||
        camera.aspect_ratio <= 0.0F || camera.near_plane <= 0.0F ||
        camera.far_plane <= camera.near_plane || environment_intensity < 0.0F ||
        environment_max_specular_lod < 0.0F || max_distance <= 0.0F ||
        max_distance > camera.far_plane || surface_bias <= 0.0F ||
        surface_bias >= max_distance || max_roughness < 0.0F || max_roughness > 1.0F) {
        return std::nullopt;
    }

    const math::Vec3 forward = math::normalize(camera.forward);
    const math::Vec3 up_input = math::normalize(camera.up);
    const math::Vec3 right_raw = math::cross(forward, up_input);
    if (!usable_direction(right_raw)) return std::nullopt;
    const math::Vec3 right = math::normalize(right_raw);
    const math::Vec3 up = math::normalize(math::cross(right, forward));
    const float tan_half_fov = std::tan(camera.vertical_fov_radians * 0.5F);
    if (!std::isfinite(tan_half_fov) || tan_half_fov <= 0.0F) return std::nullopt;

    RuntimeRayReflectionsConstants constants{};
    constants.camera_position_near[0] = camera.position.x;
    constants.camera_position_near[1] = camera.position.y;
    constants.camera_position_near[2] = camera.position.z;
    constants.camera_position_near[3] = camera.near_plane;
    constants.camera_forward_far[0] = forward.x;
    constants.camera_forward_far[1] = forward.y;
    constants.camera_forward_far[2] = forward.z;
    constants.camera_forward_far[3] = camera.far_plane;
    constants.camera_right_tan_half_fov[0] = right.x;
    constants.camera_right_tan_half_fov[1] = right.y;
    constants.camera_right_tan_half_fov[2] = right.z;
    constants.camera_right_tan_half_fov[3] = tan_half_fov;
    constants.camera_up_aspect[0] = up.x;
    constants.camera_up_aspect[1] = up.y;
    constants.camera_up_aspect[2] = up.z;
    constants.camera_up_aspect[3] = camera.aspect_ratio;
    constants.target_size_jitter[0] = static_cast<float>(width);
    constants.target_size_jitter[1] = static_cast<float>(height);
    constants.target_size_jitter[2] = camera.jitter_x_ndc;
    constants.target_size_jitter[3] = camera.jitter_y_ndc;
    constants.trace_parameters[0] = max_distance;
    constants.trace_parameters[1] = surface_bias;
    constants.trace_parameters[2] = max_roughness;
    constants.trace_parameters[3] = static_cast<float>(runtime_ray_reflection_max_instance_id);
    constants.environment_parameters[0] = environment_intensity;
    constants.environment_parameters[1] = environment_rotation_radians;
    constants.environment_parameters[2] = environment_max_specular_lod;
    constants.environment_parameters[3] = 1.0F;
    return constants;
}

struct RuntimeRayReflectionsPass::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> shader;
    std::unique_ptr<render::ComputePipeline> pipeline;
    std::unique_ptr<render::Buffer> constants;
    std::unique_ptr<render::Texture> radiance;
    std::unique_ptr<render::Buffer> hits;
    std::uint32_t group_size_x = 0U;
    std::uint32_t group_size_y = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    bool available = false;
};

RuntimeRayReflectionsPass::RuntimeRayReflectionsPass() : state_{std::make_unique<State>()} {}
RuntimeRayReflectionsPass::~RuntimeRayReflectionsPass() = default;
RuntimeRayReflectionsPass::RuntimeRayReflectionsPass(RuntimeRayReflectionsPass&&) noexcept = default;
RuntimeRayReflectionsPass& RuntimeRayReflectionsPass::operator=(RuntimeRayReflectionsPass&&) noexcept = default;

bool RuntimeRayReflectionsPass::initialize(
    render::Device& device,
    const std::filesystem::path& shader_manifest,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime ray-reflection state is unavailable.");
        return false;
    }
    state_->device = &device;
    state_->shader.reset();
    state_->pipeline.reset();
    state_->constants.reset();
    state_->radiance.reset();
    state_->hits.reset();
    state_->width = 0U;
    state_->height = 0U;
    state_->group_size_x = 0U;
    state_->group_size_y = 0U;
    state_->available = false;

    if (!device.capabilities().ray_tracing || shader_manifest.empty()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    auto cooked = render::load_cooked_shader(device, shader_manifest);
    if (!cooked || cooked.module == nullptr || cooked.module->stage() != render::ShaderStage::compute ||
        !cooked.compute_thread_group_size.has_value() || cooked.compute_thread_group_size->x == 0U ||
        cooked.compute_thread_group_size->y == 0U || cooked.compute_thread_group_size->z != 1U) {
        set_diagnostics(diagnostics, "VICE Runtime could not load the ray-reflection compute shader.");
        return false;
    }

    render::ComputePipelineDesc pipeline_desc;
    pipeline_desc.compute_shader = cooked.module.get();
    pipeline_desc.thread_group_size = *cooked.compute_thread_group_size;
    pipeline_desc.debug_name = "VICE Runtime raw ray reflections";
    auto pipeline = device.create_compute_pipeline(pipeline_desc);

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(RuntimeRayReflectionsConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime ray-reflection constants";
    auto constants = device.create_buffer(constants_desc);
    if (!pipeline || !constants) {
        set_diagnostics(diagnostics, "VICE Runtime could not create ray-reflection compute resources.");
        return false;
    }

    state_->group_size_x = cooked.compute_thread_group_size->x;
    state_->group_size_y = cooked.compute_thread_group_size->y;
    state_->shader = std::move(cooked.module);
    state_->pipeline = std::move(pipeline);
    state_->constants = std::move(constants);
    state_->available = true;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayReflectionsPass::resize(
    const std::uint32_t width,
    const std::uint32_t height,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime ray-reflection pass is not initialized.");
        return false;
    }
    if (!state_->available) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (width == 0U || height == 0U) {
        state_->radiance.reset();
        state_->hits.reset();
        state_->width = 0U;
        state_->height = 0U;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (state_->radiance != nullptr && state_->hits != nullptr &&
        state_->width == width && state_->height == height) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixels > std::numeric_limits<std::uint64_t>::max() / sizeof(RuntimeRayReflectionHit)) {
        set_diagnostics(diagnostics, "Runtime ray-reflection hit buffer size overflows.");
        return false;
    }

    render::TextureDesc radiance_desc;
    radiance_desc.width = width;
    radiance_desc.height = height;
    radiance_desc.format = render::TextureFormat::rgba16_float;
    radiance_desc.usage = render::TextureUsage::sampled | render::TextureUsage::storage;
    radiance_desc.debug_name = "VICE Runtime raw ray-reflection radiance";
    auto radiance = state_->device->create_texture(radiance_desc);

    render::BufferDesc hit_desc;
    hit_desc.size = pixels * sizeof(RuntimeRayReflectionHit);
    hit_desc.usage = render::BufferUsage::storage;
    hit_desc.memory = render::MemoryLocation::gpu_only;
    hit_desc.debug_name = "VICE Runtime ray-reflection committed hits";
    auto hits = state_->device->create_buffer(hit_desc);
    if (!radiance || !hits) {
        set_diagnostics(diagnostics, "VICE Runtime could not allocate raw ray-reflection outputs.");
        return false;
    }

    state_->radiance = std::move(radiance);
    state_->hits = std::move(hits);
    state_->width = width;
    state_->height = height;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayReflectionsPass::dispatch(
    render::CommandList& commands,
    render::Texture& camera_depth,
    render::Texture& surface_guide,
    render::AccelerationStructure& scene_tlas,
    render::Buffer& scene_instance_indices,
    const std::uint32_t scene_instance_index_count,
    render::Texture& environment_specular,
    render::Sampler& environment_sampler,
    const RuntimeRayReflectionsConstants& constants,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->available || state_->pipeline == nullptr ||
        state_->constants == nullptr || state_->radiance == nullptr || state_->hits == nullptr ||
        state_->width == 0U || state_->height == 0U || state_->group_size_x == 0U ||
        state_->group_size_y == 0U) {
        set_diagnostics(diagnostics, "Runtime ray-reflection pass is not ready.");
        return false;
    }

    const std::uint64_t scene_index_bytes =
        static_cast<std::uint64_t>(scene_instance_index_count) * sizeof(std::uint32_t);
    if (camera_depth.format() != render::TextureFormat::depth32_float ||
        surface_guide.format() != render::TextureFormat::rgba16_float ||
        !render::has_usage(camera_depth.usage(), render::TextureUsage::sampled) ||
        !render::has_usage(surface_guide.usage(), render::TextureUsage::sampled) ||
        !render::has_usage(environment_specular.usage(), render::TextureUsage::sampled) ||
        !render::has_usage(scene_instance_indices.usage(), render::BufferUsage::storage) ||
        scene_instance_index_count == 0U || scene_instance_indices.size() < scene_index_bytes ||
        camera_depth.width() != state_->width || camera_depth.height() != state_->height ||
        surface_guide.width() != state_->width || surface_guide.height() != state_->height ||
        constants.target_size_jitter[0] != static_cast<float>(state_->width) ||
        constants.target_size_jitter[1] != static_cast<float>(state_->height) ||
        constants.trace_parameters[0] <= 0.0F || constants.trace_parameters[1] <= 0.0F ||
        constants.trace_parameters[1] >= constants.trace_parameters[0] ||
        constants.trace_parameters[2] < 0.0F || constants.trace_parameters[2] > 1.0F) {
        set_diagnostics(diagnostics, "Runtime ray-reflection inputs or constants are incompatible.");
        return false;
    }

    const render::BufferViewDesc scene_index_view{
        .offset = 0U,
        .size = scene_index_bytes,
        .structure_stride = sizeof(std::uint32_t),
    };
    const render::BufferViewDesc hit_view{
        .offset = 0U,
        .size = state_->hits->size(),
        .structure_stride = sizeof(RuntimeRayReflectionHit),
    };
    if (!state_->constants->write(0U, std::as_bytes(std::span{&constants, 1})) ||
        !commands.set_compute_pipeline(*state_->pipeline) ||
        !commands.set_compute_constant_buffer(
            reflection_constants_binding,
            *state_->constants,
            0U,
            sizeof(RuntimeRayReflectionsConstants)
        ) || !commands.set_compute_acceleration_structure(reflection_tlas_binding, scene_tlas) ||
        !commands.set_compute_texture(reflection_depth_binding, camera_depth) ||
        !commands.set_compute_texture(reflection_guide_binding, surface_guide) ||
        !commands.set_compute_texture(reflection_environment_binding, environment_specular) ||
        !commands.set_compute_buffer(
            reflection_scene_indices_binding,
            scene_instance_indices,
            scene_index_view
        ) ||
        !commands.set_compute_sampler(reflection_environment_sampler_binding, environment_sampler) ||
        !commands.set_compute_storage_texture(reflection_radiance_binding, *state_->radiance) ||
        !commands.set_compute_storage_buffer(reflection_hits_binding, *state_->hits, hit_view) ||
        !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = dispatch_groups(state_->width, state_->group_size_x),
            .group_count_y = dispatch_groups(state_->height, state_->group_size_y),
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode raw ray reflections.");
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayReflectionsPass::available() const noexcept {
    return state_ != nullptr && state_->available;
}

render::Texture* RuntimeRayReflectionsPass::radiance() const noexcept {
    return state_ == nullptr ? nullptr : state_->radiance.get();
}

render::Buffer* RuntimeRayReflectionsPass::hits() const noexcept {
    return state_ == nullptr ? nullptr : state_->hits.get();
}

} // namespace vice::runtime

namespace vice::runtime::detail {
namespace {

struct RuntimeRayReflectionsSceneState {
    const RuntimeSceneRenderer* owner = nullptr;
    render::Device* device = nullptr;
    std::unique_ptr<RuntimeRayReflectionsPass> pass;
    bool valid = false;
};

thread_local RuntimeRayReflectionsSceneState ray_reflections_scene_state;

[[nodiscard]] bool ensure_ray_reflections_pass(
    const RuntimeSceneRenderer* const owner,
    render::Device& device,
    std::string* const diagnostics
) {
    auto& state = ray_reflections_scene_state;
    if (state.owner == owner && state.device == &device && state.pass != nullptr) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    state.pass.reset();
    state.owner = owner;
    state.device = &device;
    state.valid = false;
    state.pass = std::make_unique<RuntimeRayReflectionsPass>();

    bool pass_initialized = false;
#ifdef VICE_RUNTIME_RAY_REFLECTIONS_MANIFEST
    pass_initialized = state.pass->initialize(
        device,
        VICE_RUNTIME_RAY_REFLECTIONS_MANIFEST,
        diagnostics
    );
#else
    pass_initialized = state.pass->initialize(device, {}, diagnostics);
#endif
    if (!pass_initialized) {
        state.pass.reset();
        return false;
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace

bool runtime_ray_reflections_after_surface_guide(
    const RuntimeSceneRenderer* const owner,
    render::Device& device,
    render::CommandList& commands,
    render::Texture* const current_depth,
    render::Texture* const surface_guide,
    RuntimeRayTracingSceneExecutor* const ray_scene,
    const RuntimeCameraView& camera,
    const float jitter_x_ndc,
    const float jitter_y_ndc,
    const platform::WindowMetrics metrics,
    const RuntimeEnvironmentBinding environment,
    std::string* const diagnostics
) noexcept {
    auto& scene_state = ray_reflections_scene_state;
    if (scene_state.owner == owner) scene_state.valid = false;

    if (!device.capabilities().ray_tracing || owner == nullptr || current_depth == nullptr ||
        surface_guide == nullptr || ray_scene == nullptr || !environment.available ||
        environment.specular == nullptr || environment.sampler == nullptr ||
        metrics.drawable_width == 0U || metrics.drawable_height == 0U) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    auto* const tlas = ray_scene->active_tlas();
    if (tlas == nullptr) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (!ensure_ray_reflections_pass(owner, device, diagnostics)) return false;
    auto& state = ray_reflections_scene_state;
    if (state.pass == nullptr || !state.pass->available()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (!state.pass->resize(metrics.drawable_width, metrics.drawable_height, diagnostics)) return false;

    const auto active_scene_indices = ray_scene->active_scene_instance_indices();
    auto* const scene_index_buffer = ray_scene->active_scene_instance_index_buffer();
    if (active_scene_indices.empty() || scene_index_buffer == nullptr ||
        active_scene_indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(
            diagnostics,
            "Runtime ray reflections require the committed active TLAS GPU scene-index mapping."
        );
        return false;
    }

    const RuntimeRayReflectionsCamera ray_camera{
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
    const float max_distance = std::min(
        runtime_ray_reflection_default_max_distance,
        camera.camera.far_plane
    );
    const auto constants = make_runtime_ray_reflections_constants(
        ray_camera,
        metrics.drawable_width,
        metrics.drawable_height,
        environment.intensity,
        environment.rotation_radians,
        environment.max_specular_lod,
        max_distance
    );
    if (!constants.has_value()) {
        set_diagnostics(diagnostics, "VICE Runtime could not build ray-reflection camera constants.");
        return false;
    }
    const bool dispatched = state.pass->dispatch(
        commands,
        *current_depth,
        *surface_guide,
        *tlas,
        *scene_index_buffer,
        static_cast<std::uint32_t>(active_scene_indices.size()),
        *environment.specular,
        *environment.sampler,
        *constants,
        diagnostics
    );
    state.valid = dispatched;
    return dispatched;
}

render::Texture* runtime_ray_reflection_radiance(const RuntimeSceneRenderer* const owner) noexcept {
    const auto& state = ray_reflections_scene_state;
    if (owner == nullptr || state.owner != owner || state.pass == nullptr || !state.valid) {
        return nullptr;
    }
    return state.pass->radiance();
}

render::Buffer* runtime_ray_reflection_hits(const RuntimeSceneRenderer* const owner) noexcept {
    const auto& state = ray_reflections_scene_state;
    if (owner == nullptr || state.owner != owner || state.pass == nullptr || !state.valid) {
        return nullptr;
    }
    return state.pass->hits();
}

} // namespace vice::runtime::detail

namespace vice::runtime {

render::Texture* RuntimeSceneRenderer::ray_reflection_radiance() const noexcept {
    return detail::runtime_ray_reflection_radiance(this);
}

render::Buffer* RuntimeSceneRenderer::ray_reflection_hits() const noexcept {
    return detail::runtime_ray_reflection_hits(this);
}

} // namespace vice::runtime
