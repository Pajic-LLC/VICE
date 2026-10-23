#pragma once

#include "gpu_material_resources.hpp"

#include <vice/runtime/gpu_instance_materials.hpp>
#include <vice/runtime/gpu_material_table.hpp>
#include <vice/runtime/gpu_virtual_texture_feedback.hpp>

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

    [[nodiscard]] bool bind_virtual_texture_feedback(
        render::CommandList& commands
    ) noexcept;

    [[nodiscard]] bool capture_virtual_texture_feedback(
        render::CommandList& commands
    ) noexcept;

    [[nodiscard]] bool commit_virtual_texture_feedback(
        render::SubmissionId submission
    ) noexcept;

    [[nodiscard]] RuntimeGpuMaterialSceneView view() const noexcept;

private:
    RuntimeGpuMaterialTablePlan plan_;
    RuntimeGpuMaterialTable materials_;
    RuntimeGpuInstanceMaterialTable instance_materials_;
    RuntimeGpuMaterialResourceTable resources_;
    RuntimeGpuVirtualTextureFeedback virtual_texture_feedback_;
    render::Device* device_ = nullptr;
    const RuntimeMaterialLibrary* material_library_ = nullptr;
    bool feedback_bind_pending_ = false;
    bool feedback_bound_ = false;
    bool feedback_captured_ = false;
    bool initialized_ = false;
};

[[nodiscard]] bool runtime_gpu_material_buffer_binding_hook(
    render::CommandList& commands,
    const render::ShaderBindingKey& binding
) noexcept;

[[nodiscard]] bool runtime_gpu_material_end_render_pass_hook(
    render::CommandList& commands
) noexcept;

[[nodiscard]] bool runtime_gpu_material_submission_hook(
    render::SubmissionId submission
) noexcept;

} // namespace vice::runtime::detail

// scene.cpp intentionally compiles scene_legacy.inc behind small token-level RHI hooks.
// When this private header is parsed under that wrapper, extend the same bridge to
// virtual-texture feedback without changing the legacy renderer's public surface.
#if defined(render)
#define set_graphics_buffer(binding, ...) \
    set_graphics_buffer(binding, __VA_ARGS__) && \
    ::vice::runtime::detail::runtime_gpu_material_buffer_binding_hook(*commands, (binding))
#define end_render_pass(...) \
    end_render_pass(__VA_ARGS__) && \
    ::vice::runtime::detail::runtime_gpu_material_end_render_pass_hook(*commands)
#define wait_for_submission(submission) \
    wait_for_submission(submission) && \
    ::vice::runtime::detail::runtime_gpu_material_submission_hook((submission))
#endif
