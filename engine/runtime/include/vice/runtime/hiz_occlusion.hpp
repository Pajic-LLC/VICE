#pragma once

#include <vice/math/math.hpp>
#include <vice/runtime/hiz.hpp>
#include <vice/runtime/visibility.hpp>

#include <memory>
#include <span>
#include <string>

namespace vice::runtime {

inline constexpr std::uint32_t runtime_hiz_occlusion_max_levels = 16U;

class RuntimeHiZOcclusionCuller final {
public:
    RuntimeHiZOcclusionCuller();
    ~RuntimeHiZOcclusionCuller();

    RuntimeHiZOcclusionCuller(const RuntimeHiZOcclusionCuller&) = delete;
    RuntimeHiZOcclusionCuller& operator=(const RuntimeHiZOcclusionCuller&) = delete;
    RuntimeHiZOcclusionCuller(RuntimeHiZOcclusionCuller&&) noexcept;
    RuntimeHiZOcclusionCuller& operator=(RuntimeHiZOcclusionCuller&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool cull(
        render::CommandList& commands,
        const RuntimeVisibilityGpuResult& frustum_visibility,
        std::span<const RuntimeVisibilitySphere> spheres,
        const math::Mat4& view_projection,
        float near_plane,
        float far_plane,
        const RuntimeHiZPyramid& pyramid,
        float depth_bias = 0.001F,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeVisibilityGpuResult result() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
