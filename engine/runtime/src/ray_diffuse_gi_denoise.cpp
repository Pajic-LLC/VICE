#include <vice/runtime/ray_diffuse_gi_denoise.hpp>

#include <vice/render/shader_asset.hpp>
#include <vice/runtime/ray_reflections.hpp>

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

constexpr render::ShaderBindingKey constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey radiance_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey depth_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey guide_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey output_binding{
    .register_class = render::ShaderRegisterClass::unordered_access,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] std::uint32_t dispatch_groups(
    const std::uint32_t extent,
    const std::uint32_t group_size
) noexcept {
    return group_size == 0U ? 0U : (extent + group_size - 1U) / group_size;
}

[[nodiscard]] bool usable_depth(const float depth) noexcept {
    return std::isfinite(depth) && depth >= 0.0F && depth < background_depth;
}

} // namespace

float runtime_ray_diffuse_gi_bilateral_weight(
    const float center_device_depth,
    const float neighbor_device_depth,
    const float normal_alignment,
    const float pixel_distance_squared,
    const float near_plane,
    const float far_plane,
    const float depth_sigma,
    const float normal_power,
    const float spatial_sigma
) noexcept {
    if (!usable_depth(center_device_depth) || !usable_depth(neighbor_device_depth) ||
        !std::isfinite(normal_alignment) || !std::isfinite(pixel_distance_squared) ||
        pixel_distance_squared < 0.0F || !std::isfinite(near_plane) ||
        !std::isfinite(far_plane) || near_plane <= 0.0F || far_plane <= near_plane ||
        !std::isfinite(depth_sigma) || depth_sigma <= 0.0F ||
        !std::isfinite(normal_power) || normal_power < 1.0F ||
        !std::isfinite(spatial_sigma) || spatial_sigma <= 0.0F) {
        return 0.0F;
    }

    const float center = runtime_ray_reflections_linear_depth(
        center_device_depth, near_plane, far_plane
    );
    const float neighbor = runtime_ray_reflections_linear_depth(
        neighbor_device_depth, near_plane, far_plane
    );
    if (center <= 0.0F || neighbor <= 0.0F) return 0.0F;

    const float relative_depth = std::abs(neighbor - center) /
        std::max(center, std::max(near_plane, 1.0e-5F));
    const float depth_ratio = relative_depth / depth_sigma;
    const float spatial_ratio = pixel_distance_squared / (spatial_sigma * spatial_sigma);
    const float geometry_weight = std::exp(
        -0.5F * (depth_ratio * depth_ratio + spatial_ratio)
    );
    const float normal_weight = std::pow(
        std::clamp(normal_alignment, 0.0F, 1.0F), normal_power
    );
    const float weight = geometry_weight * normal_weight;
    return std::isfinite(weight) ? std::clamp(weight, 0.0F, 1.0F) : 0.0F;
}

std::optional<RuntimeRayDiffuseGiDenoiseConstants>
make_runtime_ray_diffuse_gi_denoise_constants(
    const std::uint32_t width,
    const std::uint32_t height,
    const float near_plane,
    const float far_plane,
    const float depth_sigma,
    const float normal_power,
    const float spatial_sigma,
    const std::uint32_t kernel_radius
) noexcept {
    if (width == 0U || height == 0U || !std::isfinite(near_plane) ||
        !std::isfinite(far_plane) || near_plane <= 0.0F || far_plane <= near_plane ||
        !std::isfinite(depth_sigma) || depth_sigma <= 0.0F ||
        !std::isfinite(normal_power) || normal_power < 1.0F ||
        !std::isfinite(spatial_sigma) || spatial_sigma <= 0.0F ||
        kernel_radius == 0U || kernel_radius > 4U) {
        return std::nullopt;
    }

    RuntimeRayDiffuseGiDenoiseConstants constants{};
    constants.target_size_near_far[0] = static_cast<float>(width);
    constants.target_size_near_far[1] = static_cast<float>(height);
    constants.target_size_near_far[2] = near_plane;
    constants.target_size_near_far[3] = far_plane;
    constants.filter_parameters[0] = depth_sigma;
    constants.filter_parameters[1] = normal_power;
    constants.filter_parameters[2] = spatial_sigma;
    constants.filter_parameters[3] = static_cast<float>(kernel_radius);
    return constants;
}

struct RuntimeRayDiffuseGiDenoisePass::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> shader;
    std::unique_ptr<render::ComputePipeline> pipeline;
    std::unique_ptr<render::Buffer> constants;
    std::unique_ptr<render::Texture> radiance;
    std::uint32_t group_size_x = 0U;
    std::uint32_t group_size_y = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    bool available = false;
};

RuntimeRayDiffuseGiDenoisePass::RuntimeRayDiffuseGiDenoisePass()
    : state_{std::make_unique<State>()} {}
RuntimeRayDiffuseGiDenoisePass::~RuntimeRayDiffuseGiDenoisePass() = default;
RuntimeRayDiffuseGiDenoisePass::RuntimeRayDiffuseGiDenoisePass(
    RuntimeRayDiffuseGiDenoisePass&&
) noexcept = default;
RuntimeRayDiffuseGiDenoisePass& RuntimeRayDiffuseGiDenoisePass::operator=(
    RuntimeRayDiffuseGiDenoisePass&&
) noexcept = default;

bool RuntimeRayDiffuseGiDenoisePass::initialize(
    render::Device& device,
    const std::filesystem::path& shader_manifest,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI denoise state is unavailable.");
        return false;
    }
    state_->device = &device;
    state_->shader.reset();
    state_->pipeline.reset();
    state_->constants.reset();
    state_->radiance.reset();
    state_->group_size_x = 0U;
    state_->group_size_y = 0U;
    state_->width = 0U;
    state_->height = 0U;
    state_->available = false;

    if (shader_manifest.empty()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    auto cooked = render::load_cooked_shader(device, shader_manifest);
    if (!cooked || cooked.module == nullptr || cooked.module->stage() != render::ShaderStage::compute ||
        !cooked.compute_thread_group_size.has_value() ||
        cooked.compute_thread_group_size->x == 0U ||
        cooked.compute_thread_group_size->y == 0U ||
        cooked.compute_thread_group_size->z != 1U) {
        set_diagnostics(diagnostics, "VICE Runtime could not load the diffuse-GI denoise shader.");
        return false;
    }

    render::ComputePipelineDesc pipeline_desc;
    pipeline_desc.compute_shader = cooked.module.get();
    pipeline_desc.thread_group_size = *cooked.compute_thread_group_size;
    pipeline_desc.debug_name = "VICE Runtime diffuse GI spatial reconstruction";
    auto pipeline = device.create_compute_pipeline(pipeline_desc);

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(RuntimeRayDiffuseGiDenoiseConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime diffuse GI denoise constants";
    auto constants = device.create_buffer(constants_desc);
    if (!pipeline || !constants) {
        set_diagnostics(diagnostics, "Runtime could not create diffuse-GI denoise resources.");
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

bool RuntimeRayDiffuseGiDenoisePass::resize(
    const std::uint32_t width,
    const std::uint32_t height,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI denoise pass is not initialized.");
        return false;
    }
    if (!state_->available) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (width == 0U || height == 0U) {
        state_->radiance.reset();
        state_->width = 0U;
        state_->height = 0U;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (state_->radiance != nullptr && state_->width == width && state_->height == height) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    render::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = render::TextureFormat::rgba16_float;
    desc.usage = render::TextureUsage::sampled | render::TextureUsage::storage;
    desc.debug_name = "VICE Runtime diffuse GI spatial radiance";
    auto radiance = state_->device->create_texture(desc);
    if (!radiance) {
        set_diagnostics(diagnostics, "Runtime could not allocate diffuse-GI spatial radiance.");
        return false;
    }

    state_->radiance = std::move(radiance);
    state_->width = width;
    state_->height = height;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayDiffuseGiDenoisePass::dispatch(
    render::CommandList& commands,
    render::Texture& radiance,
    render::Texture& camera_depth,
    render::Texture& surface_guide,
    const RuntimeRayDiffuseGiDenoiseConstants& constants,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->available || !state_->pipeline || !state_->constants ||
        !state_->radiance || state_->width == 0U || state_->height == 0U ||
        state_->group_size_x == 0U || state_->group_size_y == 0U) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI denoise pass is not ready.");
        return false;
    }
    if (radiance.format() != render::TextureFormat::rgba16_float ||
        surface_guide.format() != render::TextureFormat::rgba16_float ||
        camera_depth.format() != render::TextureFormat::depth32_float ||
        !render::has_usage(radiance.usage(), render::TextureUsage::sampled) ||
        !render::has_usage(surface_guide.usage(), render::TextureUsage::sampled) ||
        !render::has_usage(camera_depth.usage(), render::TextureUsage::sampled) ||
        radiance.width() != state_->width || radiance.height() != state_->height ||
        surface_guide.width() != state_->width || surface_guide.height() != state_->height ||
        camera_depth.width() != state_->width || camera_depth.height() != state_->height ||
        constants.target_size_near_far[0] != static_cast<float>(state_->width) ||
        constants.target_size_near_far[1] != static_cast<float>(state_->height) ||
        constants.target_size_near_far[2] <= 0.0F ||
        constants.target_size_near_far[3] <= constants.target_size_near_far[2] ||
        constants.filter_parameters[0] <= 0.0F || constants.filter_parameters[1] < 1.0F ||
        constants.filter_parameters[2] <= 0.0F || constants.filter_parameters[3] < 1.0F ||
        constants.filter_parameters[3] > 4.0F) {
        set_diagnostics(diagnostics, "Runtime diffuse-GI denoise inputs are incompatible.");
        return false;
    }

    if (!state_->constants->write(0U, std::as_bytes(std::span{&constants, 1})) ||
        !commands.set_compute_pipeline(*state_->pipeline) ||
        !commands.set_compute_constant_buffer(
            constants_binding, *state_->constants, 0U, sizeof(RuntimeRayDiffuseGiDenoiseConstants)
        ) || !commands.set_compute_texture(radiance_binding, radiance) ||
        !commands.set_compute_texture(depth_binding, camera_depth) ||
        !commands.set_compute_texture(guide_binding, surface_guide) ||
        !commands.set_compute_storage_texture(output_binding, *state_->radiance) ||
        !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = dispatch_groups(state_->width, state_->group_size_x),
            .group_count_y = dispatch_groups(state_->height, state_->group_size_y),
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "Runtime could not encode diffuse-GI spatial reconstruction.");
        return false;
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayDiffuseGiDenoisePass::available() const noexcept {
    return state_ != nullptr && state_->available;
}

render::Texture* RuntimeRayDiffuseGiDenoisePass::radiance() const noexcept {
    return state_ == nullptr ? nullptr : state_->radiance.get();
}

} // namespace vice::runtime
