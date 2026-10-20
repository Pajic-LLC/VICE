#include <vice/runtime/visibility.hpp>

#include "visibility_shaders.hpp"

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

constexpr std::uint32_t visibility_thread_group_size = 64U;

struct GpuVisibilityInput {
    float center_radius[4]{};
    std::uint32_t instance_index = 0U;
    std::uint32_t padding[3]{};
};

struct GpuVisibilityConstants {
    float frustum_planes[6][4]{};
    std::uint32_t instance_count = 0U;
    std::uint32_t padding[3]{};
};

static_assert(sizeof(GpuVisibilityInput) == 32U);
static_assert(sizeof(GpuVisibilityConstants) == 112U);

[[nodiscard]] constexpr math::Vec3 add(
    const math::Vec3 left,
    const math::Vec3 right
) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] constexpr math::Vec3 scaled(
    const math::Vec3 value,
    const float scale
) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] bool finite(const math::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] std::uint32_t grown_capacity(const std::uint32_t required) noexcept {
    if (required <= 1U) return 1U;
    std::uint32_t capacity = 1U;
    while (capacity < required && capacity <= std::numeric_limits<std::uint32_t>::max() / 2U) {
        capacity *= 2U;
    }
    return std::max(capacity, required);
}

} // namespace

bool valid(const RuntimeBoundingSphere& sphere) noexcept {
    return finite(sphere.center) && std::isfinite(sphere.radius) && sphere.radius >= 0.0F;
}

bool valid(const RuntimeVisibilityPlane& plane) noexcept {
    if (!finite(plane.normal) || !std::isfinite(plane.distance)) return false;
    const float length_squared = math::dot(plane.normal, plane.normal);
    return std::isfinite(length_squared) && length_squared > 0.999F && length_squared < 1.001F;
}

bool valid(const RuntimeVisibilityFrustum& frustum) noexcept {
    return std::all_of(frustum.planes.begin(), frustum.planes.end(), [](const auto& plane) {
        return valid(plane);
    });
}

bool valid(const RuntimeVisibilitySphere& sphere) noexcept {
    return finite(sphere.center) && std::isfinite(sphere.radius) && sphere.radius >= 0.0F;
}

RuntimeBoundingSphere runtime_mesh_bounding_sphere(
    const std::span<const render::MeshVertex> vertices
) noexcept {
    if (vertices.empty()) return {};

    math::Vec3 minimum{
        vertices.front().position[0],
        vertices.front().position[1],
        vertices.front().position[2],
    };
    math::Vec3 maximum = minimum;
    for (const auto& vertex : vertices) {
        minimum.x = std::min(minimum.x, vertex.position[0]);
        minimum.y = std::min(minimum.y, vertex.position[1]);
        minimum.z = std::min(minimum.z, vertex.position[2]);
        maximum.x = std::max(maximum.x, vertex.position[0]);
        maximum.y = std::max(maximum.y, vertex.position[1]);
        maximum.z = std::max(maximum.z, vertex.position[2]);
    }

    const math::Vec3 center{
        (minimum.x + maximum.x) * 0.5F,
        (minimum.y + maximum.y) * 0.5F,
        (minimum.z + maximum.z) * 0.5F,
    };
    float radius_squared = 0.0F;
    for (const auto& vertex : vertices) {
        const math::Vec3 offset{
            vertex.position[0] - center.x,
            vertex.position[1] - center.y,
            vertex.position[2] - center.z,
        };
        radius_squared = std::max(radius_squared, math::dot(offset, offset));
    }
    return RuntimeBoundingSphere{
        .center = center,
        .radius = std::sqrt(std::max(radius_squared, 0.0F)),
    };
}

RuntimeBoundingSphere runtime_transform_bounding_sphere(
    const RuntimeBoundingSphere& sphere,
    const math::Mat4& transform
) noexcept {
    if (!valid(sphere)) return {};

    const math::Vec3 center{
        transform(0U, 0U) * sphere.center.x + transform(0U, 1U) * sphere.center.y +
            transform(0U, 2U) * sphere.center.z + transform(0U, 3U),
        transform(1U, 0U) * sphere.center.x + transform(1U, 1U) * sphere.center.y +
            transform(1U, 2U) * sphere.center.z + transform(1U, 3U),
        transform(2U, 0U) * sphere.center.x + transform(2U, 1U) * sphere.center.y +
            transform(2U, 2U) * sphere.center.z + transform(2U, 3U),
    };

    const auto column_length = [&](const std::size_t column) {
        return std::sqrt(
            transform(0U, column) * transform(0U, column) +
            transform(1U, column) * transform(1U, column) +
            transform(2U, column) * transform(2U, column)
        );
    };
    const float scale = std::max({column_length(0U), column_length(1U), column_length(2U)});
    if (!finite(center) || !std::isfinite(scale)) return {};
    return RuntimeBoundingSphere{.center = center, .radius = sphere.radius * scale};
}

RuntimeVisibilityFrustum runtime_visibility_frustum(
    math::Vec3 forward,
    math::Vec3 up,
    const float vertical_fov_radians,
    const float aspect_ratio,
    const float near_plane,
    const float far_plane
) noexcept {
    constexpr float pi = 3.14159265358979323846F;
    RuntimeVisibilityFrustum result{};
    if (!finite(forward) || !finite(up) ||
        !std::isfinite(vertical_fov_radians) || vertical_fov_radians <= 0.0F ||
        vertical_fov_radians >= pi || !std::isfinite(aspect_ratio) || aspect_ratio <= 0.0F ||
        !std::isfinite(near_plane) || !std::isfinite(far_plane) || near_plane <= 0.0F ||
        far_plane <= near_plane) {
        return result;
    }

    forward = math::normalize(forward);
    math::Vec3 right = math::normalize(math::cross(forward, up));
    if (math::dot(forward, forward) < 0.999F || math::dot(right, right) < 0.999F) return result;
    up = math::normalize(math::cross(right, forward));
    if (math::dot(up, up) < 0.999F) return result;

    const float tangent_y = std::tan(vertical_fov_radians * 0.5F);
    const float tangent_x = tangent_y * aspect_ratio;
    if (!std::isfinite(tangent_x) || !std::isfinite(tangent_y)) return result;

    result.planes[0] = {math::normalize(add(scaled(forward, tangent_x), right)), 0.0F};
    result.planes[1] = {math::normalize(add(scaled(forward, tangent_x), scaled(right, -1.0F))), 0.0F};
    result.planes[2] = {math::normalize(add(scaled(forward, tangent_y), up)), 0.0F};
    result.planes[3] = {math::normalize(add(scaled(forward, tangent_y), scaled(up, -1.0F))), 0.0F};
    result.planes[4] = {forward, -near_plane};
    result.planes[5] = {scaled(forward, -1.0F), far_plane};
    return result;
}

bool runtime_visibility_intersects(
    const RuntimeVisibilityFrustum& frustum,
    const RuntimeVisibilitySphere& sphere
) noexcept {
    if (!valid(frustum) || !valid(sphere)) return false;
    for (const auto& plane : frustum.planes) {
        const float distance = math::dot(plane.normal, sphere.center) + plane.distance;
        if (distance < -sphere.radius) return false;
    }
    return true;
}

struct RuntimeVisibilityCuller::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> shader;
    std::unique_ptr<render::ComputePipeline> pipeline;
    std::unique_ptr<render::Buffer> constants;
    std::unique_ptr<render::Buffer> zero_upload;
    std::unique_ptr<render::Buffer> input_upload;
    std::unique_ptr<render::Buffer> input_gpu;
    std::unique_ptr<render::Buffer> visible_count;
    std::unique_ptr<render::Buffer> visible_indices;
    std::uint32_t capacity = 0U;

    [[nodiscard]] bool ensure_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (required <= capacity && input_upload && input_gpu && visible_indices) return true;
        if (device == nullptr || required == 0U) return false;

        const std::uint32_t new_capacity = grown_capacity(required);
        const std::uint64_t input_size =
            static_cast<std::uint64_t>(new_capacity) * sizeof(GpuVisibilityInput);
        const std::uint64_t output_size =
            static_cast<std::uint64_t>(new_capacity) * sizeof(std::uint32_t);

        render::BufferDesc upload_desc;
        upload_desc.size = input_size;
        upload_desc.usage = render::BufferUsage::copy_source;
        upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
        upload_desc.debug_name = "VICE Runtime visibility input upload";
        auto new_upload = device->create_buffer(upload_desc);

        render::BufferDesc input_desc;
        input_desc.size = input_size;
        input_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination;
        input_desc.memory = render::MemoryLocation::gpu_only;
        input_desc.debug_name = "VICE Runtime visibility input";
        auto new_input = device->create_buffer(input_desc);

        render::BufferDesc output_desc;
        output_desc.size = output_size;
        output_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_source;
        output_desc.memory = render::MemoryLocation::gpu_only;
        output_desc.debug_name = "VICE Runtime visible instance indices";
        auto new_output = device->create_buffer(output_desc);

        if (!new_upload || !new_input || !new_output) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate GPU visibility buffers.");
            return false;
        }
        input_upload = std::move(new_upload);
        input_gpu = std::move(new_input);
        visible_indices = std::move(new_output);
        capacity = new_capacity;
        return true;
    }
};

RuntimeVisibilityCuller::RuntimeVisibilityCuller() : state_{std::make_unique<State>()} {}
RuntimeVisibilityCuller::~RuntimeVisibilityCuller() = default;
RuntimeVisibilityCuller::RuntimeVisibilityCuller(RuntimeVisibilityCuller&&) noexcept = default;
RuntimeVisibilityCuller& RuntimeVisibilityCuller::operator=(RuntimeVisibilityCuller&&) noexcept = default;

bool RuntimeVisibilityCuller::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime visibility state is unavailable.");
        return false;
    }

    const bool metal = device.backend() == render::Backend::metal;
    render::ShaderModuleDesc shader_desc;
    shader_desc.stage = render::ShaderStage::compute;
    shader_desc.language = metal
        ? render::ShaderSourceLanguage::metal
        : render::ShaderSourceLanguage::hlsl;
    shader_desc.source = metal ? detail::visibility_metal_source : detail::visibility_hlsl_source;
    shader_desc.resources = detail::visibility_resources;
    shader_desc.entry_point = "vice_runtime_visibility_cs";
    shader_desc.debug_name = "VICE Runtime visibility compute";
    auto shader = device.create_shader_module(shader_desc);
    if (!shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the visibility compute shader.");
        return false;
    }

    render::ComputePipelineDesc pipeline_desc;
    pipeline_desc.compute_shader = shader.get();
    pipeline_desc.thread_group_size = {visibility_thread_group_size, 1U, 1U};
    pipeline_desc.debug_name = "VICE Runtime visibility pipeline";
    auto pipeline = device.create_compute_pipeline(pipeline_desc);
    if (!pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the visibility compute pipeline.");
        return false;
    }

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(GpuVisibilityConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime visibility constants";
    auto constants = device.create_buffer(constants_desc);

    render::BufferDesc zero_desc;
    zero_desc.size = sizeof(std::uint32_t);
    zero_desc.usage = render::BufferUsage::copy_source;
    zero_desc.memory = render::MemoryLocation::cpu_to_gpu;
    zero_desc.debug_name = "VICE Runtime visibility zero upload";
    auto zero_upload = device.create_buffer(zero_desc);

    render::BufferDesc count_desc;
    count_desc.size = sizeof(std::uint32_t);
    count_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_source |
        render::BufferUsage::copy_destination;
    count_desc.memory = render::MemoryLocation::gpu_only;
    count_desc.debug_name = "VICE Runtime visible instance count";
    auto visible_count = device.create_buffer(count_desc);

    const std::uint32_t zero = 0U;
    if (!constants || !zero_upload || !visible_count ||
        !zero_upload->write(0U, std::as_bytes(std::span{&zero, 1}))) {
        set_diagnostics(diagnostics, "VICE Runtime could not create visibility control buffers.");
        return false;
    }

    state_->device = &device;
    state_->shader = std::move(shader);
    state_->pipeline = std::move(pipeline);
    state_->constants = std::move(constants);
    state_->zero_upload = std::move(zero_upload);
    state_->visible_count = std::move(visible_count);
    state_->input_upload.reset();
    state_->input_gpu.reset();
    state_->visible_indices.reset();
    state_->capacity = 0U;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeVisibilityCuller::cull(
    render::CommandList& commands,
    const std::span<const RuntimeVisibilitySphere> spheres,
    const RuntimeVisibilityFrustum& frustum,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr || !state_->pipeline ||
        !state_->constants || !state_->zero_upload || !state_->visible_count) {
        set_diagnostics(diagnostics, "Runtime visibility culler is not initialized.");
        return false;
    }
    if (!valid(frustum)) {
        set_diagnostics(diagnostics, "Runtime visibility rejected an invalid frustum.");
        return false;
    }
    if (spheres.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(diagnostics, "Runtime visibility instance count exceeds the GPU contract.");
        return false;
    }
    for (const auto& sphere : spheres) {
        if (!valid(sphere)) {
            set_diagnostics(diagnostics, "Runtime visibility rejected an invalid instance sphere.");
            return false;
        }
    }

    GpuVisibilityConstants constants{};
    for (std::size_t index = 0U; index < frustum.planes.size(); ++index) {
        constants.frustum_planes[index][0] = frustum.planes[index].normal.x;
        constants.frustum_planes[index][1] = frustum.planes[index].normal.y;
        constants.frustum_planes[index][2] = frustum.planes[index].normal.z;
        constants.frustum_planes[index][3] = frustum.planes[index].distance;
    }
    constants.instance_count = static_cast<std::uint32_t>(spheres.size());
    if (!state_->constants->write(0U, std::as_bytes(std::span{&constants, 1})) ||
        !commands.copy_buffer(
            *state_->zero_upload,
            0U,
            *state_->visible_count,
            0U,
            sizeof(std::uint32_t)
        )) {
        set_diagnostics(diagnostics, "VICE Runtime could not reset visibility state.");
        return false;
    }

    if (spheres.empty()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    const std::uint32_t count = static_cast<std::uint32_t>(spheres.size());
    if (!state_->ensure_capacity(count, diagnostics)) return false;

    std::vector<GpuVisibilityInput> gpu_inputs;
    gpu_inputs.reserve(spheres.size());
    for (const auto& sphere : spheres) {
        gpu_inputs.push_back(GpuVisibilityInput{
            .center_radius = {sphere.center.x, sphere.center.y, sphere.center.z, sphere.radius},
            .instance_index = sphere.instance_index,
        });
    }
    const auto input_bytes = std::as_bytes(std::span{gpu_inputs});
    if (!state_->input_upload->write(0U, input_bytes) ||
        !commands.copy_buffer(
            *state_->input_upload,
            0U,
            *state_->input_gpu,
            0U,
            input_bytes.size()
        )) {
        set_diagnostics(diagnostics, "VICE Runtime could not upload visibility instances.");
        return false;
    }

    const render::BufferViewDesc input_view{
        .offset = 0U,
        .size = input_bytes.size(),
        .structure_stride = sizeof(GpuVisibilityInput),
    };
    const render::BufferViewDesc count_view{
        .offset = 0U,
        .size = sizeof(std::uint32_t),
        .structure_stride = sizeof(std::uint32_t),
    };
    const render::BufferViewDesc output_view{
        .offset = 0U,
        .size = static_cast<std::uint64_t>(state_->capacity) * sizeof(std::uint32_t),
        .structure_stride = sizeof(std::uint32_t),
    };
    const std::uint32_t group_count =
        (count + visibility_thread_group_size - 1U) / visibility_thread_group_size;

    if (!commands.set_compute_pipeline(*state_->pipeline) ||
        !commands.set_compute_constant_buffer(
            detail::visibility_constants_binding,
            *state_->constants,
            0U,
            sizeof(GpuVisibilityConstants)
        ) ||
        !commands.set_compute_buffer(
            detail::visibility_input_binding,
            *state_->input_gpu,
            input_view
        ) ||
        !commands.set_compute_storage_buffer(
            detail::visibility_count_binding,
            *state_->visible_count,
            count_view
        ) ||
        !commands.set_compute_storage_buffer(
            detail::visibility_indices_binding,
            *state_->visible_indices,
            output_view
        ) ||
        !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = group_count,
            .group_count_y = 1U,
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode GPU visibility culling.");
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeVisibilityGpuResult RuntimeVisibilityCuller::result() const noexcept {
    if (state_ == nullptr) return {};
    return RuntimeVisibilityGpuResult{
        .visible_count = state_->visible_count.get(),
        .visible_indices = state_->visible_indices.get(),
        .capacity = state_->capacity,
    };
}

} // namespace vice::runtime
