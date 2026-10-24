#include <vice/runtime/ray_ambient_occlusion.hpp>

#include <vice/render/shader_asset.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace vice::runtime {
namespace {

constexpr float background_depth = 0.999999F;
constexpr float two_pi = 6.28318530717958647692F;
constexpr std::uint32_t sample_seed_mask = 0x00FFFFFFU;

constexpr render::ShaderBindingKey ao_constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};

constexpr render::ShaderBindingKey ao_tlas_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};

constexpr render::ShaderBindingKey ao_depth_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1U,
    .space = 0U,
    .array_element = 0U,
};

constexpr render::ShaderBindingKey ao_visibility_binding{
    .register_class = render::ShaderRegisterClass::unordered_access,
    .binding = 0U,
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

[[nodiscard]] constexpr std::uint32_t hash_u32(std::uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] float unit_random(const std::uint32_t value) noexcept {
    return static_cast<float>(hash_u32(value) & sample_seed_mask) / 16777216.0F;
}

} // namespace

float runtime_ray_ambient_occlusion_linear_depth(
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

RuntimeRayAmbientOcclusionNeighbor runtime_ray_ambient_occlusion_preferred_neighbor(
    const float center_device_depth,
    const float negative_device_depth,
    const float positive_device_depth,
    const float near_plane,
    const float far_plane
) noexcept {
    if (!usable_device_depth(center_device_depth)) {
        return RuntimeRayAmbientOcclusionNeighbor::invalid;
    }
    const float center = runtime_ray_ambient_occlusion_linear_depth(
        center_device_depth, near_plane, far_plane
    );
    if (center <= 0.0F) return RuntimeRayAmbientOcclusionNeighbor::invalid;

    const bool negative_valid = usable_device_depth(negative_device_depth);
    const bool positive_valid = usable_device_depth(positive_device_depth);
    if (!negative_valid && !positive_valid) {
        return RuntimeRayAmbientOcclusionNeighbor::invalid;
    }
    if (!negative_valid) return RuntimeRayAmbientOcclusionNeighbor::positive;
    if (!positive_valid) return RuntimeRayAmbientOcclusionNeighbor::negative;

    const float negative = runtime_ray_ambient_occlusion_linear_depth(
        negative_device_depth, near_plane, far_plane
    );
    const float positive = runtime_ray_ambient_occlusion_linear_depth(
        positive_device_depth, near_plane, far_plane
    );
    if (negative <= 0.0F && positive <= 0.0F) {
        return RuntimeRayAmbientOcclusionNeighbor::invalid;
    }
    if (negative <= 0.0F) return RuntimeRayAmbientOcclusionNeighbor::positive;
    if (positive <= 0.0F) return RuntimeRayAmbientOcclusionNeighbor::negative;

    return std::abs(negative - center) <= std::abs(positive - center)
        ? RuntimeRayAmbientOcclusionNeighbor::negative
        : RuntimeRayAmbientOcclusionNeighbor::positive;
}

std::optional<math::Vec3> runtime_ray_ambient_occlusion_world_position(
    const RuntimeRayAmbientOcclusionCamera& camera,
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
    const float axial_depth = runtime_ray_ambient_occlusion_linear_depth(
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

std::optional<math::Vec3> runtime_ray_ambient_occlusion_hemisphere_direction(
    const math::Vec3 normal,
    const std::uint32_t pixel_x,
    const std::uint32_t pixel_y,
    const std::uint32_t sample_seed
) noexcept {
    if (!usable_direction(normal)) return std::nullopt;
    const math::Vec3 n = math::normalize(normal);
    const std::uint32_t seed = (sample_seed & sample_seed_mask) ^
        (pixel_x * 0x9E3779B9U) ^ (pixel_y * 0x85EBCA6BU);
    const float u1 = unit_random(seed ^ 0xA511E9B3U);
    const float u2 = unit_random(seed ^ 0x63D83595U);
    const float radius = std::sqrt(std::clamp(u1, 0.0F, 1.0F));
    const float phi = two_pi * u2;
    const float local_x = radius * std::cos(phi);
    const float local_y = radius * std::sin(phi);
    const float local_z = std::sqrt(std::max(0.0F, 1.0F - u1));

    const math::Vec3 helper = std::abs(n.z) < 0.999F
        ? math::Vec3{0.0F, 0.0F, 1.0F}
        : math::Vec3{1.0F, 0.0F, 0.0F};
    const math::Vec3 tangent_raw = math::cross(helper, n);
    if (!usable_direction(tangent_raw)) return std::nullopt;
    const math::Vec3 tangent = math::normalize(tangent_raw);
    const math::Vec3 bitangent = math::cross(n, tangent);
    const math::Vec3 direction = math::normalize(math::Vec3{
        tangent.x * local_x + bitangent.x * local_y + n.x * local_z,
        tangent.y * local_x + bitangent.y * local_y + n.y * local_z,
        tangent.z * local_x + bitangent.z * local_y + n.z * local_z,
    });
    if (!usable_direction(direction) || math::dot(direction, n) < -1.0e-5F) {
        return std::nullopt;
    }
    return direction;
}

std::optional<RuntimeRayAmbientOcclusionConstants>
make_runtime_ray_ambient_occlusion_constants(
    const RuntimeRayAmbientOcclusionCamera& camera,
    const std::uint32_t width,
    const std::uint32_t height,
    const float radius,
    const float surface_bias,
    const std::uint32_t sample_seed
) noexcept {
    if (width == 0U || height == 0U || !finite_vec3(camera.position) ||
        !usable_direction(camera.forward) || !usable_direction(camera.up) ||
        !std::isfinite(camera.vertical_fov_radians) || !std::isfinite(camera.aspect_ratio) ||
        !std::isfinite(camera.near_plane) || !std::isfinite(camera.far_plane) ||
        !std::isfinite(camera.jitter_x_ndc) || !std::isfinite(camera.jitter_y_ndc) ||
        !std::isfinite(radius) || !std::isfinite(surface_bias) ||
        camera.vertical_fov_radians <= 0.0F || camera.vertical_fov_radians >= 3.13F ||
        camera.aspect_ratio <= 0.0F || camera.near_plane <= 0.0F ||
        camera.far_plane <= camera.near_plane || radius <= 0.0F ||
        radius > camera.far_plane || surface_bias <= 0.0F || surface_bias >= radius) {
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

    RuntimeRayAmbientOcclusionConstants constants{};
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
    constants.ao_parameters[0] = radius;
    constants.ao_parameters[1] = surface_bias;
    constants.ao_parameters[2] = static_cast<float>(sample_seed & sample_seed_mask);
    return constants;
}

struct RuntimeRayAmbientOcclusionPass::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> shader;
    std::unique_ptr<render::ComputePipeline> pipeline;
    std::unique_ptr<render::Buffer> constants;
    std::unique_ptr<render::Texture> visibility;
    std::uint32_t group_size_x = 0U;
    std::uint32_t group_size_y = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    bool available = false;
};

RuntimeRayAmbientOcclusionPass::RuntimeRayAmbientOcclusionPass()
    : state_{std::make_unique<State>()} {}
RuntimeRayAmbientOcclusionPass::~RuntimeRayAmbientOcclusionPass() = default;
RuntimeRayAmbientOcclusionPass::RuntimeRayAmbientOcclusionPass(
    RuntimeRayAmbientOcclusionPass&&
) noexcept = default;
RuntimeRayAmbientOcclusionPass& RuntimeRayAmbientOcclusionPass::operator=(
    RuntimeRayAmbientOcclusionPass&&
) noexcept = default;

bool RuntimeRayAmbientOcclusionPass::initialize(
    render::Device& device,
    const std::filesystem::path& shader_manifest,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime ray ambient-occlusion state is unavailable.");
        return false;
    }
    state_->device = &device;
    state_->shader.reset();
    state_->pipeline.reset();
    state_->constants.reset();
    state_->visibility.reset();
    state_->group_size_x = 0U;
    state_->group_size_y = 0U;
    state_->width = 0U;
    state_->height = 0U;
    state_->available = false;

    if (!device.capabilities().ray_tracing || shader_manifest.empty()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    auto cooked = render::load_cooked_shader(device, shader_manifest);
    if (!cooked || cooked.module == nullptr ||
        cooked.module->stage() != render::ShaderStage::compute ||
        !cooked.compute_thread_group_size.has_value() ||
        cooked.compute_thread_group_size->x == 0U ||
        cooked.compute_thread_group_size->y == 0U ||
        cooked.compute_thread_group_size->z != 1U) {
        set_diagnostics(diagnostics, "VICE Runtime could not load the ray ambient-occlusion compute shader.");
        return false;
    }

    render::ComputePipelineDesc pipeline_desc;
    pipeline_desc.compute_shader = cooked.module.get();
    pipeline_desc.thread_group_size = *cooked.compute_thread_group_size;
    pipeline_desc.debug_name = "VICE Runtime ray ambient-occlusion visibility";
    auto pipeline = device.create_compute_pipeline(pipeline_desc);

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(RuntimeRayAmbientOcclusionConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime ray ambient-occlusion constants";
    auto constants = device.create_buffer(constants_desc);
    if (!pipeline || !constants) {
        set_diagnostics(diagnostics, "VICE Runtime could not create ray ambient-occlusion compute resources.");
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

bool RuntimeRayAmbientOcclusionPass::resize(
    const std::uint32_t width,
    const std::uint32_t height,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime ray ambient-occlusion pass is not initialized.");
        return false;
    }
    if (!state_->available) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (width == 0U || height == 0U) {
        state_->visibility.reset();
        state_->width = 0U;
        state_->height = 0U;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (state_->visibility != nullptr && state_->width == width && state_->height == height) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    render::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = render::TextureFormat::r32_float;
    desc.usage = render::TextureUsage::sampled | render::TextureUsage::storage;
    desc.debug_name = "VICE Runtime ray ambient-occlusion visibility";
    auto visibility = state_->device->create_texture(desc);
    if (!visibility) {
        set_diagnostics(diagnostics, "VICE Runtime could not allocate ray ambient-occlusion visibility.");
        return false;
    }
    state_->visibility = std::move(visibility);
    state_->width = width;
    state_->height = height;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayAmbientOcclusionPass::dispatch(
    render::CommandList& commands,
    render::Texture& camera_depth,
    render::AccelerationStructure& scene_tlas,
    const RuntimeRayAmbientOcclusionConstants& constants,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->available || !state_->pipeline || !state_->constants ||
        !state_->visibility || state_->width == 0U || state_->height == 0U ||
        state_->group_size_x == 0U || state_->group_size_y == 0U) {
        set_diagnostics(diagnostics, "Runtime ray ambient-occlusion pass is not ready.");
        return false;
    }
    if (camera_depth.format() != render::TextureFormat::depth32_float ||
        !render::has_usage(camera_depth.usage(), render::TextureUsage::sampled) ||
        camera_depth.width() != state_->width || camera_depth.height() != state_->height ||
        constants.target_size_jitter[0] != static_cast<float>(state_->width) ||
        constants.target_size_jitter[1] != static_cast<float>(state_->height) ||
        constants.ao_parameters[0] <= 0.0F || constants.ao_parameters[1] <= 0.0F ||
        constants.ao_parameters[1] >= constants.ao_parameters[0]) {
        set_diagnostics(diagnostics, "Runtime ray ambient-occlusion depth source or constants are incompatible.");
        return false;
    }

    if (!state_->constants->write(0U, std::as_bytes(std::span{&constants, 1})) ||
        !commands.set_compute_pipeline(*state_->pipeline) ||
        !commands.set_compute_constant_buffer(
            ao_constants_binding,
            *state_->constants,
            0U,
            sizeof(RuntimeRayAmbientOcclusionConstants)
        ) || !commands.set_compute_acceleration_structure(ao_tlas_binding, scene_tlas) ||
        !commands.set_compute_texture(ao_depth_binding, camera_depth) ||
        !commands.set_compute_storage_texture(ao_visibility_binding, *state_->visibility) ||
        !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = dispatch_groups(state_->width, state_->group_size_x),
            .group_count_y = dispatch_groups(state_->height, state_->group_size_y),
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode ray ambient occlusion.");
        return false;
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayAmbientOcclusionPass::available() const noexcept {
    return state_ != nullptr && state_->available;
}

render::Texture* RuntimeRayAmbientOcclusionPass::visibility() const noexcept {
    return state_ == nullptr ? nullptr : state_->visibility.get();
}

} // namespace vice::runtime
