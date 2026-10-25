#include <vice/runtime/ray_diffuse_gi.hpp>

#include <vice/render/shader_asset.hpp>

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

constexpr float pi = 3.14159265358979323846F;

constexpr render::ShaderBindingKey gi_constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey gi_tlas_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey gi_depth_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey gi_guide_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey gi_environment_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 3U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey gi_scene_indices_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 4U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey gi_environment_sampler_binding{
    .register_class = render::ShaderRegisterClass::sampler,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey gi_radiance_binding{
    .register_class = render::ShaderRegisterClass::unordered_access,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey gi_hits_binding{
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

[[nodiscard]] std::uint32_t hash_u32(std::uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] float hash_unit(const std::uint32_t value) noexcept {
    return static_cast<float>(hash_u32(value) & 0x00FFFFFFU) / 16777216.0F;
}

[[nodiscard]] std::uint32_t dispatch_groups(
    const std::uint32_t extent,
    const std::uint32_t group_size
) noexcept {
    return group_size == 0U ? 0U : (extent + group_size - 1U) / group_size;
}

} // namespace

std::optional<math::Vec3> runtime_ray_diffuse_gi_direction(
    const math::Vec3 world_normal,
    const std::uint32_t pixel_x,
    const std::uint32_t pixel_y,
    const std::uint32_t frame_index
) noexcept {
    if (!usable_direction(world_normal)) return std::nullopt;

    const math::Vec3 normal = math::normalize(world_normal);
    const std::uint32_t sample_frame = frame_index % runtime_ray_diffuse_gi_sample_period;
    const std::uint32_t pixel_seed = hash_u32(pixel_x ^ hash_u32(pixel_y + 0x9E3779B9U));
    const std::uint32_t seed = hash_u32(pixel_seed ^ hash_u32(sample_frame + 0x85EBCA6BU));
    const float u1 = hash_unit(seed ^ 0xA511E9B3U);
    const float u2 = hash_unit(seed ^ 0x63D83595U);

    const float radius = std::sqrt(std::clamp(u1, 0.0F, 1.0F));
    const float phi = 2.0F * pi * u2;
    const float local_x = radius * std::cos(phi);
    const float local_y = radius * std::sin(phi);
    const float local_z = std::sqrt(std::max(1.0F - u1, 0.0F));

    const math::Vec3 helper = std::abs(normal.z) < 0.999F
        ? math::Vec3{0.0F, 0.0F, 1.0F}
        : math::Vec3{0.0F, 1.0F, 0.0F};
    const math::Vec3 tangent_raw = math::cross(helper, normal);
    if (!usable_direction(tangent_raw)) return std::nullopt;
    const math::Vec3 tangent = math::normalize(tangent_raw);
    const math::Vec3 bitangent = math::cross(normal, tangent);
    const math::Vec3 direction{
        tangent.x * local_x + bitangent.x * local_y + normal.x * local_z,
        tangent.y * local_x + bitangent.y * local_y + normal.y * local_z,
        tangent.z * local_x + bitangent.z * local_y + normal.z * local_z,
    };
    if (!usable_direction(direction)) return std::nullopt;
    const math::Vec3 result = math::normalize(direction);
    if (math::dot(result, normal) <= 0.0F) return std::nullopt;
    return result;
}

std::optional<RuntimeRayDiffuseGiConstants> make_runtime_ray_diffuse_gi_constants(
    const RuntimeRayDiffuseGiCamera& camera,
    const std::uint32_t width,
    const std::uint32_t height,
    const float environment_intensity,
    const float environment_rotation_radians,
    const std::uint32_t frame_index,
    const float max_distance,
    const float surface_bias
) noexcept {
    if (width == 0U || height == 0U || !finite_vec3(camera.position) ||
        !usable_direction(camera.forward) || !usable_direction(camera.up) ||
        !std::isfinite(camera.vertical_fov_radians) || !std::isfinite(camera.aspect_ratio) ||
        !std::isfinite(camera.near_plane) || !std::isfinite(camera.far_plane) ||
        !std::isfinite(camera.jitter_x_ndc) || !std::isfinite(camera.jitter_y_ndc) ||
        !std::isfinite(environment_intensity) || !std::isfinite(environment_rotation_radians) ||
        !std::isfinite(max_distance) || !std::isfinite(surface_bias) ||
        camera.vertical_fov_radians <= 0.0F || camera.vertical_fov_radians >= 3.13F ||
        camera.aspect_ratio <= 0.0F || camera.near_plane <= 0.0F ||
        camera.far_plane <= camera.near_plane || environment_intensity < 0.0F ||
        max_distance <= 0.0F || max_distance > camera.far_plane || surface_bias <= 0.0F ||
        surface_bias >= max_distance) {
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

    RuntimeRayDiffuseGiConstants constants{};
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
    constants.trace_parameters[2] = static_cast<float>(frame_index % runtime_ray_diffuse_gi_sample_period);
    constants.trace_parameters[3] = static_cast<float>(runtime_ray_diffuse_gi_max_instance_id);
    constants.environment_parameters[0] = environment_intensity;
    constants.environment_parameters[1] = environment_rotation_radians;
    constants.environment_parameters[2] = 0.0F;
    constants.environment_parameters[3] = 1.0F;
    return constants;
}

struct RuntimeRayDiffuseGiPass::State {
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

RuntimeRayDiffuseGiPass::RuntimeRayDiffuseGiPass() : state_{std::make_unique<State>()} {}
RuntimeRayDiffuseGiPass::~RuntimeRayDiffuseGiPass() = default;
RuntimeRayDiffuseGiPass::RuntimeRayDiffuseGiPass(RuntimeRayDiffuseGiPass&&) noexcept = default;
RuntimeRayDiffuseGiPass& RuntimeRayDiffuseGiPass::operator=(RuntimeRayDiffuseGiPass&&) noexcept = default;

bool RuntimeRayDiffuseGiPass::initialize(
    render::Device& device,
    const std::filesystem::path& shader_manifest,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI state is unavailable.");
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
        set_diagnostics(diagnostics, "VICE Runtime could not load the diffuse-GI compute shader.");
        return false;
    }

    render::ComputePipelineDesc pipeline_desc;
    pipeline_desc.compute_shader = cooked.module.get();
    pipeline_desc.thread_group_size = *cooked.compute_thread_group_size;
    pipeline_desc.debug_name = "VICE Runtime raw diffuse GI";
    auto pipeline = device.create_compute_pipeline(pipeline_desc);

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(RuntimeRayDiffuseGiConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime diffuse-GI constants";
    auto constants = device.create_buffer(constants_desc);
    if (!pipeline || !constants) {
        set_diagnostics(diagnostics, "VICE Runtime could not create diffuse-GI compute resources.");
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

bool RuntimeRayDiffuseGiPass::resize(
    const std::uint32_t width,
    const std::uint32_t height,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI pass is not initialized.");
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
    if (pixels > std::numeric_limits<std::uint64_t>::max() / sizeof(RuntimeRayDiffuseGiHit)) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI hit buffer size overflows.");
        return false;
    }

    render::TextureDesc radiance_desc;
    radiance_desc.width = width;
    radiance_desc.height = height;
    radiance_desc.format = render::TextureFormat::rgba16_float;
    radiance_desc.usage = render::TextureUsage::sampled | render::TextureUsage::storage;
    radiance_desc.debug_name = "VICE Runtime raw diffuse-GI radiance";
    auto radiance = state_->device->create_texture(radiance_desc);

    render::BufferDesc hit_desc;
    hit_desc.size = pixels * sizeof(RuntimeRayDiffuseGiHit);
    hit_desc.usage = render::BufferUsage::storage;
    hit_desc.memory = render::MemoryLocation::gpu_only;
    hit_desc.debug_name = "VICE Runtime diffuse-GI committed hits";
    auto hits = state_->device->create_buffer(hit_desc);
    if (!radiance || !hits) {
        set_diagnostics(diagnostics, "VICE Runtime could not allocate raw diffuse-GI outputs.");
        return false;
    }

    state_->radiance = std::move(radiance);
    state_->hits = std::move(hits);
    state_->width = width;
    state_->height = height;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayDiffuseGiPass::dispatch(
    render::CommandList& commands,
    render::Texture& camera_depth,
    render::Texture& surface_guide,
    render::AccelerationStructure& scene_tlas,
    render::Buffer& scene_instance_indices,
    const std::uint32_t scene_instance_index_count,
    render::Texture& environment_specular,
    render::Sampler& environment_sampler,
    const RuntimeRayDiffuseGiConstants& constants,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->available || state_->pipeline == nullptr ||
        state_->constants == nullptr || state_->radiance == nullptr || state_->hits == nullptr ||
        state_->width == 0U || state_->height == 0U || state_->group_size_x == 0U ||
        state_->group_size_y == 0U) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI pass is not ready.");
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
        constants.trace_parameters[2] < 0.0F ||
        constants.trace_parameters[2] >= static_cast<float>(runtime_ray_diffuse_gi_sample_period)) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI inputs or constants are incompatible.");
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
        .structure_stride = sizeof(RuntimeRayDiffuseGiHit),
    };
    if (!state_->constants->write(0U, std::as_bytes(std::span{&constants, 1})) ||
        !commands.set_compute_pipeline(*state_->pipeline) ||
        !commands.set_compute_constant_buffer(
            gi_constants_binding,
            *state_->constants,
            0U,
            sizeof(RuntimeRayDiffuseGiConstants)
        ) || !commands.set_compute_acceleration_structure(gi_tlas_binding, scene_tlas) ||
        !commands.set_compute_texture(gi_depth_binding, camera_depth) ||
        !commands.set_compute_texture(gi_guide_binding, surface_guide) ||
        !commands.set_compute_texture(gi_environment_binding, environment_specular) ||
        !commands.set_compute_buffer(
            gi_scene_indices_binding,
            scene_instance_indices,
            scene_index_view
        ) ||
        !commands.set_compute_sampler(gi_environment_sampler_binding, environment_sampler) ||
        !commands.set_compute_storage_texture(gi_radiance_binding, *state_->radiance) ||
        !commands.set_compute_storage_buffer(gi_hits_binding, *state_->hits, hit_view) ||
        !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = dispatch_groups(state_->width, state_->group_size_x),
            .group_count_y = dispatch_groups(state_->height, state_->group_size_y),
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode raw diffuse GI.");
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayDiffuseGiPass::available() const noexcept {
    return state_ != nullptr && state_->available;
}

render::Texture* RuntimeRayDiffuseGiPass::radiance() const noexcept {
    return state_ == nullptr ? nullptr : state_->radiance.get();
}

render::Buffer* RuntimeRayDiffuseGiPass::hits() const noexcept {
    return state_ == nullptr ? nullptr : state_->hits.get();
}

} // namespace vice::runtime
