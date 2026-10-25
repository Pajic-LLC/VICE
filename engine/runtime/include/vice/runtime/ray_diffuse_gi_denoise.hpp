#pragma once

#include <vice/render/rhi.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vice::runtime {

struct RuntimeRayDiffuseGiDenoiseConstants {
    float target_size_near_far[4]{};
    float filter_parameters[4]{};
};

static_assert(sizeof(RuntimeRayDiffuseGiDenoiseConstants) == 32U);

[[nodiscard]] float runtime_ray_diffuse_gi_bilateral_weight(
    float center_device_depth,
    float neighbor_device_depth,
    float normal_alignment,
    float pixel_distance_squared,
    float near_plane,
    float far_plane,
    float depth_sigma = 0.025F,
    float normal_power = 24.0F,
    float spatial_sigma = 1.5F
) noexcept;

[[nodiscard]] std::optional<RuntimeRayDiffuseGiDenoiseConstants>
make_runtime_ray_diffuse_gi_denoise_constants(
    std::uint32_t width,
    std::uint32_t height,
    float near_plane,
    float far_plane,
    float depth_sigma = 0.025F,
    float normal_power = 24.0F,
    float spatial_sigma = 1.5F,
    std::uint32_t kernel_radius = 2U
) noexcept;

class RuntimeRayDiffuseGiDenoisePass final {
public:
    RuntimeRayDiffuseGiDenoisePass();
    ~RuntimeRayDiffuseGiDenoisePass();

    RuntimeRayDiffuseGiDenoisePass(const RuntimeRayDiffuseGiDenoisePass&) = delete;
    RuntimeRayDiffuseGiDenoisePass& operator=(const RuntimeRayDiffuseGiDenoisePass&) = delete;
    RuntimeRayDiffuseGiDenoisePass(RuntimeRayDiffuseGiDenoisePass&&) noexcept;
    RuntimeRayDiffuseGiDenoisePass& operator=(RuntimeRayDiffuseGiDenoisePass&&) noexcept;

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
        render::Texture& radiance,
        render::Texture& camera_depth,
        render::Texture& surface_guide,
        const RuntimeRayDiffuseGiDenoiseConstants& constants,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] render::Texture* radiance() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
