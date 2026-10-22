#include <vice/runtime/hiz_occlusion.hpp>

#include "hiz_occlusion_shaders.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

constexpr std::uint32_t occlusion_group_size = 64U;

struct HiZOcclusionConstants {
    math::Mat4 view_projection{};
    float projection_data[4]{};
    float occlusion_data[4]{};
    std::uint32_t visibility_data[4]{};
    std::uint32_t viewport_data[4]{};
};

struct HiZOcclusionSphere {
    float center_radius[4]{};
};

static_assert(sizeof(HiZOcclusionConstants) == 128U);
static_assert(sizeof(HiZOcclusionSphere) == 16U);

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] std::uint32_t grown_capacity(const std::uint32_t required) noexcept {
    if (required <= 1U) return std::max(required, 1U);
    std::uint32_t capacity = 1U;
    while (capacity < required && capacity <= std::numeric_limits<std::uint32_t>::max() / 2U) {
        capacity *= 2U;
    }
    return std::max(capacity, required);
}

[[nodiscard]] float projection_scale(const math::Mat4& matrix, const std::size_t row) noexcept {
    const float x = matrix(row, 0U);
    const float y = matrix(row, 1U);
    const float z = matrix(row, 2U);
    return std::sqrt(x * x + y * y + z * z);
}

[[nodiscard]] bool finite_matrix(const math::Mat4& matrix) noexcept {
    return std::all_of(matrix.values.begin(), matrix.values.end(), [](const float value) {
        return std::isfinite(value);
    });
}

} // namespace

struct RuntimeHiZOcclusionCuller::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> shader;
    std::unique_ptr<render::ComputePipeline> pipeline;
    std::unique_ptr<render::Sampler> sampler;
    std::unique_ptr<render::Buffer> constants;
    std::unique_ptr<render::Buffer> zero_upload;
    std::unique_ptr<render::Buffer> sphere_upload;
    std::unique_ptr<render::Buffer> sphere_gpu;
    std::unique_ptr<render::Buffer> visible_count;
    std::unique_ptr<render::Buffer> visible_indices;
    std::uint32_t allocated_capacity = 0U;
    std::uint32_t active_capacity = 0U;

    [[nodiscard]] bool ensure_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (required == 0U) {
            active_capacity = 0U;
            return true;
        }
        if (required <= allocated_capacity && sphere_upload && sphere_gpu && visible_indices) {
            active_capacity = required;
            return true;
        }
        if (device == nullptr) return false;

        const std::uint32_t new_capacity = grown_capacity(required);
        const std::uint64_t sphere_bytes =
            static_cast<std::uint64_t>(new_capacity) * sizeof(HiZOcclusionSphere);
        const std::uint64_t index_bytes =
            static_cast<std::uint64_t>(new_capacity) * sizeof(std::uint32_t);

        render::BufferDesc sphere_upload_desc;
        sphere_upload_desc.size = sphere_bytes;
        sphere_upload_desc.usage = render::BufferUsage::copy_source;
        sphere_upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
        sphere_upload_desc.debug_name = "VICE Runtime Hi-Z sphere upload";
        auto new_sphere_upload = device->create_buffer(sphere_upload_desc);

        render::BufferDesc sphere_gpu_desc;
        sphere_gpu_desc.size = sphere_bytes;
        sphere_gpu_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination;
        sphere_gpu_desc.memory = render::MemoryLocation::gpu_only;
        sphere_gpu_desc.debug_name = "VICE Runtime Hi-Z sphere table";
        auto new_sphere_gpu = device->create_buffer(sphere_gpu_desc);

        render::BufferDesc indices_desc;
        indices_desc.size = index_bytes;
        indices_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_source;
        indices_desc.memory = render::MemoryLocation::gpu_only;
        indices_desc.debug_name = "VICE Runtime Hi-Z visible instance indices";
        auto new_indices = device->create_buffer(indices_desc);

        if (!new_sphere_upload || !new_sphere_gpu || !new_indices) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate Hi-Z occlusion buffers.");
            return false;
        }

        sphere_upload = std::move(new_sphere_upload);
        sphere_gpu = std::move(new_sphere_gpu);
        visible_indices = std::move(new_indices);
        allocated_capacity = new_capacity;
        active_capacity = required;
        return true;
    }
};

RuntimeHiZOcclusionCuller::RuntimeHiZOcclusionCuller() : state_{std::make_unique<State>()} {}
RuntimeHiZOcclusionCuller::~RuntimeHiZOcclusionCuller() = default;
RuntimeHiZOcclusionCuller::RuntimeHiZOcclusionCuller(RuntimeHiZOcclusionCuller&&) noexcept = default;
RuntimeHiZOcclusionCuller& RuntimeHiZOcclusionCuller::operator=(RuntimeHiZOcclusionCuller&&) noexcept = default;

bool RuntimeHiZOcclusionCuller::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime Hi-Z occlusion state is unavailable.");
        return false;
    }

    const bool metal = device.backend() == render::Backend::metal;
    render::ShaderModuleDesc shader_desc;
    shader_desc.stage = render::ShaderStage::compute;
    shader_desc.language = metal
        ? render::ShaderSourceLanguage::metal
        : render::ShaderSourceLanguage::hlsl;
    shader_desc.source = metal
        ? detail::hiz_occlusion_metal_source
        : detail::hiz_occlusion_hlsl_source;
    shader_desc.resources = detail::hiz_occlusion_resources;
    shader_desc.entry_point = "vice_runtime_hiz_occlusion_cs";
    shader_desc.debug_name = "VICE Runtime Hi-Z occlusion compute";
    auto shader = device.create_shader_module(shader_desc);

    render::ComputePipelineDesc pipeline_desc;
    pipeline_desc.compute_shader = shader.get();
    pipeline_desc.thread_group_size = {occlusion_group_size, 1U, 1U};
    pipeline_desc.debug_name = "VICE Runtime Hi-Z occlusion pipeline";
    auto pipeline = shader ? device.create_compute_pipeline(pipeline_desc) : nullptr;

    render::SamplerDesc sampler_desc;
    sampler_desc.min_filter = render::Filter::nearest;
    sampler_desc.mag_filter = render::Filter::nearest;
    sampler_desc.mip_filter = render::Filter::nearest;
    sampler_desc.address_u = render::AddressMode::clamp_to_edge;
    sampler_desc.address_v = render::AddressMode::clamp_to_edge;
    sampler_desc.address_w = render::AddressMode::clamp_to_edge;
    sampler_desc.debug_name = "VICE Runtime Hi-Z occlusion sampler";
    auto sampler = device.create_sampler(sampler_desc);

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(HiZOcclusionConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime Hi-Z occlusion constants";
    auto constants = device.create_buffer(constants_desc);

    render::BufferDesc zero_desc;
    zero_desc.size = sizeof(std::uint32_t);
    zero_desc.usage = render::BufferUsage::copy_source;
    zero_desc.memory = render::MemoryLocation::cpu_to_gpu;
    zero_desc.debug_name = "VICE Runtime Hi-Z occlusion zero upload";
    auto zero_upload = device.create_buffer(zero_desc);

    render::BufferDesc count_desc;
    count_desc.size = sizeof(std::uint32_t);
    count_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_source |
        render::BufferUsage::copy_destination;
    count_desc.memory = render::MemoryLocation::gpu_only;
    count_desc.debug_name = "VICE Runtime Hi-Z visible instance count";
    auto visible_count = device.create_buffer(count_desc);

    const std::uint32_t zero = 0U;
    if (!shader || !pipeline || !sampler || !constants || !zero_upload || !visible_count ||
        !zero_upload->write(0U, std::as_bytes(std::span{&zero, 1}))) {
        set_diagnostics(diagnostics, "VICE Runtime could not create Hi-Z occlusion resources.");
        return false;
    }

    state_->device = &device;
    state_->shader = std::move(shader);
    state_->pipeline = std::move(pipeline);
    state_->sampler = std::move(sampler);
    state_->constants = std::move(constants);
    state_->zero_upload = std::move(zero_upload);
    state_->visible_count = std::move(visible_count);
    state_->sphere_upload.reset();
    state_->sphere_gpu.reset();
    state_->visible_indices.reset();
    state_->allocated_capacity = 0U;
    state_->active_capacity = 0U;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeHiZOcclusionCuller::cull(
    render::CommandList& commands,
    const RuntimeVisibilityGpuResult& frustum_visibility,
    const std::span<const RuntimeVisibilitySphere> spheres,
    const math::Mat4& view_projection,
    const float near_plane,
    const float far_plane,
    const RuntimeHiZPyramid& pyramid,
    const float depth_bias,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr || !state_->pipeline ||
        !state_->sampler || !state_->constants || !state_->zero_upload || !state_->visible_count) {
        set_diagnostics(diagnostics, "Runtime Hi-Z occlusion culler is not initialized.");
        return false;
    }
    if (!finite_matrix(view_projection) || !std::isfinite(near_plane) ||
        !std::isfinite(far_plane) || near_plane <= 0.0F || far_plane <= near_plane ||
        !std::isfinite(depth_bias) || depth_bias < 0.0F) {
        set_diagnostics(diagnostics, "Runtime Hi-Z occlusion rejected invalid camera parameters.");
        return false;
    }
    if (spheres.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(diagnostics, "Runtime Hi-Z occlusion instance count exceeds the GPU contract.");
        return false;
    }

    const std::uint32_t instance_count = static_cast<std::uint32_t>(spheres.size());
    std::vector<HiZOcclusionSphere> dense_spheres(spheres.size());
    std::vector<bool> seen(spheres.size(), false);
    for (const auto& sphere : spheres) {
        if (!valid(sphere) || sphere.instance_index >= instance_count || seen[sphere.instance_index]) {
            set_diagnostics(diagnostics, "Runtime Hi-Z occlusion requires unique dense scene-instance sphere indices.");
            return false;
        }
        seen[sphere.instance_index] = true;
        dense_spheres[sphere.instance_index] = HiZOcclusionSphere{
            .center_radius = {sphere.center.x, sphere.center.y, sphere.center.z, sphere.radius},
        };
    }

    const std::uint32_t pyramid_level_count = pyramid.level_count();
    if (pyramid_level_count == 0U || pyramid.level(0U) == nullptr) {
        set_diagnostics(diagnostics, "Runtime Hi-Z occlusion requires a built pyramid allocation.");
        return false;
    }
    const auto base_size = pyramid.level_size(0U);
    if (base_size.width == 0U || base_size.height == 0U) {
        set_diagnostics(diagnostics, "Runtime Hi-Z occlusion rejected an invalid pyramid extent.");
        return false;
    }

    if (!commands.copy_buffer(
            *state_->zero_upload,
            0U,
            *state_->visible_count,
            0U,
            sizeof(std::uint32_t)
        )) {
        set_diagnostics(diagnostics, "VICE Runtime could not reset Hi-Z occlusion visibility.");
        return false;
    }

    state_->active_capacity = frustum_visibility.capacity;
    if (frustum_visibility.capacity == 0U) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (frustum_visibility.visible_count == nullptr || frustum_visibility.visible_indices == nullptr ||
        instance_count == 0U ||
        !render::has_usage(frustum_visibility.visible_count->usage(), render::BufferUsage::storage) ||
        !render::has_usage(frustum_visibility.visible_indices->usage(), render::BufferUsage::storage)) {
        set_diagnostics(diagnostics, "Runtime Hi-Z occlusion input visibility is unavailable.");
        return false;
    }

    const std::uint32_t required_capacity = std::max(instance_count, frustum_visibility.capacity);
    if (!state_->ensure_capacity(required_capacity, diagnostics)) return false;
    state_->active_capacity = frustum_visibility.capacity;

    const auto sphere_bytes = std::as_bytes(std::span{dense_spheres});
    if (!state_->sphere_upload->write(0U, sphere_bytes) ||
        !commands.copy_buffer(
            *state_->sphere_upload,
            0U,
            *state_->sphere_gpu,
            0U,
            sphere_bytes.size()
        )) {
        set_diagnostics(diagnostics, "VICE Runtime could not upload Hi-Z occlusion spheres.");
        return false;
    }

    const float x_scale = projection_scale(view_projection, 0U);
    const float y_scale = projection_scale(view_projection, 1U);
    if (!std::isfinite(x_scale) || !std::isfinite(y_scale) || x_scale <= 0.0F || y_scale <= 0.0F) {
        set_diagnostics(diagnostics, "Runtime Hi-Z occlusion rejected an invalid projection scale.");
        return false;
    }

    const std::uint32_t bound_level_count = std::min(
        pyramid_level_count,
        runtime_hiz_occlusion_max_levels
    );
    HiZOcclusionConstants constants{};
    constants.view_projection = view_projection;
    constants.projection_data[0] = x_scale;
    constants.projection_data[1] = y_scale;
    constants.projection_data[2] = near_plane;
    constants.projection_data[3] = far_plane;
    constants.occlusion_data[0] = depth_bias;
    constants.visibility_data[0] = frustum_visibility.capacity;
    constants.visibility_data[1] = instance_count;
    constants.visibility_data[2] = bound_level_count;
    constants.viewport_data[0] = base_size.width;
    constants.viewport_data[1] = base_size.height;
    if (!state_->constants->write(0U, std::as_bytes(std::span{&constants, 1}))) {
        set_diagnostics(diagnostics, "VICE Runtime could not update Hi-Z occlusion constants.");
        return false;
    }

    const render::BufferViewDesc count_view{
        .offset = 0U,
        .size = sizeof(std::uint32_t),
        .structure_stride = sizeof(std::uint32_t),
    };
    const render::BufferViewDesc input_indices_view{
        .offset = 0U,
        .size = static_cast<std::uint64_t>(frustum_visibility.capacity) * sizeof(std::uint32_t),
        .structure_stride = sizeof(std::uint32_t),
    };
    const render::BufferViewDesc sphere_view{
        .offset = 0U,
        .size = sphere_bytes.size(),
        .structure_stride = sizeof(HiZOcclusionSphere),
    };
    const render::BufferViewDesc output_indices_view{
        .offset = 0U,
        .size = static_cast<std::uint64_t>(state_->allocated_capacity) * sizeof(std::uint32_t),
        .structure_stride = sizeof(std::uint32_t),
    };

    if (!commands.set_compute_pipeline(*state_->pipeline) ||
        !commands.set_compute_constant_buffer(
            detail::hiz_occlusion_constants_binding,
            *state_->constants,
            0U,
            sizeof(HiZOcclusionConstants)
        ) ||
        !commands.set_compute_storage_buffer(
            detail::hiz_occlusion_input_count_binding,
            *frustum_visibility.visible_count,
            count_view
        ) ||
        !commands.set_compute_storage_buffer(
            detail::hiz_occlusion_input_indices_binding,
            *frustum_visibility.visible_indices,
            input_indices_view
        ) ||
        !commands.set_compute_storage_buffer(
            detail::hiz_occlusion_spheres_binding,
            *state_->sphere_gpu,
            sphere_view
        ) ||
        !commands.set_compute_storage_buffer(
            detail::hiz_occlusion_output_count_binding,
            *state_->visible_count,
            count_view
        ) ||
        !commands.set_compute_storage_buffer(
            detail::hiz_occlusion_output_indices_binding,
            *state_->visible_indices,
            output_indices_view
        )) {
        set_diagnostics(diagnostics, "VICE Runtime could not bind Hi-Z occlusion buffers.");
        return false;
    }

    render::Texture* fallback_level = pyramid.level(bound_level_count - 1U);
    if (fallback_level == nullptr) {
        set_diagnostics(diagnostics, "Runtime Hi-Z occlusion pyramid level is unavailable.");
        return false;
    }
    for (std::uint32_t level = 0U; level < runtime_hiz_occlusion_max_levels; ++level) {
        render::Texture* texture = level < bound_level_count ? pyramid.level(level) : fallback_level;
        if (texture == nullptr || !render::has_usage(texture->usage(), render::TextureUsage::sampled) ||
            !commands.set_compute_texture(detail::hiz_occlusion_level_binding(level), *texture)) {
            set_diagnostics(diagnostics, "VICE Runtime could not bind a Hi-Z occlusion pyramid level.");
            return false;
        }
    }

    const std::uint32_t group_count =
        (frustum_visibility.capacity + occlusion_group_size - 1U) / occlusion_group_size;
    if (!commands.set_compute_sampler(detail::hiz_occlusion_sampler_binding, *state_->sampler) ||
        !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = group_count,
            .group_count_y = 1U,
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode Hi-Z occlusion culling.");
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeVisibilityGpuResult RuntimeHiZOcclusionCuller::result() const noexcept {
    if (state_ == nullptr) return {};
    return RuntimeVisibilityGpuResult{
        .visible_count = state_->visible_count.get(),
        .visible_indices = state_->visible_indices.get(),
        .capacity = state_->active_capacity,
    };
}

} // namespace vice::runtime
