from pathlib import Path

path = Path("engine/runtime/src/scene.cpp")
text = path.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    text = text.replace(old, new, 1)


replace_once(
    "#include <vice/runtime/gpu_draw_batches.hpp>\n#include <vice/runtime/gpu_instance_table.hpp>\n#include <vice/runtime/gpu_visibility_set.hpp>",
    "#include <vice/runtime/gpu_cluster_instance_remap.hpp>\n#include <vice/runtime/gpu_cluster_visibility.hpp>\n#include <vice/runtime/gpu_draw_batches.hpp>\n#include <vice/runtime/gpu_instance_table.hpp>\n#include <vice/runtime/gpu_meshlet_table.hpp>\n#include <vice/runtime/gpu_visibility_set.hpp>",
    "cluster includes",
)

replace_once(
    "    RuntimeHiZOcclusionCuller hiz_occlusion;\n"
    "    RuntimeGpuIdentityVisibility gpu_shadow_visibility;\n"
    "    RuntimeGpuDrawBatcher gpu_draw_batches;\n"
    "    RuntimeGpuDrawBatcher gpu_pbr_draw_batches;\n"
    "    RuntimeGpuDrawBatcher gpu_shadow_draw_batches;\n"
    "    RuntimeGpuDrawBatcher gpu_motion_draw_batches;\n"
    "    RuntimeGpuInstanceTable gpu_instances;\n"
    "    RuntimeGpuInstanceTable gpu_previous_instances;",
    "    RuntimeHiZOcclusionCuller hiz_occlusion;\n"
    "    RuntimeGpuMeshletTablePlan gpu_meshlet_plan;\n"
    "    RuntimeGpuMeshletTable gpu_meshlet_table;\n"
    "    RuntimeGpuClusterVisibility gpu_cluster_visibility;\n"
    "    RuntimeGpuIdentityVisibility gpu_shadow_visibility;\n"
    "    RuntimeGpuDrawBatcher gpu_draw_batches;\n"
    "    RuntimeGpuDrawBatcher gpu_pbr_draw_batches;\n"
    "    RuntimeGpuDrawBatcher gpu_cluster_default_draw_batches;\n"
    "    RuntimeGpuDrawBatcher gpu_cluster_pbr_draw_batches;\n"
    "    RuntimeGpuDrawBatcher gpu_shadow_draw_batches;\n"
    "    RuntimeGpuDrawBatcher gpu_motion_draw_batches;\n"
    "    RuntimeGpuClusterInstanceRemap gpu_cluster_default_instances;\n"
    "    RuntimeGpuClusterInstanceRemap gpu_cluster_pbr_instances;\n"
    "    RuntimeGpuInstanceTable gpu_instances;\n"
    "    RuntimeGpuInstanceTable gpu_cluster_instances;\n"
    "    RuntimeGpuInstanceTable gpu_previous_instances;",
    "cluster renderer state",
)

replace_once(
    "    if (!state_->catalog.load(content_root)) {\n"
    "        set_diagnostics(diagnostics, state_->catalog.diagnostics());\n"
    "        return false;\n"
    "    }\n"
    "    if (!state_->materials.initialize(",
    "    if (!state_->catalog.load(content_root)) {\n"
    "        set_diagnostics(diagnostics, state_->catalog.diagnostics());\n"
    "        return false;\n"
    "    }\n"
    "    auto gpu_meshlet_build = build_runtime_gpu_meshlet_table_plan(state_->catalog.records());\n"
    "    if (!gpu_meshlet_build) {\n"
    "        set_diagnostics(diagnostics, gpu_meshlet_build.diagnostics);\n"
    "        return false;\n"
    "    }\n"
    "    state_->gpu_meshlet_plan = std::move(gpu_meshlet_build.plan);\n"
    "    if (!state_->materials.initialize(",
    "meshlet plan initialization",
)

replace_once(
    "    if (!state_->visibility.initialize(device, diagnostics) ||\n"
    "        !state_->hiz_pyramid.initialize(device, diagnostics) ||\n"
    "        !state_->hiz_occlusion.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_shadow_visibility.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_draw_batches.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_pbr_draw_batches.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_shadow_draw_batches.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_motion_draw_batches.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_instances.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_previous_instances.initialize(device, diagnostics)) {",
    "    if (!state_->visibility.initialize(device, diagnostics) ||\n"
    "        !state_->hiz_pyramid.initialize(device, diagnostics) ||\n"
    "        !state_->hiz_occlusion.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_meshlet_table.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_cluster_visibility.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_shadow_visibility.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_draw_batches.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_pbr_draw_batches.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_cluster_default_draw_batches.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_cluster_pbr_draw_batches.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_shadow_draw_batches.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_motion_draw_batches.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_cluster_default_instances.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_cluster_pbr_instances.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_instances.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_cluster_instances.initialize(device, diagnostics) ||\n"
    "        !state_->gpu_previous_instances.initialize(device, diagnostics)) {",
    "cluster runtime initialization",
)

replace_once(
    "    for (auto& table : state_->gpu_point_shadow_instances) {\n"
    "        if (!table.initialize(device, diagnostics)) return false;\n"
    "    }\n"
    "    state_->gpu_default_frame = create_gpu_default_frame_buffer(device);",
    "    for (auto& table : state_->gpu_point_shadow_instances) {\n"
    "        if (!table.initialize(device, diagnostics)) return false;\n"
    "    }\n"
    "    auto meshlet_commands = device.create_command_list();\n"
    "    if (!meshlet_commands || !state_->gpu_meshlet_table.upload(\n"
    "            *meshlet_commands, state_->gpu_meshlet_plan, diagnostics\n"
    "        )) {\n"
    "        set_diagnostics(diagnostics, \"VICE Runtime could not upload immutable GPU meshlet metadata.\");\n"
    "        return false;\n"
    "    }\n"
    "    const auto meshlet_submission = device.submit(std::move(meshlet_commands));\n"
    "    if (meshlet_submission == render::invalid_submission ||\n"
    "        !device.wait_for_submission(meshlet_submission)) {\n"
    "        set_diagnostics(diagnostics, \"VICE Runtime GPU meshlet metadata submission failed.\");\n"
    "        return false;\n"
    "    }\n"
    "    state_->gpu_default_frame = create_gpu_default_frame_buffer(device);",
    "meshlet metadata upload",
)

old_plan = '''    std::vector<math::Mat4> gpu_instance_models;
    gpu_instance_models.reserve(instances.size());
    std::vector<std::uint32_t> gpu_batch_assignments(
        instances.size(), runtime_gpu_draw_batch_unassigned
    );
    std::vector<RuntimeGpuDrawBatch> gpu_default_batches;
    std::vector<world::ResourceId> gpu_default_batch_meshes;
    std::unordered_map<world::ResourceId, std::uint32_t, world::ResourceIdHash> gpu_default_batch_lookup;
    for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
        const auto& instance = instances[instance_index];
        gpu_instance_models.push_back(runtime_model_matrix(instance));
        if (instance.material.has_value()) continue;

        auto found = gpu_default_batch_lookup.find(instance.mesh);
        if (found == gpu_default_batch_lookup.end()) {
            const std::uint32_t batch_index = static_cast<std::uint32_t>(gpu_default_batches.size());
            const auto& mesh = state_->meshes.at(instance.mesh);
            gpu_default_batches.push_back(RuntimeGpuDrawBatch{
                .index_count = mesh.index_count,
                .first_index = 0U,
                .vertex_offset = 0,
            });
            gpu_default_batch_meshes.push_back(instance.mesh);
            found = gpu_default_batch_lookup.emplace(instance.mesh, batch_index).first;
        }
        gpu_batch_assignments[instance_index] = found->second;
    }
'''
new_plan = '''    const auto gpu_cluster_candidate_build = build_runtime_gpu_cluster_candidate_plan(
        instances,
        state_->gpu_meshlet_plan
    );
    if (!gpu_cluster_candidate_build) {
        set_diagnostics(diagnostics, gpu_cluster_candidate_build.diagnostics);
        return false;
    }
    const auto& gpu_cluster_candidates = gpu_cluster_candidate_build.plan;
    std::vector<std::uint8_t> gpu_cluster_default_eligible(instances.size(), 0U);
    std::vector<std::uint8_t> gpu_cluster_pbr_eligible(instances.size(), 0U);
    for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
        const auto& instance = instances[instance_index];
        const auto mesh_index = state_->gpu_meshlet_plan.mesh_index(instance.mesh);
        if (!mesh_index.has_value() || *mesh_index >= state_->gpu_meshlet_plan.meshes.size()) {
            set_diagnostics(diagnostics, "Runtime cluster submission could not resolve a mesh table index.");
            return false;
        }
        if (state_->gpu_meshlet_plan.meshes[*mesh_index].meshlet_count == 0U) continue;
        if (!instance.material.has_value()) {
            gpu_cluster_default_eligible[instance_index] = 1U;
            continue;
        }
        const auto* const material = state_->materials.catalog().find(*instance.material);
        if (material != nullptr && render::render_material_uses_gpu_scene_pbr(material->asset.version)) {
            gpu_cluster_pbr_eligible[instance_index] = 1U;
        }
    }
    const auto gpu_cluster_default_build = build_runtime_gpu_cluster_batch_plan(
        gpu_cluster_candidates,
        state_->gpu_meshlet_plan,
        gpu_cluster_default_eligible
    );
    const auto gpu_cluster_pbr_build = build_runtime_gpu_cluster_batch_plan(
        gpu_cluster_candidates,
        state_->gpu_meshlet_plan,
        gpu_cluster_pbr_eligible
    );
    if (!gpu_cluster_default_build || !gpu_cluster_pbr_build) {
        set_diagnostics(
            diagnostics,
            !gpu_cluster_default_build
                ? gpu_cluster_default_build.diagnostics
                : gpu_cluster_pbr_build.diagnostics
        );
        return false;
    }
    const auto& gpu_cluster_default_plan = gpu_cluster_default_build.plan;
    const auto& gpu_cluster_pbr_plan = gpu_cluster_pbr_build.plan;

    std::vector<math::Mat4> gpu_instance_models;
    gpu_instance_models.reserve(instances.size());
    std::vector<math::Mat4> gpu_cluster_models;
    gpu_cluster_models.reserve(instances.size());
    std::vector<std::uint32_t> gpu_batch_assignments(
        instances.size(), runtime_gpu_draw_batch_unassigned
    );
    std::vector<RuntimeGpuDrawBatch> gpu_default_batches;
    std::vector<world::ResourceId> gpu_default_batch_meshes;
    std::unordered_map<world::ResourceId, std::uint32_t, world::ResourceIdHash> gpu_default_batch_lookup;
    for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
        const auto& instance = instances[instance_index];
        gpu_instance_models.push_back(runtime_model_matrix(instance));
        gpu_cluster_models.push_back(runtime_model_matrix_relative_to(instance, camera_global_position));
        if (instance.material.has_value() || gpu_cluster_default_eligible[instance_index] != 0U) continue;

        auto found = gpu_default_batch_lookup.find(instance.mesh);
        if (found == gpu_default_batch_lookup.end()) {
            const std::uint32_t batch_index = static_cast<std::uint32_t>(gpu_default_batches.size());
            const auto& mesh = state_->meshes.at(instance.mesh);
            gpu_default_batches.push_back(RuntimeGpuDrawBatch{
                .index_count = mesh.index_count,
                .first_index = 0U,
                .vertex_offset = 0,
            });
            gpu_default_batch_meshes.push_back(instance.mesh);
            found = gpu_default_batch_lookup.emplace(instance.mesh, batch_index).first;
        }
        gpu_batch_assignments[instance_index] = found->second;
    }
'''
replace_once(old_plan, new_plan, "cluster scene plan")

replace_once(
    "    const auto& gpu_pbr_plan = gpu_pbr_build.plan;",
    "    auto gpu_pbr_plan = std::move(gpu_pbr_build.plan);\n"
    "    for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {\n"
    "        if (gpu_cluster_pbr_eligible[instance_index] != 0U) {\n"
    "            gpu_pbr_plan.instance_batches[instance_index] = runtime_gpu_draw_batch_unassigned;\n"
    "        }\n"
    "    }",
    "cluster pbr fallback mask",
)

replace_once(
    "    RuntimeGpuDrawBatchResult gpu_default_batch_result{};\n"
    "    RuntimeGpuDrawBatchResult gpu_pbr_batch_result{};\n"
    "    RuntimeGpuDrawBatchResult gpu_shadow_batch_result{};\n"
    "    RuntimeGpuDrawBatchResult gpu_motion_batch_result{};\n"
    "    if (!instances.empty()) {\n"
    "        if (!state_->gpu_instances.upload(*commands, gpu_instance_models, diagnostics)) {\n"
    "            return false;\n"
    "        }\n"
    "    }",
    "    RuntimeGpuDrawBatchResult gpu_default_batch_result{};\n"
    "    RuntimeGpuDrawBatchResult gpu_pbr_batch_result{};\n"
    "    RuntimeGpuDrawBatchResult gpu_cluster_default_batch_result{};\n"
    "    RuntimeGpuDrawBatchResult gpu_cluster_pbr_batch_result{};\n"
    "    RuntimeGpuDrawBatchResult gpu_shadow_batch_result{};\n"
    "    RuntimeGpuDrawBatchResult gpu_motion_batch_result{};\n"
    "    RuntimeGpuClusterVisibilityResult gpu_cluster_visibility_result{};\n"
    "    if (!instances.empty()) {\n"
    "        if (!state_->gpu_instances.upload(*commands, gpu_instance_models, diagnostics) ||\n"
    "            !state_->gpu_cluster_instances.upload(*commands, gpu_cluster_models, diagnostics)) {\n"
    "            return false;\n"
    "        }\n"
    "    }\n"
    "    if (!gpu_cluster_candidates.candidates.empty() &&\n"
    "        (!gpu_cluster_default_plan.batches.empty() || !gpu_cluster_pbr_plan.batches.empty())) {\n"
    "        if (!state_->gpu_cluster_visibility.cull(\n"
    "                *commands,\n"
    "                gpu_cluster_candidates.candidates,\n"
    "                camera_visibility,\n"
    "                state_->gpu_meshlet_table.result(),\n"
    "                state_->gpu_cluster_instances.result(),\n"
    "                visibility_frustum,\n"
    "                diagnostics\n"
    "            )) {\n"
    "            return false;\n"
    "        }\n"
    "        gpu_cluster_visibility_result = state_->gpu_cluster_visibility.result();\n"
    "    }",
    "cluster visibility execution",
)

replace_once(
    "    if (!gpu_pbr_plan.batches.empty()) {\n"
    "        if (!state_->gpu_pbr_draw_batches.build(",
    "    if (!gpu_cluster_default_plan.batches.empty()) {\n"
    "        if (!state_->gpu_cluster_default_draw_batches.build(\n"
    "                *commands,\n"
    "                gpu_cluster_visibility_result.visibility,\n"
    "                gpu_cluster_default_plan.candidate_batches,\n"
    "                gpu_cluster_default_plan.batches,\n"
    "                diagnostics\n"
    "            ) || !state_->gpu_cluster_default_instances.remap(\n"
    "                *commands,\n"
    "                state_->gpu_cluster_default_draw_batches.result(),\n"
    "                gpu_cluster_visibility_result,\n"
    "                diagnostics\n"
    "            )) {\n"
    "            return false;\n"
    "        }\n"
    "        const GpuDefaultFrame gpu_frame{.view_projection = view_projection};\n"
    "        if (!state_->gpu_default_frame->write(\n"
    "                0U, std::as_bytes(std::span{&gpu_frame, 1})\n"
    "            )) {\n"
    "            set_diagnostics(diagnostics, \"VICE Runtime could not update GPU default cluster frame constants.\");\n"
    "            return false;\n"
    "        }\n"
    "        gpu_cluster_default_batch_result = state_->gpu_cluster_default_draw_batches.result();\n"
    "    }\n"
    "    if (!gpu_pbr_plan.batches.empty()) {\n"
    "        if (!state_->gpu_pbr_draw_batches.build(",
    "cluster default build",
)

replace_once(
    "        gpu_pbr_batch_result = state_->gpu_pbr_draw_batches.result();\n"
    "    }\n"
    "    const bool has_point_shadows = std::any_of(",
    "        gpu_pbr_batch_result = state_->gpu_pbr_draw_batches.result();\n"
    "    }\n"
    "    if (!gpu_cluster_pbr_plan.batches.empty()) {\n"
    "        if (!state_->gpu_cluster_pbr_draw_batches.build(\n"
    "                *commands,\n"
    "                gpu_cluster_visibility_result.visibility,\n"
    "                gpu_cluster_pbr_plan.candidate_batches,\n"
    "                gpu_cluster_pbr_plan.batches,\n"
    "                diagnostics\n"
    "            ) || !state_->gpu_cluster_pbr_instances.remap(\n"
    "                *commands,\n"
    "                state_->gpu_cluster_pbr_draw_batches.result(),\n"
    "                gpu_cluster_visibility_result,\n"
    "                diagnostics\n"
    "            )) {\n"
    "            return false;\n"
    "        }\n"
    "        gpu_cluster_pbr_batch_result = state_->gpu_cluster_pbr_draw_batches.result();\n"
    "    }\n"
    "    const bool has_point_shadows = std::any_of(",
    "cluster pbr build",
)

whole_default_end = '''    if (!gpu_default_batches.empty()) {
        const auto instance_table = state_->gpu_instances.result();
        if (instance_table.models == nullptr || gpu_default_batch_result.compacted_instance_indices == nullptr ||
            gpu_default_batch_result.indirect_arguments == nullptr) {
            set_diagnostics(diagnostics, "VICE Runtime GPU default batch resources are unavailable.");
            return false;
        }
        const render::BufferViewDesc model_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) * sizeof(math::Mat4),
            .structure_stride = sizeof(math::Mat4),
        };
        const render::BufferViewDesc compacted_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) * sizeof(std::uint32_t),
            .structure_stride = sizeof(std::uint32_t),
        };
        if (!commands->set_graphics_pipeline(*state_->pipeline) ||
            !commands->set_graphics_constant_buffer(
                frame_binding, *state_->gpu_default_frame, 0U, sizeof(GpuDefaultFrame)
            ) ||
            !commands->set_graphics_buffer(
                gpu_model_table_binding, *instance_table.models, model_view
            ) ||
            !commands->set_graphics_buffer(
                gpu_compacted_instance_binding,
                *gpu_default_batch_result.compacted_instance_indices,
                compacted_view
            )) {
            set_diagnostics(diagnostics, "VICE Runtime could not bind GPU default scene resources.");
            return false;
        }
        for (std::size_t batch_index = 0U; batch_index < gpu_default_batches.size(); ++batch_index) {
            auto& mesh = state_->meshes.at(gpu_default_batch_meshes[batch_index]);
            const std::uint64_t argument_offset = static_cast<std::uint64_t>(batch_index) *
                sizeof(render::IndexedIndirectDrawArguments);
            if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                !commands->draw_indexed_indirect(
                    *gpu_default_batch_result.indirect_arguments,
                    argument_offset
                )) {
                set_diagnostics(
                    diagnostics,
                    "VICE Runtime could not encode a GPU-driven default mesh batch."
                );
                return false;
            }
        }
    }
'''
cluster_default_render = whole_default_end + '''
    if (!gpu_cluster_default_plan.batches.empty()) {
        const auto instance_table = state_->gpu_instances.result();
        const auto remapped_instances = state_->gpu_cluster_default_instances.result();
        if (instance_table.models == nullptr || remapped_instances.scene_instance_indices == nullptr ||
            gpu_cluster_default_batch_result.indirect_arguments == nullptr) {
            set_diagnostics(diagnostics, "VICE Runtime GPU default cluster resources are unavailable.");
            return false;
        }
        const render::BufferViewDesc model_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) * sizeof(math::Mat4),
            .structure_stride = sizeof(math::Mat4),
        };
        const render::BufferViewDesc compacted_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(gpu_cluster_candidates.candidates.size()) * sizeof(std::uint32_t),
            .structure_stride = sizeof(std::uint32_t),
        };
        if (!commands->set_graphics_pipeline(*state_->pipeline) ||
            !commands->set_graphics_constant_buffer(
                frame_binding, *state_->gpu_default_frame, 0U, sizeof(GpuDefaultFrame)
            ) || !commands->set_graphics_buffer(
                gpu_model_table_binding, *instance_table.models, model_view
            ) || !commands->set_graphics_buffer(
                gpu_compacted_instance_binding,
                *remapped_instances.scene_instance_indices,
                compacted_view
            )) {
            set_diagnostics(diagnostics, "VICE Runtime could not bind GPU default cluster resources.");
            return false;
        }
        for (std::size_t batch_index = 0U; batch_index < gpu_cluster_default_plan.batches.size(); ++batch_index) {
            auto& mesh = state_->meshes.at(gpu_cluster_default_plan.batch_meshes[batch_index]);
            const std::uint64_t argument_offset = static_cast<std::uint64_t>(batch_index) *
                sizeof(render::IndexedIndirectDrawArguments);
            if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                !commands->draw_indexed_indirect(
                    *gpu_cluster_default_batch_result.indirect_arguments,
                    argument_offset
                )) {
                set_diagnostics(diagnostics, "VICE Runtime could not encode a GPU-driven default cluster batch.");
                return false;
            }
        }
    }
'''
replace_once(whole_default_end, cluster_default_render, "default cluster rendering")

pbr_loop = '''        for (std::size_t batch_index = 0U; batch_index < gpu_pbr_plan.batches.size(); ++batch_index) {
            auto& mesh = state_->meshes.at(gpu_pbr_plan.batch_meshes[batch_index]);
            const std::uint64_t argument_offset = static_cast<std::uint64_t>(batch_index) *
                sizeof(render::IndexedIndirectDrawArguments);
            if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                !commands->draw_indexed_indirect(
                    *gpu_pbr_batch_result.indirect_arguments,
                    argument_offset
                )) {
                set_diagnostics(diagnostics, "VICE Runtime could not encode an indexed GPU PBR batch.");
                return false;
            }
        }
'''
pbr_with_clusters = pbr_loop + '''
        if (!gpu_cluster_pbr_plan.batches.empty()) {
            const auto remapped_instances = state_->gpu_cluster_pbr_instances.result();
            if (remapped_instances.scene_instance_indices == nullptr ||
                gpu_cluster_pbr_batch_result.indirect_arguments == nullptr) {
                set_diagnostics(diagnostics, "VICE Runtime GPU PBR cluster resources are unavailable.");
                return false;
            }
            const render::BufferViewDesc cluster_compacted_view{
                .offset = 0U,
                .size = static_cast<std::uint64_t>(gpu_cluster_candidates.candidates.size()) *
                    sizeof(std::uint32_t),
                .structure_stride = sizeof(std::uint32_t),
            };
            if (!commands->set_graphics_buffer(
                    gpu_pbr_compacted_instance_binding,
                    *remapped_instances.scene_instance_indices,
                    cluster_compacted_view
                )) {
                set_diagnostics(diagnostics, "VICE Runtime could not bind GPU PBR cluster instance indices.");
                return false;
            }
            for (std::size_t batch_index = 0U;
                 batch_index < gpu_cluster_pbr_plan.batches.size();
                 ++batch_index) {
                auto& mesh = state_->meshes.at(gpu_cluster_pbr_plan.batch_meshes[batch_index]);
                const std::uint64_t argument_offset = static_cast<std::uint64_t>(batch_index) *
                    sizeof(render::IndexedIndirectDrawArguments);
                if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                    !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                    !commands->draw_indexed_indirect(
                        *gpu_cluster_pbr_batch_result.indirect_arguments,
                        argument_offset
                    )) {
                    set_diagnostics(diagnostics, "VICE Runtime could not encode an indexed GPU PBR cluster batch.");
                    return false;
                }
            }
        }
'''
replace_once(pbr_loop, pbr_with_clusters, "pbr cluster rendering")

path.write_text(text)
