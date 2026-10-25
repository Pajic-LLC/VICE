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
inline constexpr float runtime_ray_reflection_history_depth_threshold=0.0025F;
struct RuntimeRayReflectionHistoryProjection{float u=0.0F;float v=0.0F;float device_depth=0.0F;};
struct RuntimeRayReflectionHistoryConstants{math::Mat4 previous_view_projection{};float target_size_history[4]{};float current_to_previous_origin[4]{};};
static_assert(sizeof(RuntimeRayReflectionHistoryConstants)==96U);
[[nodiscard]] std::optional<RuntimeRayReflectionHistoryProjection> runtime_ray_reflection_history_projection(const math::Mat4&,math::Vec3) noexcept;
[[nodiscard]] inline std::optional<math::Vec3> runtime_ray_reflection_history_rebase_position(const math::Vec3 p,const world::GlobalPosition current,const world::GlobalPosition previous) noexcept {
 if(!world::finite(current)||!world::finite(previous)||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)) return std::nullopt;
 const double dx=current.x-previous.x,dy=current.y-previous.y,dz=current.z-previous.z;
 constexpr double limit=static_cast<double>(std::numeric_limits<float>::max());
 const double x=static_cast<double>(p.x)+dx,y=static_cast<double>(p.y)+dy,z=static_cast<double>(p.z)+dz;
 if(std::abs(x)>limit||std::abs(y)>limit||std::abs(z)>limit) return std::nullopt;
 const math::Vec3 result{static_cast<float>(x),static_cast<float>(y),static_cast<float>(z)};
 return std::isfinite(result.x)&&std::isfinite(result.y)&&std::isfinite(result.z)?std::optional<math::Vec3>{result}:std::nullopt;
}
[[nodiscard]] bool runtime_ray_reflection_history_depth_matches(float,float,float depth_threshold=runtime_ray_reflection_history_depth_threshold) noexcept;
[[nodiscard]] std::optional<RuntimeRayReflectionHistoryConstants> make_runtime_ray_reflection_history_constants(const math::Mat4&,std::uint32_t,std::uint32_t,bool,float depth_threshold=runtime_ray_reflection_history_depth_threshold) noexcept;
[[nodiscard]] inline std::optional<RuntimeRayReflectionHistoryConstants> make_runtime_ray_reflection_history_constants(const math::Mat4& matrix,const std::uint32_t width,const std::uint32_t height,const bool valid,const world::GlobalPosition current,const world::GlobalPosition previous,const float threshold=runtime_ray_reflection_history_depth_threshold) noexcept {
 auto constants=make_runtime_ray_reflection_history_constants(matrix,width,height,valid,threshold); if(!constants) return std::nullopt;
 const auto delta=runtime_ray_reflection_history_rebase_position({},current,previous); if(!delta) return std::nullopt;
 constants->current_to_previous_origin[0]=delta->x; constants->current_to_previous_origin[1]=delta->y; constants->current_to_previous_origin[2]=delta->z; return constants;
}
class RuntimeRayReflectionHistoryPass final{public:RuntimeRayReflectionHistoryPass();~RuntimeRayReflectionHistoryPass();RuntimeRayReflectionHistoryPass(const RuntimeRayReflectionHistoryPass&)=delete;RuntimeRayReflectionHistoryPass& operator=(const RuntimeRayReflectionHistoryPass&)=delete;RuntimeRayReflectionHistoryPass(RuntimeRayReflectionHistoryPass&&) noexcept;RuntimeRayReflectionHistoryPass& operator=(RuntimeRayReflectionHistoryPass&&) noexcept;[[nodiscard]] bool initialize(render::Device&,const std::filesystem::path&,std::string*=nullptr);[[nodiscard]] bool dispatch(render::CommandList&,render::Texture&,render::Buffer&,render::Texture&,render::Texture&,const RuntimeRayReflectionHistoryConstants&,std::string*=nullptr);[[nodiscard]] bool available() const noexcept;private:struct State;std::unique_ptr<State> state_;};
} // namespace vice::runtime
