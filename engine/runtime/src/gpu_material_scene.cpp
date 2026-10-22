#include "gpu_material_scene.hpp"
#include "material_pipeline.hpp"

#include <utility>

namespace vice::runtime::detail {
namespace {

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

} // namespace

bool RuntimeGpuMaterialSceneResources::initialize(
    render::Device& device,
    RuntimeGpuMaterialTablePlan plan,
    const RuntimeTextureLibrary& texture_library,
    std::string* const diagnostics
) {
    initialized_ = false;
    plan_ = {};
    if (!resources_.initialize(device, plan, texture_library, diagnostics) ||
        !materials_.initialize(device, diagnostics) ||
        !instance_materials_.initialize(device, diagnostics)) {
        return false;
    }
    plan_ = std::move(plan);
    initialized_ = true;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuMaterialSceneResources::initialize(
    render::Device& device,
    RuntimeGpuMaterialTablePlan plan,
    const RuntimeMaterialLibrary& material_library,
    std::string* const diagnostics
) {
    initialized_ = false;
    plan_ = {};
    if (!resources_.initialize(device, plan, material_library, diagnostics) ||
        !materials_.initialize(device, diagnostics) ||
        !instance_materials_.initialize(device, diagnostics)) {
        return false;
    }
    plan_ = std::move(plan);
    initialized_ = true;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuMaterialSceneResources::upload(
    render::CommandList& commands,
    const std::span<const RenderMeshInstance> instances,
    std::string* const diagnostics
) {
    if (!initialized_) {
        set_diagnostics(diagnostics, "Runtime GPU material scene resources are not initialized.");
        return false;
    }

    const RuntimeGpuInstanceMaterialBuildResult instance_records =
        build_runtime_gpu_instance_material_records(instances, plan_);
    if (!instance_records) {
        set_diagnostics(diagnostics, instance_records.diagnostics);
        return false;
    }

    if (!materials_.upload(commands, plan_.records, diagnostics) ||
        !instance_materials_.upload(commands, instance_records.records, diagnostics)) {
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeGpuMaterialSceneView RuntimeGpuMaterialSceneResources::view() const noexcept {
    if (!initialized_) return {};
    return RuntimeGpuMaterialSceneView{
        .plan = &plan_,
        .materials = materials_.result(),
        .instance_materials = instance_materials_.result(),
        .resources = resources_.view(),
    };
}

} // namespace vice::runtime::detail
