#pragma once

#include <vice/math/math.hpp>
#include <vice/render/rhi.hpp>
#include <vice/world/spatial.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace vice::runtime {

inline constexpr float runtime_ray_reflection_history_depth_threshold = 0.0025F;

struct RuntimeRayReflectionHistoryProjection {
    float u = 0.0F;
    float v = 0.0F;
    float device_depth = 0.0F;
};

struct RuntimeRayReflectionHistoryConstants {
    math::Mat4 previous_view_projection{};
    float target_size_history[4]{};
    float current_to_previous_origin[4]{};
};

static_assert(sizeof(RuntimeRayReflectionHistoryConstants) == 96U);

[[nodiscard]] std::optional<RuntimeRayReflectionHistoryProjection>
runtime_ray_reflection_history_projection(
    const math::Mat4& previous_view_projection,
    math::Vec3 world_position
) noexcept;

[[nodiscard]] bool runtime_ray_reflection_history_depth_matches(
    float expected_device_depth,
    float sampled_device_depth,
    float depth_threshold = runtime_ray_reflection_history_depth_threshold
) noexcept;

[[nodiscard]] inline std::optional<math::Vec3>
runtime_ray_reflection_history_rebase_position(
    const math::Vec3 position,
    const world::GlobalPosition current_origin,
    const world::GlobalPosition previous_origin
) noexcept {
    if (!world::finite(current_origin) || !world::finite(previous_origin) ||
        !std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z)) {
        return std::nullopt;
    }

    const double x = static_cast<double>(position.x) +
        (current_origin.x - previous_origin.x);
    const double y = static_cast<double>(position.y) +
        (current_origin.y - previous_origin.y);
    const double z = static_cast<double>(position.z) +
        (current_origin.z - previous_origin.z);
    constexpr double float_limit =
        static_cast<double>(std::numeric_limits<float>::max());
    if (std::abs(x) > float_limit || std::abs(y) > float_limit ||
        std::abs(z) > float_limit) {
        return std::nullopt;
    }

    const math::Vec3 rebased{
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(z),
    };
    if (!std::isfinite(rebased.x) || !std::isfinite(rebased.y) ||
        !std::isfinite(rebased.z)) {
        return std::nullopt;
    }
    return rebased;
}

[[nodiscard]] std::optional<RuntimeRayReflectionHistoryConstants>
make_runtime_ray_reflection_history_constants(
    const math::Mat4& previous_view_projection,
    std::uint32_t width,
    std::uint32_t height,
    bool history_valid,
    float depth_threshold = runtime_ray_reflection_history_depth_threshold
) noexcept;

[[nodiscard]] inline std::optional<RuntimeRayReflectionHistoryConstants>
make_runtime_ray_reflection_history_constants(
    const math::Mat4& previous_view_projection,
    const std::uint32_t width,
    const std::uint32_t height,
    const bool history_valid,
    const world::GlobalPosition current_origin,
    const world::GlobalPosition previous_origin,
    const float depth_threshold = runtime_ray_reflection_history_depth_threshold
) noexcept {
    auto constants = make_runtime_ray_reflection_history_constants(
        previous_view_projection,
        width,
        height,
        history_valid,
        depth_threshold
    );
    if (!constants.has_value()) return std::nullopt;

    const auto delta = runtime_ray_reflection_history_rebase_position(
        {},
        current_origin,
        previous_origin
    );
    if (!delta.has_value()) return std::nullopt;
    constants->current_to_previous_origin[0] = delta->x;
    constants->current_to_previous_origin[1] = delta->y;
    constants->current_to_previous_origin[2] = delta->z;
    return constants;
}

class RuntimeRayReflectionHistoryPass final {
public:
    RuntimeRayReflectionHistoryPass();
    ~RuntimeRayReflectionHistoryPass();

    RuntimeRayReflectionHistoryPass(const RuntimeRayReflectionHistoryPass&) = delete;
    RuntimeRayReflectionHistoryPass& operator=(const RuntimeRayReflectionHistoryPass&) = delete;
    RuntimeRayReflectionHistoryPass(RuntimeRayReflectionHistoryPass&&) noexcept;
    RuntimeRayReflectionHistoryPass& operator=(RuntimeRayReflectionHistoryPass&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        const std::filesystem::path& shader_manifest,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool dispatch(
        render::CommandList& commands,
        render::Texture& reflection_radiance,
        render::Buffer& reflection_hits,
        render::Texture& previous_scene_color,
        render::Texture& previous_depth,
        const RuntimeRayReflectionHistoryConstants& constants,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool available() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
