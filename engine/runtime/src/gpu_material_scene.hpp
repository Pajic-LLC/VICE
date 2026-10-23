#pragma once

#include "gpu_material_resources.hpp"

#include <vice/runtime/gpu_instance_materials.hpp>
#include <vice/runtime/gpu_material_table.hpp>

#include <span>
#include <string>

namespace vice::runtime::detail {

class RuntimeMaterialLibrary;

struct RuntimeGpuMaterialSceneView {
    const RuntimeGpuMaterialTablePlan* plan = nullptr;
    RuntimeGpuMaterialTableResult materials{};
    RuntimeGpuInstanceMaterialTableResult instance_materials{};
    RuntimeGpuMaterialResourceTableView resources{};
};

class RuntimeGpuMaterialSceneResources final {
public:
    RuntimeGpuMaterialSceneResources() = default;
    ~RuntimeGpuMaterialSceneResources() = default;

    RuntimeGpuMaterialSceneResources(const RuntimeGpuMaterialSceneResources&) = delete;
    RuntimeGpuMaterialSceneResources& operator=(const RuntimeGpuMaterialSceneResources&) = delete;
    RuntimeGpuMaterialSceneResources(RuntimeGpuMaterialSceneResources&&) noexcept = default;
    RuntimeGpuMaterialSceneResources& operator=(RuntimeGpuMaterialSceneResources&&) noexcept = default;

    [[nodiscard]] bool initialize(
        render::Device& device,
        RuntimeGpuMaterialTablePlan plan,
        const RuntimeTextureLibrary& texture_library,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool initialize(
        render::Device& device,
        RuntimeGpuMaterialTablePlan plan,
        const RuntimeMaterialLibrary& material_library,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool refresh_resources(
        render::Device& device,
        const RuntimeMaterialLibrary& material_library,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool upload(
        render::CommandList& commands,
        std::span<const RenderMeshInstance> instances,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeGpuMaterialSceneView view() const noexcept;

private:
    RuntimeGpuMaterialTablePlan plan_;
    RuntimeGpuMaterialTable materials_;
    RuntimeGpuInstanceMaterialTable instance_materials_;
    RuntimeGpuMaterialResourceTable resources_;
    bool initialized_ = false;
};

} // namespace vice::runtime::detail
