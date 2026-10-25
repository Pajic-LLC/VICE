#include <vice/runtime/ray_reflection_history.hpp>

#include <vice/render/shader_asset.hpp>
#include <vice/runtime/ray_reflections.hpp>

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

constexpr render::ShaderBindingKey history_constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey history_hits_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey history_color_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey history_depth_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey history_radiance_binding{
    .register_class = render::ShaderRegisterClass::unordered_access,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] bool finite_matrix(const math::Mat4& matrix) noexcept {
    return std::all_of(matrix.values.begin(), matrix.values.end(), [](const float value) {
        return std::isfinite(value);
    });
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

std::optional<RuntimeRayReflectionHistoryProjection>
runtime_ray_reflection_history_projection(
    const math::Mat4& previous_view_projection,
    const math::Vec3 world_position
) noexcept {
    if (!finite_matrix(previous_view_projection) ||
        !std::isfinite(world_position.x) || !std::isfinite(world_position.y) ||
        !std::isfinite(world_position.z)) {
        return std::nullopt;
    }

    const float clip_x = previous_view_projection(0U, 0U) * world_position.x +
        previous_view_projection(0U, 1U) * world_position.y +
        previous_view_projection(0U, 2U) * world_position.z +
        previous_view_projection(0U, 3U);
    const float clip_y = previous_view_projection(1U, 0U) * world_position.x +
        previous_view_projection(1U, 1U) * world_position.y +
        previous_view_projection(1U, 2U) * world_position.z +
        previous_view_projection(1U, 3U);
    const float clip_z = previous_view_projection(2U, 0U) * world_position.x +
        previous_view_projection(2U, 1U) * world_position.y +
        previous_view_projection(2U, 2U) * world_position.z +
        previous_view_projection(2U, 3U);
    const float clip_w = previous_view_projection(3U, 0U) * world_position.x +
        previous_view_projection(3U, 1U) * world_position.y +
        previous_view_projection(3U, 2U) * world_position.z +
        previous_view_projection(3U, 3U);
    if (!std::isfinite(clip_x) || !std::isfinite(clip_y) || !std::isfinite(clip_z) ||
        !std::isfinite(clip_w) || clip_w <= 1.0e-6F) {
        return std::nullopt;
    }

    const float ndc_x = clip_x / clip_w;
    const float ndc_y = clip_y / clip_w;
    const float ndc_z = clip_z / clip_w;
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) || !usable_depth(ndc_z) ||
        ndc_x < -1.0F || ndc_x > 1.0F || ndc_y < -1.0F || ndc_y > 1.0F) {
        return std::nullopt;
    }

    return RuntimeRayReflectionHistoryProjection{
        .u = ndc_x * 0.5F + 0.5F,
        .v = ndc_y * -0.5F + 0.5F,
        .device_depth = ndc_z,
    };
}

bool runtime_ray_reflection_history_depth_matches(
    const float expected_device_depth,
    const float sampled_device_depth,
    const float depth_threshold
) noexcept {
    if (!usable_depth(expected_device_depth) || !usable_depth(sampled_device_depth) ||
        !std::isfinite(depth_threshold) || depth_threshold <= 0.0F) {
        return false;
    }
    const float relative_threshold = std::max(
        depth_threshold,
        depth_threshold * 8.0F *
            std::max(std::abs(expected_device_depth), std::abs(sampled_device_depth))
    );
    return std::abs(expected_device_depth - sampled_device_depth) <= relative_threshold;
}

std::optional<RuntimeRayReflectionHistoryConstants>
make_runtime_ray_reflection_history_constants(
    const math::Mat4& previous_view_projection,
    const std::uint32_t width,
    const std::uint32_t height,
    const bool history_valid,
    const float depth_threshold
) noexcept {
    if (width == 0U || height == 0U || !finite_matrix(previous_view_projection) ||
        !std::isfinite(depth_threshold) || depth_threshold <= 0.0F) {
        return std::nullopt;
    }

    RuntimeRayReflectionHistoryConstants constants{};
    constants.previous_view_projection = previous_view_projection;
    constants.target_size_history[0] = static_cast<float>(width);
    constants.target_size_history[1] = static_cast<float>(height);
    constants.target_size_history[2] = history_valid ? 1.0F : 0.0F;
    constants.target_size_history[3] = depth_threshold;
    return constants;
}

struct RuntimeRayReflectionHistoryPass::State {
    std::unique_ptr<render::ShaderModule> shader;
    std::unique_ptr<render::ComputePipeline> pipeline;
    std::unique_ptr<render::Buffer> constants;
    std::uint32_t group_size_x = 0U;
    std::uint32_t group_size_y = 0U;
    bool available = false;
};

RuntimeRayReflectionHistoryPass::RuntimeRayReflectionHistoryPass()
    : state_{std::make_unique<State>()} {}
RuntimeRayReflectionHistoryPass::~RuntimeRayReflectionHistoryPass() = default;
RuntimeRayReflectionHistoryPass::RuntimeRayReflectionHistoryPass(
    RuntimeRayReflectionHistoryPass&&
) noexcept = default;
RuntimeRayReflectionHistoryPass& RuntimeRayReflectionHistoryPass::operator=(
    RuntimeRayReflectionHistoryPass&&
) noexcept = default;

bool RuntimeRayReflectionHistoryPass::initialize(
    render::Device& device,
    const std::filesystem::path& shader_manifest,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime reflection-history state is unavailable.");
        return false;
    }
    state_->shader.reset();
    state_->pipeline.reset();
    state_->constants.reset();
    state_->group_size_x = 0U;
    state_->group_size_y = 0U;
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
            "VICE Runtime could not load the reflection-history compute shader."
        );
        return false;
    }

    render::ComputePipelineDesc pipeline_desc;
    pipeline_desc.compute_shader = cooked.module.get();
    pipeline_desc.thread_group_size = *cooked.compute_thread_group_size;
    pipeline_desc.debug_name = "VICE Runtime reflection history resolve";
    auto pipeline = device.create_compute_pipeline(pipeline_desc);

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(RuntimeRayReflectionHistoryConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime reflection-history constants";
    auto constants = device.create_buffer(constants_desc);
    if (!pipeline || !constants) {
        set_diagnostics(
            diagnostics,
            "VICE Runtime could not create reflection-history compute resources."
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

bool RuntimeRayReflectionHistoryPass::dispatch(
    render::CommandList& commands,
    render::Texture& reflection_radiance,
    render::Buffer& reflection_hits,
    render::Texture& previous_scene_color,
    render::Texture& previous_depth,
    const RuntimeRayReflectionHistoryConstants& constants,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->available || !state_->pipeline || !state_->constants ||
        state_->group_size_x == 0U || state_->group_size_y == 0U) {
        set_diagnostics(diagnostics, "Runtime reflection-history pass is not ready.");
        return false;
    }

    const auto width = static_cast<std::uint32_t>(constants.target_size_history[0]);
    const auto height = static_cast<std::uint32_t>(constants.target_size_history[1]);
    const std::uint64_t pixel_count = static_cast<std::uint64_t>(width) * height;
    const std::uint64_t required_hit_bytes = pixel_count * sizeof(RuntimeRayReflectionHit);
    if (width == 0U || height == 0U ||
        constants.target_size_history[0] != static_cast<float>(width) ||
        constants.target_size_history[1] != static_cast<float>(height) ||
        constants.target_size_history[2] < 0.0F || constants.target_size_history[2] > 1.0F ||
        constants.target_size_history[3] <= 0.0F ||
        reflection_radiance.format() != render::TextureFormat::rgba16_float ||
        previous_scene_color.format() != render::TextureFormat::rgba16_float ||
        previous_depth.format() != render::TextureFormat::depth32_float ||
        !render::has_usage(reflection_radiance.usage(), render::TextureUsage::storage) ||
        !render::has_usage(previous_scene_color.usage(), render::TextureUsage::sampled) ||
        !render::has_usage(previous_depth.usage(), render::TextureUsage::sampled) ||
        !render::has_usage(reflection_hits.usage(), render::BufferUsage::storage) ||
        reflection_radiance.width() != width || reflection_radiance.height() != height ||
        previous_scene_color.width() != width || previous_scene_color.height() != height ||
        previous_depth.width() != width || previous_depth.height() != height ||
        reflection_hits.size() < required_hit_bytes) {
        set_diagnostics(
            diagnostics,
            "Runtime reflection-history inputs or constants are incompatible."
        );
        return false;
    }

    const render::BufferViewDesc hit_view{
        .offset = 0U,
        .size = required_hit_bytes,
        .structure_stride = sizeof(RuntimeRayReflectionHit),
    };
    if (!state_->constants->write(0U, std::as_bytes(std::span{&constants, 1})) ||
        !commands.set_compute_pipeline(*state_->pipeline) ||
        !commands.set_compute_constant_buffer(
            history_constants_binding,
            *state_->constants,
            0U,
            sizeof(RuntimeRayReflectionHistoryConstants)
        ) || !commands.set_compute_buffer(history_hits_binding, reflection_hits, hit_view) ||
        !commands.set_compute_texture(history_color_binding, previous_scene_color) ||
        !commands.set_compute_texture(history_depth_binding, previous_depth) ||
        !commands.set_compute_storage_texture(history_radiance_binding, reflection_radiance) ||
        !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = dispatch_groups(width, state_->group_size_x),
            .group_count_y = dispatch_groups(height, state_->group_size_y),
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode reflection-history resolve.");
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeRayReflectionHistoryPass::available() const noexcept {
    return state_ != nullptr && state_->available;
}

} // namespace vice::runtime
