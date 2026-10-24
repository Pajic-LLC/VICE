#pragma once

#include <vice/render/rhi.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vice::runtime {

struct RuntimeRayAmbientOcclusionTemporalConstants {
    float target_size_history[4]{};
    float rejection_parameters[4]{};
};

static_assert(sizeof(RuntimeRayAmbientOcclusionTemporalConstants) == 32U);

[[nodiscard]] float runtime_ray_ambient_occlusion_temporal_history_weight(
    float base_history_weight,
    float velocity_x,
    float velocity_y,
    float motion_rejection,
    bool history_valid
) noexcept;

[[nodiscard]] bool runtime_ray_ambient_occlusion_temporal_depth_accept(
    float current_depth,
    float previous_depth,
    float depth_threshold = 0.0025F
) noexcept;

[[nodiscard]] float runtime_ray_ambient_occlusion_temporal_clamp_history(
    float history_visibility,
    float neighborhood_min,
    float neighborhood_max
) noexcept;

[[nodiscard]] std::optional<RuntimeRayAmbientOcclusionTemporalConstants>
make_runtime_ray_ambient_occlusion_temporal_constants(
    std::uint32_t width,
    std::uint32_t height,
    bool history_valid,
    float base_history_weight = 0.88F,
    float depth_threshold = 0.0025F,
    float motion_rejection = 32.0F
) noexcept;

class RuntimeRayAmbientOcclusionTemporalPass final {
public:
    RuntimeRayAmbientOcclusionTemporalPass();
    ~RuntimeRayAmbientOcclusionTemporalPass();

    RuntimeRayAmbientOcclusionTemporalPass(const RuntimeRayAmbientOcclusionTemporalPass&) = delete;
    RuntimeRayAmbientOcclusionTemporalPass& operator=(const RuntimeRayAmbientOcclusionTemporalPass&) = delete;
    RuntimeRayAmbientOcclusionTemporalPass(RuntimeRayAmbientOcclusionTemporalPass&&) noexcept;
    RuntimeRayAmbientOcclusionTemporalPass& operator=(RuntimeRayAmbientOcclusionTemporalPass&&) noexcept;

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
        render::Texture& current_visibility,
        render::Texture& motion_vectors,
        render::Texture& current_depth,
        render::Texture& previous_depth,
        const RuntimeRayAmbientOcclusionTemporalConstants& constants,
        std::uint64_t frame_index,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] render::Texture* visibility() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
