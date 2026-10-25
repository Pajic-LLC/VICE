#pragma once

#include <vice/math/math.hpp>
#include <vice/render/rhi.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vice::runtime {

inline constexpr std::uint32_t runtime_ray_reflection_max_instance_id = 0x00FFFFFFU;
inline constexpr float runtime_ray_reflection_default_max_distance = 250.0F;
inline constexpr float runtime_ray_reflection_default_surface_bias = 0.02F;
inline constexpr float runtime_ray_reflection_default_max_roughness = 0.85F;
inline constexpr float runtime_ray_reflection_history_depth_threshold = 0.0025F;

struct RuntimeRayReflectionsCamera {
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

struct RuntimeRayReflectionsHistory {
    math::Mat4 previous_view_projection{};
    bool valid = false;
    float depth_threshold = runtime_ray_reflection_history_depth_threshold;
};

struct RuntimeRayReflectionHistoryProjection {
    float u = 0.0F;
    float v = 0.0F;
    float device_depth = 0.0F;
};

struct RuntimeRayReflectionsConstants {
    float camera_position_near[4]{};
    float camera_forward_far[4]{};
    float camera_right_tan_half_fov[4]{};
    float camera_up_aspect[4]{};
    float target_size_jitter[4]{};
    float trace_parameters[4]{};
    float environment_parameters[4]{};
    math::Mat4 previous_view_projection{};
    float history_parameters[4]{};
};

static_assert(sizeof(RuntimeRayReflectionsConstants) == 192U);

struct RuntimeRayReflectionHit {
    float world_position[3]{};
    std::uint32_t instance_id_plus_one = 0U;
};

static_assert(sizeof(RuntimeRayReflectionHit) == 16U);

[[nodiscard]] float runtime_ray_reflections_linear_depth(
    float device_depth,
    float near_plane,
    float far_plane
) noexcept;

[[nodiscard]] std::optional<math::Vec3> runtime_ray_reflections_world_position(
    const RuntimeRayReflectionsCamera& camera,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t pixel_x,
    std::uint32_t pixel_y,
    float device_depth
) noexcept;

[[nodiscard]] std::optional<math::Vec3> runtime_ray_reflections_direction(
    math::Vec3 surface_position,
    math::Vec3 camera_position,
    math::Vec3 world_normal
) noexcept;

[[nodiscard]] bool runtime_ray_reflections_eligible(
    float roughness,
    float max_roughness = runtime_ray_reflection_default_max_roughness
) noexcept;

[[nodiscard]] std::optional<RuntimeRayReflectionHistoryProjection>
runtime_ray_reflections_history_projection(
    const math::Mat4& previous_view_projection,
    math::Vec3 world_position
) noexcept;

[[nodiscard]] bool runtime_ray_reflections_history_depth_matches(
    float expected_device_depth,
    float sampled_device_depth,
    float depth_threshold = runtime_ray_reflection_history_depth_threshold
) noexcept;

[[nodiscard]] std::optional<RuntimeRayReflectionsConstants>
make_runtime_ray_reflections_constants(
    const RuntimeRayReflectionsCamera& camera,
    std::uint32_t width,
    std::uint32_t height,
    float environment_intensity,
    float environment_rotation_radians,
    float environment_max_specular_lod,
    float max_distance = runtime_ray_reflection_default_max_distance,
    float surface_bias = runtime_ray_reflection_default_surface_bias,
    float max_roughness = runtime_ray_reflection_default_max_roughness,
    const RuntimeRayReflectionsHistory& history = {}
) noexcept;

class RuntimeRayReflectionsPass final {
public:
    RuntimeRayReflectionsPass();
    ~RuntimeRayReflectionsPass();

    RuntimeRayReflectionsPass(const RuntimeRayReflectionsPass&) = delete;
    RuntimeRayReflectionsPass& operator=(const RuntimeRayReflectionsPass&) = delete;
    RuntimeRayReflectionsPass(RuntimeRayReflectionsPass&&) noexcept;
    RuntimeRayReflectionsPass& operator=(RuntimeRayReflectionsPass&&) noexcept;

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
        render::Texture& surface_guide,
        render::AccelerationStructure& scene_tlas,
        render::Texture& environment_specular,
        render::Texture& previous_scene_color,
        render::Texture& previous_depth,
        render::Sampler& environment_sampler,
        const RuntimeRayReflectionsConstants& constants,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] render::Texture* radiance() const noexcept;
    [[nodiscard]] render::Buffer* hits() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
