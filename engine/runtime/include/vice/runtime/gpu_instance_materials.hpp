#pragma once

#include <vice/render/rhi.hpp>
#include <vice/runtime/gpu_material_table.hpp>
#include <vice/runtime/scene.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeGpuInstanceMaterialRecord {
    std::uint32_t material_index = runtime_gpu_resource_unassigned;
    std::uint32_t reserved[3]{};
};

static_assert(sizeof(RuntimeGpuInstanceMaterialRecord) == 16U);

struct RuntimeGpuInstanceMaterialBuildResult {
    std::vector<RuntimeGpuInstanceMaterialRecord> records;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeGpuInstanceMaterialBuildResult build_runtime_gpu_instance_material_records(
    std::span<const RenderMeshInstance> instances,
    const RuntimeGpuMaterialTablePlan& materials
);

struct RuntimeGpuInstanceMaterialTableResult {
    render::Buffer* records = nullptr;
    std::uint32_t instance_count = 0U;
    std::uint32_t capacity = 0U;
};

class RuntimeGpuInstanceMaterialTable final {
public:
    RuntimeGpuInstanceMaterialTable();
    ~RuntimeGpuInstanceMaterialTable();

    RuntimeGpuInstanceMaterialTable(const RuntimeGpuInstanceMaterialTable&) = delete;
    RuntimeGpuInstanceMaterialTable& operator=(const RuntimeGpuInstanceMaterialTable&) = delete;
    RuntimeGpuInstanceMaterialTable(RuntimeGpuInstanceMaterialTable&&) noexcept;
    RuntimeGpuInstanceMaterialTable& operator=(RuntimeGpuInstanceMaterialTable&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool upload(
        render::CommandList& commands,
        std::span<const RuntimeGpuInstanceMaterialRecord> records,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeGpuInstanceMaterialTableResult result() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
