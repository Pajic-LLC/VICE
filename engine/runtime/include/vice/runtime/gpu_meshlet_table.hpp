#pragma once

#include <vice/render/rhi.hpp>
#include <vice/runtime/scene.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeGpuMeshRecord {
    std::uint32_t first_meshlet = 0U;
    std::uint32_t meshlet_count = 0U;
    std::uint32_t reserved[2]{};
};

static_assert(sizeof(RuntimeGpuMeshRecord) == 16U);

struct RuntimeGpuMeshletRecord {
    float bounds_center_radius[4]{};
    std::uint32_t vertex_offset = 0U;
    std::uint32_t vertex_count = 0U;
    std::uint32_t triangle_offset = 0U;
    std::uint32_t triangle_count = 0U;
    std::uint32_t source_first_index = 0U;
    std::uint32_t source_index_count = 0U;
    std::uint32_t mesh_index = 0U;
    std::uint32_t reserved = 0U;
};

static_assert(sizeof(RuntimeGpuMeshletRecord) == 48U);

struct RuntimeGpuMeshletTablePlan {
    std::vector<RuntimeGpuMeshRecord> meshes;
    std::vector<RuntimeGpuMeshletRecord> meshlets;
    std::vector<std::uint32_t> vertex_indices;
    std::vector<std::uint32_t> triangle_indices;
    std::vector<world::ResourceId> mesh_ids;

    [[nodiscard]] std::optional<std::uint32_t> mesh_index(
        world::ResourceId resource_id
    ) const noexcept;
};

struct RuntimeGpuMeshletTableBuildResult {
    RuntimeGpuMeshletTablePlan plan;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeGpuMeshletTableBuildResult build_runtime_gpu_meshlet_table_plan(
    std::span<const CookedMeshRecord> meshes
);

struct RuntimeGpuMeshletTableResult {
    render::Buffer* meshes = nullptr;
    render::Buffer* meshlets = nullptr;
    render::Buffer* vertex_indices = nullptr;
    render::Buffer* triangle_indices = nullptr;
    std::uint32_t mesh_count = 0U;
    std::uint32_t meshlet_count = 0U;
    std::uint32_t vertex_index_count = 0U;
    std::uint32_t triangle_index_count = 0U;
};

class RuntimeGpuMeshletTable final {
public:
    RuntimeGpuMeshletTable();
    ~RuntimeGpuMeshletTable();

    RuntimeGpuMeshletTable(const RuntimeGpuMeshletTable&) = delete;
    RuntimeGpuMeshletTable& operator=(const RuntimeGpuMeshletTable&) = delete;
    RuntimeGpuMeshletTable(RuntimeGpuMeshletTable&&) noexcept;
    RuntimeGpuMeshletTable& operator=(RuntimeGpuMeshletTable&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool upload(
        render::CommandList& commands,
        const RuntimeGpuMeshletTablePlan& plan,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeGpuMeshletTableResult result() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
