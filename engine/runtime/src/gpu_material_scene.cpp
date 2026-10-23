#include "gpu_material_scene.hpp"
#include "material_pipeline.hpp"

#include <cstdint>
#include <utility>

namespace vice::runtime::detail {
namespace {

thread_local RuntimeGpuMaterialSceneResources* active_gpu_material_scene = nullptr;

constexpr render::ShaderBindingKey virtual_texture_feedback_binding{
    .register_class = render::ShaderRegisterClass::unordered_access,
    .binding = 0U,
    .space = 3U,
    .array_element = 0U,
};

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] bool material_record_binding(
    const render::ShaderBindingKey& binding
) noexcept {
    return binding.register_class == render::ShaderRegisterClass::shader_resource &&
        binding.binding == 3U && binding.space == 0U && binding.array_element == 0U;
}

} // namespace

bool RuntimeGpuMaterialSceneResources::initialize(
    render::Device& device,
    RuntimeGpuMaterialTablePlan plan,
    const RuntimeTextureLibrary& texture_library,
    std::string* const diagnostics
) {
    if (active_gpu_material_scene == this) active_gpu_material_scene = nullptr;
    initialized_ = false;
    plan_ = {};
    device_ = nullptr;
    material_library_ = nullptr;
    feedback_bind_pending_ = false;
    feedback_bound_ = false;
    feedback_captured_ = false;
    if (!resources_.initialize(device, plan, texture_library, diagnostics) ||
        !materials_.initialize(device, diagnostics) ||
        !instance_materials_.initialize(device, diagnostics) ||
        !virtual_texture_feedback_.initialize(device, diagnostics)) {
        return false;
    }
    plan_ = std::move(plan);
    device_ = &device;
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
    if (active_gpu_material_scene == this) active_gpu_material_scene = nullptr;
    initialized_ = false;
    plan_ = {};
    device_ = nullptr;
    material_library_ = nullptr;
    feedback_bind_pending_ = false;
    feedback_bound_ = false;
    feedback_captured_ = false;
    if (!resources_.initialize(device, plan, material_library, diagnostics) ||
        !materials_.initialize(device, diagnostics) ||
        !instance_materials_.initialize(device, diagnostics) ||
        !virtual_texture_feedback_.initialize(device, diagnostics)) {
        return false;
    }
    plan_ = std::move(plan);
    device_ = &device;
    material_library_ = &material_library;
    initialized_ = true;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuMaterialSceneResources::refresh_resources(
    render::Device& device,
    const RuntimeMaterialLibrary& material_library,
    std::string* const diagnostics
) {
    if (!initialized_) {
        set_diagnostics(diagnostics, "Runtime GPU material scene resources are not initialized.");
        return false;
    }
    device_ = &device;
    material_library_ = &material_library;
    return resources_.initialize(device, plan_, material_library, diagnostics);
}

bool RuntimeGpuMaterialSceneResources::upload(
    render::CommandList& commands,
    const std::span<const RenderMeshInstance> instances,
    std::string* const diagnostics
) {
    if (!initialized_ || device_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU material scene resources are not initialized.");
        return false;
    }

    virtual_texture_feedback_.cancel();
    feedback_bind_pending_ = false;
    feedback_bound_ = false;
    feedback_captured_ = false;
    if (active_gpu_material_scene == this) active_gpu_material_scene = nullptr;

    auto feedback = virtual_texture_feedback_.poll();
    if (!feedback) {
        set_diagnostics(diagnostics, feedback.diagnostics);
        return false;
    }
    if (!feedback.requests.empty() && material_library_ != nullptr &&
        !resources_.apply_virtual_texture_feedback(
            *device_, plan_, *material_library_, feedback.requests, diagnostics
        )) {
        return false;
    }
    if (!virtual_texture_feedback_.prepare(
            commands, resources_.virtual_sampling_plan(), diagnostics
        )) {
        return false;
    }

    const RuntimeGpuInstanceMaterialBuildResult instance_records =
        build_runtime_gpu_instance_material_records(instances, plan_);
    if (!instance_records) {
        virtual_texture_feedback_.cancel();
        set_diagnostics(diagnostics, instance_records.diagnostics);
        return false;
    }

    const auto gpu_records = resources_.material_records();
    if (gpu_records.size() != plan_.records.size()) {
        virtual_texture_feedback_.cancel();
        set_diagnostics(diagnostics, "Runtime GPU material resource record table is misaligned.");
        return false;
    }
    if (!materials_.upload(commands, gpu_records, diagnostics) ||
        !instance_materials_.upload(commands, instance_records.records, diagnostics)) {
        virtual_texture_feedback_.cancel();
        return false;
    }

    feedback_bind_pending_ = true;
    active_gpu_material_scene = this;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuMaterialSceneResources::bind_virtual_texture_feedback(
    render::CommandList& commands
) noexcept {
    if (!initialized_ || !feedback_bind_pending_) return true;
    render::Buffer* const buffer = virtual_texture_feedback_.binding_buffer();
    if (buffer == nullptr || buffer->size() < sizeof(std::uint32_t)) return false;
    const render::BufferViewDesc view{
        .offset = 0U,
        .size = buffer->size(),
        .structure_stride = sizeof(std::uint32_t),
    };
    if (!commands.set_graphics_storage_buffer(
            virtual_texture_feedback_binding, *buffer, view
        )) {
        return false;
    }
    feedback_bind_pending_ = false;
    feedback_bound_ = virtual_texture_feedback_.active();
    return true;
}

bool RuntimeGpuMaterialSceneResources::capture_virtual_texture_feedback(
    render::CommandList& commands
) noexcept {
    if (!initialized_ || !feedback_bound_) return true;
    if (!virtual_texture_feedback_.capture(commands, nullptr)) return false;
    feedback_bound_ = false;
    feedback_captured_ = true;
    return true;
}

bool RuntimeGpuMaterialSceneResources::commit_virtual_texture_feedback(
    const render::SubmissionId submission
) noexcept {
    if (!initialized_) return true;
    bool success = true;
    if (feedback_captured_) {
        success = virtual_texture_feedback_.commit(submission, nullptr);
    } else {
        virtual_texture_feedback_.cancel();
    }
    feedback_bind_pending_ = false;
    feedback_bound_ = false;
    feedback_captured_ = false;
    if (active_gpu_material_scene == this) active_gpu_material_scene = nullptr;
    return success;
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

bool runtime_gpu_material_buffer_binding_hook(
    render::CommandList& commands,
    const render::ShaderBindingKey& binding
) noexcept {
    if (active_gpu_material_scene == nullptr || !material_record_binding(binding)) return true;
    return active_gpu_material_scene->bind_virtual_texture_feedback(commands);
}

bool runtime_gpu_material_end_render_pass_hook(render::CommandList& commands) noexcept {
    if (active_gpu_material_scene == nullptr) return true;
    return active_gpu_material_scene->capture_virtual_texture_feedback(commands);
}

bool runtime_gpu_material_submission_hook(const render::SubmissionId submission) noexcept {
    if (active_gpu_material_scene == nullptr) return true;
    return active_gpu_material_scene->commit_virtual_texture_feedback(submission);
}

} // namespace vice::runtime::detail
