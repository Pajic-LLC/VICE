#pragma once

#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/scene.hpp>
#include <vice/runtime/visibility.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeGpuVirtualGeometryNodeRecord {
    float bounds_center_radius[4]{};
    float geometric_error = 0.0F;
    std::uint32_t parent_index = render::virtual_geometry_invalid_node;
    std::uint32_t first_child = render::virtual_geometry_invalid_node;
    std::uint32_t child_count = 0U;
    std::uint32_t page_offset = 0U;
    std::uint32_t page_count = 0U;
    std::uint32_t depth = 0U;
    std::uint32_t mesh_index = 0U;
    std::uint32_t local_node_index = render::virtual_geometry_invalid_node;
    std::uint32_t reserved[3]{};

    friend bool operator==(
        const RuntimeGpuVirtualGeometryNodeRecord&,
        const RuntimeGpuVirtualGeometryNodeRecord&
    ) = default;
};

static_assert(sizeof(RuntimeGpuVirtualGeometryNodeRecord) == 64U);

struct RuntimeGpuVirtualGeometryCandidate {
    std::uint32_t scene_instance_index = 0U;
    std::uint32_t node_index = render::virtual_geometry_invalid_node;
    std::uint32_t mesh_index = 0U;
    std::uint32_t local_node_index = render::virtual_geometry_invalid_node;

    friend bool operator==(
        const RuntimeGpuVirtualGeometryCandidate&,
        const RuntimeGpuVirtualGeometryCandidate&
    ) = default;
};

static_assert(sizeof(RuntimeGpuVirtualGeometryCandidate) == 16U);

struct RuntimeGpuVirtualGeometryNodeRequest {
    std::uint32_t scene_instance_index = 0U;
    std::uint32_t mesh_index = 0U;
    std::uint32_t node_index = render::virtual_geometry_invalid_node;
    std::uint32_t reserved = 0U;

    friend bool operator==(
        const RuntimeGpuVirtualGeometryNodeRequest&,
        const RuntimeGpuVirtualGeometryNodeRequest&
    ) = default;
};

static_assert(sizeof(RuntimeGpuVirtualGeometryNodeRequest) == 16U);

struct RuntimeGpuVirtualGeometryPageRequest {
    std::uint32_t scene_instance_index = 0U;
    std::uint32_t mesh_index = 0U;
    std::uint32_t node_index = render::virtual_geometry_invalid_node;
    std::uint32_t page_index = 0U;

    friend bool operator==(
        const RuntimeGpuVirtualGeometryPageRequest&,
        const RuntimeGpuVirtualGeometryPageRequest&
    ) = default;
};

static_assert(sizeof(RuntimeGpuVirtualGeometryPageRequest) == 16U);

struct RuntimeGpuVirtualGeometryPlan {
    std::vector<RuntimeGpuVirtualGeometryNodeRecord> nodes;
    std::vector<RuntimeGpuVirtualGeometryCandidate> candidates;
    std::vector<world::ResourceId> mesh_ids;
    std::uint32_t scene_instance_count = 0U;

    [[nodiscard]] std::optional<std::uint32_t> mesh_index(
        world::ResourceId resource_id
    ) const noexcept;

    friend bool operator==(
        const RuntimeGpuVirtualGeometryPlan&,
        const RuntimeGpuVirtualGeometryPlan&
    ) = default;
};

struct RuntimeGpuVirtualGeometryPlanBuildResult {
    RuntimeGpuVirtualGeometryPlan plan;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeGpuVirtualGeometryPlanBuildResult build_runtime_gpu_virtual_geometry_plan(
    std::span<const RenderMeshInstance> instances,
    std::span<const CookedMeshRecord> meshes
);

struct RuntimeGpuVirtualGeometrySelectionResult {
    render::Buffer* node_request_count = nullptr;
    render::Buffer* node_requests = nullptr;
    render::Buffer* page_request_count = nullptr;
    render::Buffer* page_requests = nullptr;
    std::uint32_t request_capacity = 0U;
    std::uint32_t candidate_count = 0U;
};

class RuntimeGpuVirtualGeometrySelector final {
public:
    RuntimeGpuVirtualGeometrySelector();
    ~RuntimeGpuVirtualGeometrySelector();

    RuntimeGpuVirtualGeometrySelector(const RuntimeGpuVirtualGeometrySelector&) = delete;
    RuntimeGpuVirtualGeometrySelector& operator=(const RuntimeGpuVirtualGeometrySelector&) = delete;
    RuntimeGpuVirtualGeometrySelector(RuntimeGpuVirtualGeometrySelector&&) noexcept;
    RuntimeGpuVirtualGeometrySelector& operator=(RuntimeGpuVirtualGeometrySelector&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool select(
        render::CommandList& commands,
        const RuntimeGpuVirtualGeometryPlan& plan,
        const RuntimeGpuInstanceTableResult& camera_relative_instances,
        const RuntimeVisibilityFrustum& frustum,
        float vertical_fov_radians,
        float viewport_height_pixels,
        float max_projected_error_pixels,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeGpuVirtualGeometrySelectionResult result() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
