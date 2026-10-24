#pragma once

#include <vice/math/math.hpp>
#include <vice/render/ray_tracing.hpp>
#include <vice/render/rhi.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vice::runtime {

struct RuntimeRayAmbientOcclusionCamera {
    math::Vec3 position{};
    math::Vec3 forward{0.0F, 0.0F, -1.0F};
    math::Vec3 up{0.0F, 1.0F, 0.0F};
    float vertical_fov_radians = 0.82F;
    float aspect_ratio = 1.0F;
    float near_plane = 0.05F;
    float far_plane = 250.0F;
    float jitter_x_ndc = 0.0F;
    float jitter_y_ndc = 0.0F;
};

struct RuntimeRayAmbientOcclusionConstants {
    float camera_position_near[4]{};
    float camera_forward_far[4]{};
    float camera_right_tan_half_fov[4]{};
    float camera_up_aspect[4]{};
    float target_size_jitter[4]{};
    float ao_parameters[4]{};
};

static_assert(sizeof(RuntimeRayAmbientOcclusionConstants) == 96U);

enum class RuntimeRayAmbientOcclusionNeighbor : std::int8_t {
    invalid = 0,
    negative = -1,
    positive = 1,
};

[[nodiscard]] float runtime_ray_ambient_occlusion_linear_depth(
    float device_depth,
    float near_plane,
    float far_plane
) noexcept;

[[nodiscard]] RuntimeRayAmbientOcclusionNeighbor runtime_ray_ambient_occlusion_preferred_neighbor(
    float center_device_depth,
    float negative_device_depth,
    float positive_device_depth,
    float near_plane,
    float far_plane
) noexcept;

[[nodiscard]] std::optional<math::Vec3> runtime_ray_ambient_occlusion_world_position(
    const RuntimeRayAmbientOcclusionCamera& camera,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t pixel_x,
    std::uint32_t pixel_y,
    float device_depth
) noexcept;

[[nodiscard]] std::optional<math::Vec3> runtime_ray_ambient_occlusion_hemisphere_direction(
    math::Vec3 normal,
    std::uint32_t pixel_x,
    std::uint32_t pixel_y,
    std::uint32_t sample_seed = 0U
) noexcept;

[[nodiscard]] std::optional<RuntimeRayAmbientOcclusionConstants>
make_runtime_ray_ambient_occlusion_constants(
    const RuntimeRayAmbientOcclusionCamera& camera,
    std::uint32_t width,
    std::uint32_t height,
    float radius = 1.5F,
    float surface_bias = 0.0025F,
    std::uint32_t sample_seed = 0U
) noexcept;

class RuntimeRayAmbientOcclusionPass final {
public:
    RuntimeRayAmbientOcclusionPass();
    ~RuntimeRayAmbientOcclusionPass();

    RuntimeRayAmbientOcclusionPass(const RuntimeRayAmbientOcclusionPass&) = delete;
    RuntimeRayAmbientOcclusionPass& operator=(const RuntimeRayAmbientOcclusionPass&) = delete;
    RuntimeRayAmbientOcclusionPass(RuntimeRayAmbientOcclusionPass&&) noexcept;
    RuntimeRayAmbientOcclusionPass& operator=(RuntimeRayAmbientOcclusionPass&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        const std::filesystem::path& shader_manifest,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool resize(
        std::uint32_t width,
        std::uint32_t height,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool dispatch(
        render::CommandList& commands,
        render::Texture& camera_depth,
        render::AccelerationStructure& scene_tlas,
        const RuntimeRayAmbientOcclusionConstants& constants,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] render::Texture* visibility() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
