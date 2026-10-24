#include <vice/runtime/ray_ambient_occlusion_denoise.hpp>

#include <vice/render/shader_asset.hpp>
#include <vice/runtime/ray_ambient_occlusion.hpp>

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

constexpr render::ShaderBindingKey denoise_constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};

constexpr render::ShaderBindingKey denoise_visibility_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};

constexpr render::ShaderBindingKey denoise_depth_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1U,
    .space = 0U,
    .array_element = 0U,
};

constexpr render::ShaderBindingKey denoise_output_binding{
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

float runtime_ray_ambient_occlusion_bilateral_weight(
    const float center_device_depth,
    const float neighbor_device_depth,
    const float pixel_distance_squared,
    const float near_plane,
    const float far_plane,
    const float depth_sigma,
    const float spatial_sigma
) noexcept {
    if (!usable_depth(center_device_depth) || !usable_depth(neighbor_device_depth) ||
        !std::isfinite(pixel_distance_squared) || pixel_distance_squared < 0.0F ||
        !std::isfinite(depth_sigma) || depth_sigma <= 0.0F ||
        !std::isfinite(spatial_sigma) || spatial_sigma <= 0.0F) {
        return 0.0F;
    }
    const float center = runtime_ray_ambient_occlusion_linear_depth(
        center_device_depth, near_plane, far_plane
    );
    const float neighbor = runtime_ray_ambient_occlusion_linear_depth(
        neighbor_device_depth, near_plane, far_plane
    );
    if (center <= 0.0F || neighbor <= 0.0F) return 0.0F;

    const float relative_depth = std::abs(neighbor - center) /
        std::max(center, std::max(near_plane, 1.0e-5F));
    const float depth_ratio = relative_depth / depth_sigma;
    const float spatial_ratio = pixel_distance_squared / (spatial_sigma * spatial_sigma);
    const float weight = std::exp(-0.5F * (depth_ratio * depth_ratio + spatial_ratio));
    return std::isfinite(weight) ? std::clamp(weight, 0.0F, 1.0F) : 0.0F;
}

std::optional<RuntimeRayAmbientOcclusionDenoiseConstants>
make_runtime_ray_ambient_occlusion_denoise_constants(
    const std::uint32_t width,
    const std::uint32_t height,
    const float near_plane,
    const float far_plane,
    const float depth_sigma,
    const float spatial_sigma,
    const std::uint32_t kernel_radius
) noexcept {
    if (width == 0U || height == 0U ||
        !std::isfinite(near_plane) || !std::isfinite(far_plane) ||
        !std::isfinite(depth_sigma) || !std::isfinite(spatial_sigma) ||
        near_plane <= 0.0F || far_plane <= near_plane ||
        depth_sigma <= 0.0F || spatial_sigma <= 0.0F ||
        kernel_radius == 0U || kernel_radius > 4U) {
        return std::nullopt;
    }

    RuntimeRayAmbientOcclusionDenoiseConstants constants{};
    constants.target_size_near_far[0] = static_cast<float>(width);
    constants.target_size_near_far[1] = static_cast<float>(height);
    constants.target_size_near_far[2] = near_plane;
    constants.target_size_near_far[3] = far_plane;
    constants.filter_parameters[0] = depth_sigma;
    constants.filter_parameters[1] = spatial_sigma;
    constants.filter_parameters[2] = static_cast<float>(kernel_radius);
    return constants;
}

struct RuntimeRayAmbientOcclusionDenoisePass::State {
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

RuntimeRayAmbientOcclusionDenoisePass::RuntimeRayAmbientOcclusionDenoisePass()
    : state_{std::make_unique<State>()} {}
RuntimeRayAmbientOcclusionDenoisePass::~RuntimeRayAmbientOcclusionDenoisePass() = default;
RuntimeRayAmbientOcclusionDenoisePass::RuntimeRayAmbientOcclusionDenoisePass(
    RuntimeRayAmbientOcclusionDenoisePass&&
) noexcept = default;
RuntimeRayAmbientOcclusionDenoisePass& RuntimeRayAmbientOcclusionDenoisePass::operator=(
    RuntimeRayAmbientOcclusionDenoisePass&&
) noexcept = default;

bool RuntimeRayAmbientOcclusionDenoisePass::initialize(
    render::Device& device,
    const std::filesystem::path& shader_manifest,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime RTAO denoise state is unavailable.");
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

    if (shader_manifest.empty()) {
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
        set_diagnostics(diagnostics, "VICE Runtime could not load the RTAO denoise compute shader.");
        return false;
    }

    render::ComputePipelineDesc pipeline_desc;
    pipeline_desc.compute_shader = cooked.module.get();
    pipeline_desc.thread_group_size = *cooked.compute_thread_group_size;
    pipeline_desc.debug_name = "VICE Runtime RTAO spatial denoise";
    auto pipeline = device.create_compute_pipeline(pipeline_desc);

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(RuntimeRayAmbientOcclusionDenoiseConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime RTAO denoise constants";
    auto constants = device.create_buffer(constants_desc);
    if (!pipeline || !constants) {
        set_diagnostics(diagnostics, "Runtime could not create RTAO denoise compute resources.");
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

bool RuntimeRayAmbientOcclusionDenoisePass::resize(
    const std::uint32_t width,
    const std::uint32_t height,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime RTAO denoise pass is not initialized.");
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
    desc.debug_name = "VICE Runtime RTAO filtered visibility";
    auto visibility = state_->device->create_texture(desc);
    if (!visibility) {
        set_diagnostics(diagnostics, "Runtime could not allocate filtered RTAO visibility.");
        return false;
    }
    state_->visibility = std::move(visibility);
    state_->width = width;
    state_->height = height;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayAmbientOcclusionDenoisePass::dispatch(
    render::CommandList& commands,
    render::Texture& raw_visibility,
    render::Texture& camera_depth,
    const RuntimeRayAmbientOcclusionDenoiseConstants& constants,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->available || !state_->pipeline || !state_->constants ||
        !state_->visibility || state_->width == 0U || state_->height == 0U ||
        state_->group_size_x == 0U || state_->group_size_y == 0U) {
        set_diagnostics(diagnostics, "Runtime RTAO denoise pass is not ready.");
        return false;
    }
    if (raw_visibility.format() != render::TextureFormat::r32_float ||
        !render::has_usage(raw_visibility.usage(), render::TextureUsage::sampled) ||
        raw_visibility.width() != state_->width || raw_visibility.height() != state_->height ||
        camera_depth.format() != render::TextureFormat::depth32_float ||
        !render::has_usage(camera_depth.usage(), render::TextureUsage::sampled) ||
        camera_depth.width() != state_->width || camera_depth.height() != state_->height ||
        constants.target_size_near_far[0] != static_cast<float>(state_->width) ||
        constants.target_size_near_far[1] != static_cast<float>(state_->height) ||
        constants.target_size_near_far[2] <= 0.0F ||
        constants.target_size_near_far[3] <= constants.target_size_near_far[2] ||
        constants.filter_parameters[0] <= 0.0F || constants.filter_parameters[1] <= 0.0F ||
        constants.filter_parameters[2] < 1.0F || constants.filter_parameters[2] > 4.0F) {
        set_diagnostics(diagnostics, "Runtime RTAO denoise inputs or constants are incompatible.");
        return false;
    }

    if (!state_->constants->write(0U, std::as_bytes(std::span{&constants, 1})) ||
        !commands.set_compute_pipeline(*state_->pipeline) ||
        !commands.set_compute_constant_buffer(
            denoise_constants_binding,
            *state_->constants,
            0U,
            sizeof(RuntimeRayAmbientOcclusionDenoiseConstants)
        ) || !commands.set_compute_texture(denoise_visibility_binding, raw_visibility) ||
        !commands.set_compute_texture(denoise_depth_binding, camera_depth) ||
        !commands.set_compute_storage_texture(denoise_output_binding, *state_->visibility) ||
        !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = dispatch_groups(state_->width, state_->group_size_x),
            .group_count_y = dispatch_groups(state_->height, state_->group_size_y),
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "Runtime could not encode RTAO spatial denoising.");
        return false;
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayAmbientOcclusionDenoisePass::available() const noexcept {
    return state_ != nullptr && state_->available;
}

render::Texture* RuntimeRayAmbientOcclusionDenoisePass::visibility() const noexcept {
    return state_ == nullptr ? nullptr : state_->visibility.get();
}

} // namespace vice::runtime
