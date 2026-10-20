#pragma once

#include <vice/math/math.hpp>
#include <vice/render/mesh_asset.hpp>
#include <vice/render/rhi.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace vice::runtime {

struct RuntimeBoundingSphere {
    math::Vec3 center{};
    float radius = 0.0F;
};

struct RuntimeVisibilityPlane {
    math::Vec3 normal{};
    float distance = 0.0F;
};

struct RuntimeVisibilityFrustum {
    std::array<RuntimeVisibilityPlane, 6U> planes{};
};

struct RuntimeVisibilitySphere {
    math::Vec3 center{};
    float radius = 0.0F;
    std::uint32_t instance_index = 0U;
};

[[nodiscard]] bool valid(const RuntimeBoundingSphere& sphere) noexcept;
[[nodiscard]] bool valid(const RuntimeVisibilityPlane& plane) noexcept;
[[nodiscard]] bool valid(const RuntimeVisibilityFrustum& frustum) noexcept;
[[nodiscard]] bool valid(const RuntimeVisibilitySphere& sphere) noexcept;

[[nodiscard]] RuntimeBoundingSphere runtime_mesh_bounding_sphere(
    std::span<const render::MeshVertex> vertices
) noexcept;

[[nodiscard]] RuntimeBoundingSphere runtime_transform_bounding_sphere(
    const RuntimeBoundingSphere& sphere,
    const math::Mat4& transform
) noexcept;

[[nodiscard]] RuntimeVisibilityFrustum runtime_visibility_frustum(
    math::Vec3 forward,
    math::Vec3 up,
    float vertical_fov_radians,
    float aspect_ratio,
    float near_plane,
    float far_plane
) noexcept;

[[nodiscard]] bool runtime_visibility_intersects(
    const RuntimeVisibilityFrustum& frustum,
    const RuntimeVisibilitySphere& sphere
) noexcept;

struct RuntimeVisibilityGpuResult {
    render::Buffer* visible_count = nullptr;
    render::Buffer* visible_indices = nullptr;
    std::uint32_t capacity = 0U;
};

class RuntimeVisibilityCuller final {
public:
    RuntimeVisibilityCuller();
    ~RuntimeVisibilityCuller();

    RuntimeVisibilityCuller(const RuntimeVisibilityCuller&) = delete;
    RuntimeVisibilityCuller& operator=(const RuntimeVisibilityCuller&) = delete;
    RuntimeVisibilityCuller(RuntimeVisibilityCuller&&) noexcept;
    RuntimeVisibilityCuller& operator=(RuntimeVisibilityCuller&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool cull(
        render::CommandList& commands,
        std::span<const RuntimeVisibilitySphere> spheres,
        const RuntimeVisibilityFrustum& frustum,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeVisibilityGpuResult result() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
