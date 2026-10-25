#pragma once

#include <vice/math/math.hpp>
#include <vice/render/rhi.hpp>
#include <vice/runtime/ray_reflections.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vice::runtime {

inline constexpr std::uint32_t runtime_ray_diffuse_gi_max_instance_id = 0x00FFFFFFU;
inline constexpr std::uint32_t runtime_ray_diffuse_gi_sample_period = 0x01000000U;
inline constexpr float runtime_ray_diffuse_gi_default_max_distance = 40.0F;
inline constexpr float runtime_ray_diffuse_gi_default_surface_bias = 0.02F;

using RuntimeRayDiffuseGiCamera = RuntimeRayReflectionsCamera;

struct RuntimeRayDiffuseGiConstants {
    float camera_position_near[4]{};
    float camera_forward_far[4]{};
    float camera_right_tan_half_fov[4]{};
    float camera_up_aspect[4]{};
    float target_size_jitter[4]{};
    float trace_parameters[4]{};
    float environment_parameters[4]{};
};

static_assert(sizeof(RuntimeRayDiffuseGiConstants) == 112U);

struct RuntimeRayDiffuseGiHit {
    float world_position[3]{};
    // The shader stores the authoritative Runtime scene-instance index plus one.
    // Zero remains the miss sentinel so the record stays compact and GPU-friendly.
    std::uint32_t instance_id_plus_one = 0U;
};

static_assert(sizeof(RuntimeRayDiffuseGiHit) == 16U);

[[nodiscard]] std::optional<math::Vec3> runtime_ray_diffuse_gi_direction(
    math::Vec3 world_normal,
    std::uint32_t pixel_x,
    std::uint32_t pixel_y,
    std::uint32_t frame_index
) noexcept;

[[nodiscard]] std::optional<RuntimeRayDiffuseGiConstants>
make_runtime_ray_diffuse_gi_constants(
    const RuntimeRayDiffuseGiCamera& camera,
    std::uint32_t width,
    std::uint32_t height,
    float environment_intensity,
    float environment_rotation_radians,
    std::uint32_t frame_index,
    float max_distance = runtime_ray_diffuse_gi_default_max_distance,
    float surface_bias = runtime_ray_diffuse_gi_default_surface_bias
) noexcept;

class RuntimeRayDiffuseGiPass final {
public:
    RuntimeRayDiffuseGiPass();
    ~RuntimeRayDiffuseGiPass();

    RuntimeRayDiffuseGiPass(const RuntimeRayDiffuseGiPass&) = delete;
    RuntimeRayDiffuseGiPass& operator=(const RuntimeRayDiffuseGiPass&) = delete;
    RuntimeRayDiffuseGiPass(RuntimeRayDiffuseGiPass&&) noexcept;
    RuntimeRayDiffuseGiPass& operator=(RuntimeRayDiffuseGiPass&&) noexcept;

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
        render::Buffer& scene_instance_indices,
        std::uint32_t scene_instance_index_count,
        render::Texture& environment_specular,
        render::Sampler& environment_sampler,
        const RuntimeRayDiffuseGiConstants& constants,
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
