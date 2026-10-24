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

struct RuntimeDirectionalRayShadowCamera {
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

struct RuntimeDirectionalRayShadowConstants {
    float camera_position_near[4]{};
    float camera_forward_far[4]{};
    float camera_right_tan_half_fov[4]{};
    float camera_up_aspect[4]{};
    float light_direction_bias[4]{};
    float target_size_jitter[4]{};
};

static_assert(sizeof(RuntimeDirectionalRayShadowConstants) == 96U);

[[nodiscard]] float runtime_directional_ray_shadow_linear_depth(
    float device_depth,
    float near_plane,
    float far_plane
) noexcept;

[[nodiscard]] std::optional<RuntimeDirectionalRayShadowConstants>
make_runtime_directional_ray_shadow_constants(
    const RuntimeDirectionalRayShadowCamera& camera,
    math::Vec3 direction_toward_light,
    std::uint32_t width,
    std::uint32_t height,
    float surface_bias = 0.0025F
) noexcept;

class RuntimeDirectionalRayShadowPass final {
public:
    RuntimeDirectionalRayShadowPass();
    ~RuntimeDirectionalRayShadowPass();

    RuntimeDirectionalRayShadowPass(const RuntimeDirectionalRayShadowPass&) = delete;
    RuntimeDirectionalRayShadowPass& operator=(const RuntimeDirectionalRayShadowPass&) = delete;
    RuntimeDirectionalRayShadowPass(RuntimeDirectionalRayShadowPass&&) noexcept;
    RuntimeDirectionalRayShadowPass& operator=(RuntimeDirectionalRayShadowPass&&) noexcept;

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
        const RuntimeDirectionalRayShadowConstants& constants,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] render::Texture* visibility() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime