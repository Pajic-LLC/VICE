#include <vice/runtime/directional_ray_shadow.hpp>

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

constexpr render::ShaderBindingKey ray_shadow_constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};

constexpr render::ShaderBindingKey ray_shadow_tlas_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};

constexpr render::ShaderBindingKey ray_shadow_depth_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1U,
    .space = 0U,
    .array_element = 0U,
};

constexpr render::ShaderBindingKey ray_shadow_visibility_binding{
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

[[nodiscard]] std::uint32_t dispatch_groups(
    const std::uint32_t extent,
    const std::uint32_t group_size
) noexcept {
    return group_size == 0U ? 0U : (extent + group_size - 1U) / group_size;
}

} // namespace

float runtime_directional_ray_shadow_linear_depth(
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

std::optional<RuntimeDirectionalRayShadowConstants>
make_runtime_directional_ray_shadow_constants(
    const RuntimeDirectionalRayShadowCamera& camera,
    const math::Vec3 direction_toward_light,
    const std::uint32_t width,
    const std::uint32_t height,
    const float surface_bias
) noexcept {
    if (width == 0U || height == 0U || !finite_vec3(camera.position) ||
        !usable_direction(camera.forward) || !usable_direction(camera.up) ||
        !usable_direction(direction_toward_light) ||
        !std::isfinite(camera.vertical_fov_radians) ||
        !std::isfinite(camera.aspect_ratio) || !std::isfinite(camera.near_plane) ||
        !std::isfinite(camera.far_plane) || !std::isfinite(camera.jitter_x_ndc) ||
        !std::isfinite(camera.jitter_y_ndc) || !std::isfinite(surface_bias) ||
        camera.vertical_fov_radians <= 0.0F || camera.vertical_fov_radians >= 3.13F ||
        camera.aspect_ratio <= 0.0F || camera.near_plane <= 0.0F ||
        camera.far_plane <= camera.near_plane || surface_bias < 0.0F) {
        return std::nullopt;
    }

    const math::Vec3 forward = math::normalize(camera.forward);
    const math::Vec3 up_input = math::normalize(camera.up);
    const math::Vec3 right_raw = math::cross(forward, up_input);
    if (!usable_direction(right_raw)) return std::nullopt;
    const math::Vec3 right = math::normalize(right_raw);
    const math::Vec3 up = math::normalize(math::cross(right, forward));
    const math::Vec3 toward_light = math::normalize(direction_toward_light);
    const float tan_half_fov = std::tan(camera.vertical_fov_radians * 0.5F);
    if (!std::isfinite(tan_half_fov) || tan_half_fov <= 0.0F) return std::nullopt;

    RuntimeDirectionalRayShadowConstants constants{};
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

    constants.light_direction_bias[0] = toward_light.x;
    constants.light_direction_bias[1] = toward_light.y;
    constants.light_direction_bias[2] = toward_light.z;
    constants.light_direction_bias[3] = surface_bias;

    constants.target_size_jitter[0] = static_cast<float>(width);
    constants.target_size_jitter[1] = static_cast<float>(height);
    constants.target_size_jitter[2] = camera.jitter_x_ndc;
    constants.target_size_jitter[3] = camera.jitter_y_ndc;
    return constants;
}

struct RuntimeDirectionalRayShadowPass::State {
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

RuntimeDirectionalRayShadowPass::RuntimeDirectionalRayShadowPass()
    : state_{std::make_unique<State>()} {}
RuntimeDirectionalRayShadowPass::~RuntimeDirectionalRayShadowPass() = default;
RuntimeDirectionalRayShadowPass::RuntimeDirectionalRayShadowPass(
    RuntimeDirectionalRayShadowPass&&
) noexcept = default;
RuntimeDirectionalRayShadowPass& RuntimeDirectionalRayShadowPass::operator=(
    RuntimeDirectionalRayShadowPass&&
) noexcept = default;

bool RuntimeDirectionalRayShadowPass::initialize(
    render::Device& device,
    const std::filesystem::path& shader_manifest,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime directional ray-shadow state is unavailable.");
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
        set_diagnostics(
            diagnostics,
            "VICE Runtime could not load the directional ray-shadow compute shader."
        );
        return false;
    }

    render::ComputePipelineDesc pipeline_desc;
    pipeline_desc.compute_shader = cooked.module.get();
    pipeline_desc.thread_group_size = *cooked.compute_thread_group_size;
    pipeline_desc.debug_name = "VICE Runtime directional ray-shadow visibility";
    auto pipeline = device.create_compute_pipeline(pipeline_desc);

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(RuntimeDirectionalRayShadowConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime directional ray-shadow constants";
    auto constants = device.create_buffer(constants_desc);

    if (!pipeline || !constants) {
        set_diagnostics(
            diagnostics,
            "VICE Runtime could not create directional ray-shadow compute resources."
        );
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

bool RuntimeDirectionalRayShadowPass::resize(
    const std::uint32_t width,
    const std::uint32_t height,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime directional ray-shadow pass is not initialized.");
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
    desc.debug_name = "VICE Runtime directional ray-shadow visibility";
    auto visibility = state_->device->create_texture(desc);
    if (!visibility) {
        set_diagnostics(
            diagnostics,
            "VICE Runtime could not allocate directional ray-shadow visibility."
        );
        return false;
    }

    state_->visibility = std::move(visibility);
    state_->width = width;
    state_->height = height;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeDirectionalRayShadowPass::dispatch(
    render::CommandList& commands,
    render::Texture& camera_depth,
    render::AccelerationStructure& scene_tlas,
    const RuntimeDirectionalRayShadowConstants& constants,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->available || !state_->pipeline || !state_->constants ||
        !state_->visibility || state_->width == 0U || state_->height == 0U ||
        state_->group_size_x == 0U || state_->group_size_y == 0U) {
        set_diagnostics(diagnostics, "Runtime directional ray-shadow pass is not ready.");
        return false;
    }
    if (camera_depth.format() != render::TextureFormat::depth32_float ||
        !render::has_usage(camera_depth.usage(), render::TextureUsage::sampled) ||
        camera_depth.width() != state_->width || camera_depth.height() != state_->height ||
        constants.target_size_jitter[0] != static_cast<float>(state_->width) ||
        constants.target_size_jitter[1] != static_cast<float>(state_->height)) {
        set_diagnostics(diagnostics, "Runtime directional ray-shadow depth source is incompatible.");
        return false;
    }
    if (!state_->constants->write(
            0U,
            std::as_bytes(std::span{&constants, 1})
        ) || !commands.set_compute_pipeline(*state_->pipeline) ||
        !commands.set_compute_constant_buffer(
            ray_shadow_constants_binding,
            *state_->constants,
            0U,
            sizeof(RuntimeDirectionalRayShadowConstants)
        ) || !commands.set_compute_acceleration_structure(
            ray_shadow_tlas_binding,
            scene_tlas
        ) || !commands.set_compute_texture(
            ray_shadow_depth_binding,
            camera_depth
        ) || !commands.set_compute_storage_texture(
            ray_shadow_visibility_binding,
            *state_->visibility
        ) || !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = dispatch_groups(state_->width, state_->group_size_x),
            .group_count_y = dispatch_groups(state_->height, state_->group_size_y),
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode directional ray shadows.");
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeDirectionalRayShadowPass::available() const noexcept {
    return state_ != nullptr && state_->available;
}

render::Texture* RuntimeDirectionalRayShadowPass::visibility() const noexcept {
    return state_ == nullptr ? nullptr : state_->visibility.get();
}

} // namespace vice::runtime