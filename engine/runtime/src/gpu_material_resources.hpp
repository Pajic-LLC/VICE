#pragma once

#include "texture_pipeline.hpp"

#include <vice/runtime/gpu_material_table.hpp>

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime::detail {

class RuntimeMaterialLibrary;

struct RuntimeGpuMaterialResourceTableView {
    std::span<render::Texture* const> textures;
    std::span<render::Sampler* const> samplers;
};

class RuntimeGpuMaterialResourceTable final {
public:
    RuntimeGpuMaterialResourceTable() = default;
    ~RuntimeGpuMaterialResourceTable() = default;

    RuntimeGpuMaterialResourceTable(const RuntimeGpuMaterialResourceTable&) = delete;
    RuntimeGpuMaterialResourceTable& operator=(const RuntimeGpuMaterialResourceTable&) = delete;
    RuntimeGpuMaterialResourceTable(RuntimeGpuMaterialResourceTable&&) noexcept = default;
    RuntimeGpuMaterialResourceTable& operator=(RuntimeGpuMaterialResourceTable&&) noexcept = default;

    [[nodiscard]] bool initialize(
        render::Device& device,
        const RuntimeGpuMaterialTablePlan& plan,
        const RuntimeTextureLibrary& texture_library,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool initialize(
        render::Device& device,
        const RuntimeGpuMaterialTablePlan& plan,
        const RuntimeMaterialLibrary& material_library,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeGpuMaterialResourceTableView view() const noexcept;

private:
    void clear() noexcept;

    std::vector<render::Texture*> textures_;
    std::vector<std::unique_ptr<render::Sampler>> owned_samplers_;
    std::vector<render::Sampler*> samplers_;
};

} // namespace vice::runtime::detail
