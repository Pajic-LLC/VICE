#pragma once

#include <vice/math/math.hpp>
#include <vice/render/rhi.hpp>
#include <vice/world/spatial.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vice::runtime {
inline constexpr float runtime_ray_reflection_history_depth_threshold = 0.0025F;
struct RuntimeRayReflectionHistoryProjection { float u=0.0F; float v=0.0F; float device_depth=0.0F; };
struct RuntimeRayReflectionHistoryConstants {
    math::Mat4 previous_view_projection{};
    float target_size_history[4]{};
    float current_to_previous_origin[4]{};
};
static_assert(sizeof(RuntimeRayReflectionHistoryConstants) == 96U);
[[nodiscard]] std::optional<RuntimeRayReflectionHistoryProjection> runtime_ray_reflection_history_projection(const math::Mat4& previous_view_projection, math::Vec3 world_position) noexcept;
[[nodiscard]] std::optional<math::Vec3> runtime_ray_reflection_history_rebase_position(math::Vec3 current_relative_position, world::GlobalPosition current_origin, world::GlobalPosition previous_origin) noexcept;
[[nodiscard]] bool runtime_ray_reflection_history_depth_matches(float expected_device_depth,float sampled_device_depth,float depth_threshold=runtime_ray_reflection_history_depth_threshold) noexcept;
[[nodiscard]] std::optional<RuntimeRayReflectionHistoryConstants> make_runtime_ray_reflection_history_constants(const math::Mat4& previous_view_projection,std::uint32_t width,std::uint32_t height,bool history_valid,float depth_threshold=runtime_ray_reflection_history_depth_threshold,world::GlobalPosition current_origin={},world::GlobalPosition previous_origin={}) noexcept;
class RuntimeRayReflectionHistoryPass final {
public:
 RuntimeRayReflectionHistoryPass(); ~RuntimeRayReflectionHistoryPass(); RuntimeRayReflectionHistoryPass(const RuntimeRayReflectionHistoryPass&)=delete; RuntimeRayReflectionHistoryPass& operator=(const RuntimeRayReflectionHistoryPass&)=delete; RuntimeRayReflectionHistoryPass(RuntimeRayReflectionHistoryPass&&) noexcept; RuntimeRayReflectionHistoryPass& operator=(RuntimeRayReflectionHistoryPass&&) noexcept;
 [[nodiscard]] bool initialize(render::Device& device,const std::filesystem::path& shader_manifest,std::string* diagnostics=nullptr);
 [[nodiscard]] bool dispatch(render::CommandList& commands,render::Texture& reflection_radiance,render::Buffer& reflection_hits,render::Texture& previous_scene_color,render::Texture& previous_depth,const RuntimeRayReflectionHistoryConstants& constants,std::string* diagnostics=nullptr);
 [[nodiscard]] bool available() const noexcept;
private: struct State; std::unique_ptr<State> state_;
};
} // namespace vice::runtime
