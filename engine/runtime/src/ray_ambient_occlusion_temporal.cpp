#include <vice/runtime/ray_ambient_occlusion_temporal.hpp>

#include <vice/render/shader_asset.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace vice::runtime {
namespace {

constexpr float background_depth = 0.999999F;
constexpr std::size_t invalid_history_index = 2U;

constexpr render::ShaderBindingKey temporal_constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey temporal_current_visibility_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey temporal_history_visibility_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey temporal_motion_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey temporal_current_depth_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 3U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey temporal_previous_depth_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 4U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey temporal_output_binding{
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

float runtime_ray_ambient_occlusion_temporal_history_weight(
    const float base_history_weight,
    const float velocity_x,
    const float velocity_y,
    const float motion_rejection,
    const bool history_valid
) noexcept {
    if (!history_valid || !std::isfinite(base_history_weight) ||
        !std::isfinite(velocity_x) || !std::isfinite(velocity_y) ||
        !std::isfinite(motion_rejection) || base_history_weight < 0.0F ||
        base_history_weight > 1.0F || motion_rejection < 0.0F) {
        return 0.0F;
    }
    const float speed = std::sqrt(velocity_x * velocity_x + velocity_y * velocity_y);
    const float motion_factor = std::clamp(1.0F - speed * motion_rejection, 0.0F, 1.0F);
    return std::clamp(base_history_weight * motion_factor, 0.0F, 1.0F);
}

bool runtime_ray_ambient_occlusion_temporal_depth_accept(
    const float current_depth,
    const float previous_depth,
    const float depth_threshold
) noexcept {
    if (!usable_depth(current_depth) || !usable_depth(previous_depth) ||
        !std::isfinite(depth_threshold) || depth_threshold <= 0.0F) {
        return false;
    }
    const float relative_threshold = std::max(
        depth_threshold,
        depth_threshold * 8.0F * std::max(std::abs(current_depth), std::abs(previous_depth))
    );
    return std::abs(current_depth - previous_depth) <= relative_threshold;
}

float runtime_ray_ambient_occlusion_temporal_clamp_history(
    const float history_visibility,
    const float neighborhood_min,
    const float neighborhood_max
) noexcept {
    if (!std::isfinite(history_visibility) || !std::isfinite(neighborhood_min) ||
        !std::isfinite(neighborhood_max) || neighborhood_min > neighborhood_max) {
        return 1.0F;
    }
    return std::clamp(history_visibility, neighborhood_min, neighborhood_max);
}

std::optional<RuntimeRayAmbientOcclusionTemporalConstants>
make_runtime_ray_ambient_occlusion_temporal_constants(
    const std::uint32_t width,
    const std::uint32_t height,
    const bool history_valid,
    const float base_history_weight,
    const float depth_threshold,
    const float motion_rejection
) noexcept {
    if (width == 0U || height == 0U ||
        !std::isfinite(base_history_weight) || !std::isfinite(depth_threshold) ||
        !std::isfinite(motion_rejection) || base_history_weight < 0.0F ||
        base_history_weight > 1.0F || depth_threshold <= 0.0F || motion_rejection < 0.0F) {
        return std::nullopt;
    }

    RuntimeRayAmbientOcclusionTemporalConstants constants{};
    constants.target_size_history[0] = static_cast<float>(width);
    constants.target_size_history[1] = static_cast<float>(height);
    constants.target_size_history[2] = history_valid ? 1.0F : 0.0F;
    constants.target_size_history[3] = base_history_weight;
    constants.rejection_parameters[0] = depth_threshold;
    constants.rejection_parameters[1] = motion_rejection;
    return constants;
}

struct RuntimeRayAmbientOcclusionTemporalPass::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> shader;
    std::unique_ptr<render::ComputePipeline> pipeline;
    std::unique_ptr<render::Buffer> constants;
    std::array<std::unique_ptr<render::Texture>, 2U> history;
    std::uint32_t group_size_x = 0U;
    std::uint32_t group_size_y = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::size_t last_output_index = invalid_history_index;
    bool available = false;
};

RuntimeRayAmbientOcclusionTemporalPass::RuntimeRayAmbientOcclusionTemporalPass()
    : state_{std::make_unique<State>()} {}
RuntimeRayAmbientOcclusionTemporalPass::~RuntimeRayAmbientOcclusionTemporalPass() = default;
RuntimeRayAmbientOcclusionTemporalPass::RuntimeRayAmbientOcclusionTemporalPass(
    RuntimeRayAmbientOcclusionTemporalPass&&
) noexcept = default;
RuntimeRayAmbientOcclusionTemporalPass& RuntimeRayAmbientOcclusionTemporalPass::operator=(
    RuntimeRayAmbientOcclusionTemporalPass&&
) noexcept = default;

bool RuntimeRayAmbientOcclusionTemporalPass::initialize(
    render::Device& device,
    const std::filesystem::path& shader_manifest,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime RTAO temporal state is unavailable.");
        return false;
    }
    state_->device = &device;
    state_->shader.reset();
    state_->pipeline.reset();
    state_->constants.reset();
    for (auto& texture : state_->history) texture.reset();
    state_->group_size_x = 0U;
    state_->group_size_y = 0U;
    state_->width = 0U;
    state_->height = 0U;
    state_->last_output_index = invalid_history_index;
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
        set_diagnostics(diagnostics, "VICE Runtime could not load the RTAO temporal compute shader.");
        return false;
    }

    render::ComputePipelineDesc pipeline_desc;
    pipeline_desc.compute_shader = cooked.module.get();
    pipeline_desc.thread_group_size = *cooked.compute_thread_group_size;
    pipeline_desc.debug_name = "VICE Runtime RTAO temporal resolve";
    auto pipeline = device.create_compute_pipeline(pipeline_desc);

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(RuntimeRayAmbientOcclusionTemporalConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime RTAO temporal constants";
    auto constants = device.create_buffer(constants_desc);
    if (!pipeline || !constants) {
        set_diagnostics(diagnostics, "Runtime could not create RTAO temporal compute resources.");
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

bool RuntimeRayAmbientOcclusionTemporalPass::resize(
    const std::uint32_t width,
    const std::uint32_t height,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime RTAO temporal pass is not initialized.");
        return false;
    }
    if (!state_->available) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (width == 0U || height == 0U) {
        for (auto& texture : state_->history) texture.reset();
        state_->width = 0U;
        state_->height = 0U;
        state_->last_output_index = invalid_history_index;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (state_->history[0] != nullptr && state_->history[1] != nullptr &&
        state_->width == width && state_->height == height) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    std::array<std::unique_ptr<render::Texture>, 2U> history;
    for (std::size_t index = 0U; index < history.size(); ++index) {
        render::TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = render::TextureFormat::r32_float;
        desc.usage = render::TextureUsage::sampled | render::TextureUsage::storage;
        desc.debug_name = index == 0U
            ? "VICE Runtime RTAO temporal history 0"
            : "VICE Runtime RTAO temporal history 1";
        history[index] = state_->device->create_texture(desc);
        if (!history[index]) {
            set_diagnostics(diagnostics, "Runtime could not allocate RTAO temporal history.");
            return false;
        }
    }

    state_->history = std::move(history);
    state_->width = width;
    state_->height = height;
    state_->last_output_index = invalid_history_index;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayAmbientOcclusionTemporalPass::dispatch(
    render::CommandList& commands,
    render::Texture& current_visibility,
    render::Texture& motion_vectors,
    render::Texture& current_depth,
    render::Texture& previous_depth,
    const RuntimeRayAmbientOcclusionTemporalConstants& constants,
    const std::uint64_t frame_index,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->available || !state_->pipeline || !state_->constants ||
        !state_->history[0] || !state_->history[1] || state_->width == 0U || state_->height == 0U ||
        state_->group_size_x == 0U || state_->group_size_y == 0U) {
        set_diagnostics(diagnostics, "Runtime RTAO temporal pass is not ready.");
        return false;
    }
    if (current_visibility.format() != render::TextureFormat::r32_float ||
        !render::has_usage(current_visibility.usage(), render::TextureUsage::sampled) ||
        motion_vectors.format() != render::TextureFormat::rgba16_float ||
        !render::has_usage(motion_vectors.usage(), render::TextureUsage::sampled) ||
        current_depth.format() != render::TextureFormat::depth32_float ||
        previous_depth.format() != render::TextureFormat::depth32_float ||
        !render::has_usage(current_depth.usage(), render::TextureUsage::sampled) ||
        !render::has_usage(previous_depth.usage(), render::TextureUsage::sampled) ||
        current_visibility.width() != state_->width || current_visibility.height() != state_->height ||
        motion_vectors.width() != state_->width || motion_vectors.height() != state_->height ||
        current_depth.width() != state_->width || current_depth.height() != state_->height ||
        previous_depth.width() != state_->width || previous_depth.height() != state_->height ||
        constants.target_size_history[0] != static_cast<float>(state_->width) ||
        constants.target_size_history[1] != static_cast<float>(state_->height) ||
        constants.target_size_history[2] < 0.0F || constants.target_size_history[2] > 1.0F ||
        constants.target_size_history[3] < 0.0F || constants.target_size_history[3] > 1.0F ||
        constants.rejection_parameters[0] <= 0.0F || constants.rejection_parameters[1] < 0.0F) {
        set_diagnostics(diagnostics, "Runtime RTAO temporal inputs or constants are incompatible.");
        return false;
    }

    const std::size_t output_index = static_cast<std::size_t>(frame_index & 1ULL);
    const std::size_t history_index = 1U - output_index;
    if (!state_->constants->write(0U, std::as_bytes(std::span{&constants, 1})) ||
        !commands.set_compute_pipeline(*state_->pipeline) ||
        !commands.set_compute_constant_buffer(
            temporal_constants_binding,
            *state_->constants,
            0U,
            sizeof(RuntimeRayAmbientOcclusionTemporalConstants)
        ) || !commands.set_compute_texture(temporal_current_visibility_binding, current_visibility) ||
        !commands.set_compute_texture(
            temporal_history_visibility_binding,
            *state_->history[history_index]
        ) || !commands.set_compute_texture(temporal_motion_binding, motion_vectors) ||
        !commands.set_compute_texture(temporal_current_depth_binding, current_depth) ||
        !commands.set_compute_texture(temporal_previous_depth_binding, previous_depth) ||
        !commands.set_compute_storage_texture(temporal_output_binding, *state_->history[output_index]) ||
        !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = dispatch_groups(state_->width, state_->group_size_x),
            .group_count_y = dispatch_groups(state_->height, state_->group_size_y),
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "Runtime could not encode RTAO temporal resolve.");
        return false;
    }

    state_->last_output_index = output_index;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayAmbientOcclusionTemporalPass::available() const noexcept {
    return state_ != nullptr && state_->available;
}

render::Texture* RuntimeRayAmbientOcclusionTemporalPass::visibility() const noexcept {
    if (state_ == nullptr || state_->last_output_index >= state_->history.size()) return nullptr;
    return state_->history[state_->last_output_index].get();
}

} // namespace vice::runtime
