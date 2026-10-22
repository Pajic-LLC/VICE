from pathlib import Path

path = Path("engine/runtime/src/scene.cpp")
text = path.read_text()


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one match, found {count}: {old[:140]!r}")
    text = text.replace(old, new, 1)


replace_once(
    '''    RuntimeGpuDrawBatcher gpu_shadow_draw_batches;
    RuntimeGpuInstanceTable gpu_instances;
    detail::RuntimeGpuMaterialSceneResources gpu_material_scene;''',
    '''    RuntimeGpuDrawBatcher gpu_shadow_draw_batches;
    RuntimeGpuInstanceTable gpu_instances;
    std::array<RuntimeGpuInstanceTable, runtime_point_shadow_light_count> gpu_point_shadow_instances;
    detail::RuntimeGpuMaterialSceneResources gpu_material_scene;''',
)
replace_once(
    '''    std::array<std::unique_ptr<render::Buffer>, runtime_shadow_cascade_count> gpu_shadow_frames;
    std::unique_ptr<render::ShaderModule> gpu_pbr_vertex_shader;''',
    '''    std::array<std::unique_ptr<render::Buffer>, runtime_shadow_cascade_count> gpu_shadow_frames;
    std::array<
        std::array<std::unique_ptr<render::Buffer>, runtime_point_shadow_face_count>,
        runtime_point_shadow_light_count
    > gpu_point_shadow_frames;
    std::unique_ptr<render::ShaderModule> gpu_pbr_vertex_shader;''',
)

replace_once(
    '''        !state_->gpu_shadow_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_instances.initialize(device, diagnostics)) {
        return false;
    }
    state_->gpu_default_frame = create_gpu_default_frame_buffer(device);''',
    '''        !state_->gpu_shadow_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_instances.initialize(device, diagnostics)) {
        return false;
    }
    for (auto& table : state_->gpu_point_shadow_instances) {
        if (!table.initialize(device, diagnostics)) return false;
    }
    state_->gpu_default_frame = create_gpu_default_frame_buffer(device);''',
)
replace_once(
    '''    for (auto& frame : state_->gpu_shadow_frames) {
        frame = create_gpu_shadow_frame_buffer(device);
        if (!frame) {
            set_diagnostics(diagnostics, "VICE Runtime could not create GPU directional shadow frame constants.");
            return false;
        }
    }

    const bool metal = device.backend() == render::Backend::metal;''',
    '''    for (auto& frame : state_->gpu_shadow_frames) {
        frame = create_gpu_shadow_frame_buffer(device);
        if (!frame) {
            set_diagnostics(diagnostics, "VICE Runtime could not create GPU directional shadow frame constants.");
            return false;
        }
    }
    for (auto& light_frames : state_->gpu_point_shadow_frames) {
        for (auto& frame : light_frames) {
            frame = create_gpu_shadow_frame_buffer(device);
            if (!frame) {
                set_diagnostics(diagnostics, "VICE Runtime could not create GPU point-shadow frame constants.");
                return false;
            }
        }
    }

    const bool metal = device.backend() == render::Backend::metal;''',
)

old_shadow_build = '''    if (shadow_view.has_value() && !gpu_shadow_plan.batches.empty()) {
        if (!state_->gpu_shadow_visibility.prepare(
                *commands,
                static_cast<std::uint32_t>(instances.size()),
                diagnostics
            ) || !state_->gpu_shadow_draw_batches.build(
                *commands,
                state_->gpu_shadow_visibility.result(),
                gpu_shadow_plan.instance_batches,
                gpu_shadow_plan.batches,
                diagnostics
            )) {
            return false;
        }
        for (std::size_t cascade_index = 0U; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
            const GpuDefaultFrame shadow_frame{
                .view_projection = shadow_view->cascades[cascade_index].view_projection,
            };
            if (!state_->gpu_shadow_frames[cascade_index]->write(
                    0U, std::as_bytes(std::span{&shadow_frame, 1})
                )) {
                set_diagnostics(diagnostics, "VICE Runtime could not update GPU directional shadow frame constants.");
                return false;
            }
        }
        gpu_shadow_batch_result = state_->gpu_shadow_draw_batches.result();
    }
'''
new_shadow_build = '''    const bool has_point_shadows = std::any_of(
        point_shadow_views.begin(),
        point_shadow_views.end(),
        [](const auto& view) { return view.has_value(); }
    );
    if ((shadow_view.has_value() || has_point_shadows) && !gpu_shadow_plan.batches.empty()) {
        if (!state_->gpu_shadow_visibility.prepare(
                *commands,
                static_cast<std::uint32_t>(instances.size()),
                diagnostics
            ) || !state_->gpu_shadow_draw_batches.build(
                *commands,
                state_->gpu_shadow_visibility.result(),
                gpu_shadow_plan.instance_batches,
                gpu_shadow_plan.batches,
                diagnostics
            )) {
            return false;
        }
        gpu_shadow_batch_result = state_->gpu_shadow_draw_batches.result();
    }
    if (shadow_view.has_value() && !gpu_shadow_plan.batches.empty()) {
        for (std::size_t cascade_index = 0U; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
            const GpuDefaultFrame shadow_frame{
                .view_projection = shadow_view->cascades[cascade_index].view_projection,
            };
            if (!state_->gpu_shadow_frames[cascade_index]->write(
                    0U, std::as_bytes(std::span{&shadow_frame, 1})
                )) {
                set_diagnostics(diagnostics, "VICE Runtime could not update GPU directional shadow frame constants.");
                return false;
            }
        }
    }
    if (has_point_shadows && !gpu_shadow_plan.batches.empty()) {
        for (std::size_t light_slot = 0U; light_slot < point_shadow_views.size(); ++light_slot) {
            if (!point_shadow_views[light_slot].has_value()) continue;
            const auto& point_view = *point_shadow_views[light_slot];
            std::vector<math::Mat4> point_models;
            point_models.reserve(instances.size());
            for (const auto& instance : instances) {
                point_models.push_back(
                    runtime_model_matrix_relative_to(instance, point_view.global_position)
                );
            }
            if (!state_->gpu_point_shadow_instances[light_slot].upload(
                    *commands, point_models, diagnostics
                )) {
                return false;
            }
            for (std::size_t face_index = 0U; face_index < runtime_point_shadow_face_count; ++face_index) {
                const GpuDefaultFrame point_frame{
                    .view_projection = point_view.face_view_projections[face_index],
                };
                if (!state_->gpu_point_shadow_frames[light_slot][face_index]->write(
                        0U, std::as_bytes(std::span{&point_frame, 1})
                    )) {
                    set_diagnostics(diagnostics, "VICE Runtime could not update GPU point-shadow frame constants.");
                    return false;
                }
            }
        }
    }
'''
replace_once(old_shadow_build, new_shadow_build)

old_point_loop = '''    for (std::size_t light_slot = 0U; light_slot < point_shadow_views.size(); ++light_slot) {
        if (!point_shadow_views[light_slot].has_value()) continue;
        const auto& point_view = *point_shadow_views[light_slot];
        for (std::size_t face_index = 0U; face_index < runtime_point_shadow_face_count; ++face_index) {
            if (!commands->begin_depth_render_pass(
                    *state_->point_shadow_maps[light_slot][face_index], 1.0F, true
                ) || !commands->set_viewport(render::Viewport{
                    .width = static_cast<float>(runtime_point_shadow_map_size),
                    .height = static_cast<float>(runtime_point_shadow_map_size),
                }) || !commands->set_scissor_rect(render::ScissorRect{
                    .width = runtime_point_shadow_map_size,
                    .height = runtime_point_shadow_map_size,
                }) || !commands->set_graphics_pipeline(*state_->shadow_pipeline)) {
                set_diagnostics(diagnostics, "VICE Runtime could not begin a point-light shadow face.");
                return false;
            }
            auto& face_constants = state_->point_shadow_constants[light_slot][face_index];
            for (const auto& instance : instances) {
                auto shadow_iterator = face_constants.find(instance.entity);
                if (shadow_iterator == face_constants.end()) {
                    auto constants = create_shadow_constant_buffer(*state_->device);
                    if (!constants) {
                        set_diagnostics(diagnostics, "VICE Runtime could not create point-shadow constants.");
                        return false;
                    }
                    shadow_iterator = face_constants.emplace(instance.entity, std::move(constants)).first;
                }
                const ShadowConstants shadow_constants{
                    .view_projection = point_view.face_view_projections[face_index],
                    .model = runtime_model_matrix_relative_to(instance, point_view.global_position),
                };
                if (!shadow_iterator->second->write(
                        0U, std::as_bytes(std::span{&shadow_constants, 1})
                    )) {
                    set_diagnostics(diagnostics, "VICE Runtime could not update point-shadow constants.");
                    return false;
                }
                auto& mesh = state_->meshes.at(instance.mesh);
                if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                    !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                    !commands->set_graphics_constant_buffer(
                        frame_binding, *shadow_iterator->second, 0U, sizeof(ShadowConstants)
                    ) || !commands->draw_indexed(mesh.index_count)) {
                    set_diagnostics(diagnostics, "VICE Runtime could not encode a point-light shadow caster.");
                    return false;
                }
            }
            if (!commands->end_render_pass()) {
                set_diagnostics(diagnostics, "VICE Runtime could not finish a point-light shadow face.");
                return false;
            }
        }
    }
'''
new_point_loop = '''    for (std::size_t light_slot = 0U; light_slot < point_shadow_views.size(); ++light_slot) {
        if (!point_shadow_views[light_slot].has_value()) continue;
        const auto point_instance_table = state_->gpu_point_shadow_instances[light_slot].result();
        if (!gpu_shadow_plan.batches.empty() &&
            (point_instance_table.models == nullptr ||
             gpu_shadow_batch_result.compacted_instance_indices == nullptr ||
             gpu_shadow_batch_result.indirect_arguments == nullptr)) {
            set_diagnostics(diagnostics, "VICE Runtime GPU point-shadow batch resources are unavailable.");
            return false;
        }
        const render::BufferViewDesc point_model_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) * sizeof(math::Mat4),
            .structure_stride = sizeof(math::Mat4),
        };
        const render::BufferViewDesc point_compacted_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) * sizeof(std::uint32_t),
            .structure_stride = sizeof(std::uint32_t),
        };
        for (std::size_t face_index = 0U; face_index < runtime_point_shadow_face_count; ++face_index) {
            if (!commands->begin_depth_render_pass(
                    *state_->point_shadow_maps[light_slot][face_index], 1.0F, true
                ) || !commands->set_viewport(render::Viewport{
                    .width = static_cast<float>(runtime_point_shadow_map_size),
                    .height = static_cast<float>(runtime_point_shadow_map_size),
                }) || !commands->set_scissor_rect(render::ScissorRect{
                    .width = runtime_point_shadow_map_size,
                    .height = runtime_point_shadow_map_size,
                })) {
                set_diagnostics(diagnostics, "VICE Runtime could not begin a point-light shadow face.");
                return false;
            }
            if (!gpu_shadow_plan.batches.empty()) {
                if (!commands->set_graphics_pipeline(*state_->gpu_shadow_pipeline) ||
                    !commands->set_graphics_constant_buffer(
                        frame_binding,
                        *state_->gpu_point_shadow_frames[light_slot][face_index],
                        0U,
                        sizeof(GpuDefaultFrame)
                    ) || !commands->set_graphics_buffer(
                        gpu_model_table_binding,
                        *point_instance_table.models,
                        point_model_view
                    ) || !commands->set_graphics_buffer(
                        gpu_compacted_instance_binding,
                        *gpu_shadow_batch_result.compacted_instance_indices,
                        point_compacted_view
                    )) {
                    set_diagnostics(diagnostics, "VICE Runtime could not bind GPU point-shadow resources.");
                    return false;
                }
                for (std::size_t batch_index = 0U; batch_index < gpu_shadow_plan.batches.size(); ++batch_index) {
                    auto& mesh = state_->meshes.at(gpu_shadow_plan.batch_meshes[batch_index]);
                    const std::uint64_t argument_offset = static_cast<std::uint64_t>(batch_index) *
                        sizeof(render::IndexedIndirectDrawArguments);
                    if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                        !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                        !commands->draw_indexed_indirect(
                            *gpu_shadow_batch_result.indirect_arguments,
                            argument_offset
                        )) {
                        set_diagnostics(diagnostics, "VICE Runtime could not encode a GPU-driven point-shadow batch.");
                        return false;
                    }
                }
            }
            if (!commands->end_render_pass()) {
                set_diagnostics(diagnostics, "VICE Runtime could not finish a point-light shadow face.");
                return false;
            }
        }
    }
'''
replace_once(old_point_loop, new_point_loop)

path.write_text(text)
print("Applied #234 GPU-driven point-shadow integration.")
