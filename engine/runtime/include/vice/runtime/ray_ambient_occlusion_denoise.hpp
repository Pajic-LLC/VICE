#pragma once

#include <vice/render/rhi.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vice::runtime {

struct RuntimeRayAmbientOcclusionDenoiseConstants {
    float target_size_near_far[4]{};
    float filter_parameters[4]{};
};

static_assert(sizeof(RuntimeRayAmbientOcclusionDenoiseConstants) == 32U);

[[nodiscard]] float runtime_ray_ambient_occlusion_bilateral_weight(
    float center_device_depth,
    float neighbor_device_depth,
    float pixel_distance_squared,
    float near_plane,
    float far_plane,
    float depth_sigma = 0.02F,
    float spatial_sigma = 1.25F
) noexcept;

[[nodiscard]] std::optional<RuntimeRayAmbientOcclusionDenoiseConstants>
make_runtime_ray_ambient_occlusion_denoise_constants(
    std::uint32_t width,
    std::uint32_t height,
    float near_plane,
    float far_plane,
    float depth_sigma = 0.02F,
    float spatial_sigma = 1.25F,
    std::uint32_t kernel_radius = 2U
) noexcept;

class RuntimeRayAmbientOcclusionDenoisePass final {
public:
    RuntimeRayAmbientOcclusionDenoisePass();
    ~RuntimeRayAmbientOcclusionDenoisePass();

    RuntimeRayAmbientOcclusionDenoisePass(const RuntimeRayAmbientOcclusionDenoisePass&) = delete;
    RuntimeRayAmbientOcclusionDenoisePass& operator=(const RuntimeRayAmbientOcclusionDenoisePass&) = delete;
    RuntimeRayAmbientOcclusionDenoisePass(RuntimeRayAmbientOcclusionDenoisePass&&) noexcept;
    RuntimeRayAmbientOcclusionDenoisePass& operator=(RuntimeRayAmbientOcclusionDenoisePass&&) noexcept;

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
        render::Texture& raw_visibility,
        render::Texture& camera_depth,
        const RuntimeRayAmbientOcclusionDenoiseConstants& constants,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] render::Texture* visibility() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
