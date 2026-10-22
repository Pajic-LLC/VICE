from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


scene_path = Path("engine/runtime/src/scene.cpp")
scene = scene_path.read_text()

scene = replace_once(
    scene,
    '#include <vice/runtime/scene.hpp>\n\n',
    '#include <vice/runtime/scene.hpp>\n\n#include <vice/render/shader_asset.hpp>\n',
    "shader asset include",
)
scene = replace_once(
    scene,
    '#include "environment_pipeline.hpp"\n#include "material_pipeline.hpp"\n',
    '#include "environment_pipeline.hpp"\n#include "gpu_material_scene.hpp"\n#include "gpu_pbr_batches.hpp"\n#include "material_pipeline.hpp"\n',
    "runtime gpu pbr includes",
)

binding_anchor = '''constexpr render::ShaderBindingKey gpu_compacted_instance_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2,
    .space = 0,
    .array_element = 0,
};
'''
binding_insert = binding_anchor + r'''
constexpr render::ShaderBindingKey gpu_pbr_model_table_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey gpu_pbr_compacted_instance_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey gpu_pbr_instance_material_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey gpu_pbr_material_record_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 3,
    .space = 0,
    .array_element = 0,
};

[[nodiscard]] constexpr render::ShaderBindingKey gpu_pbr_material_texture_binding(
    const std::uint32_t array_element
) noexcept {
    return render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 0,
        .space = 1,
        .array_element = array_element,
    };
}

[[nodiscard]] constexpr render::ShaderBindingKey gpu_pbr_material_sampler_binding(
    const std::uint32_t array_element
) noexcept {
    return render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::sampler,
        .binding = 0,
        .space = 1,
        .array_element = array_element,
    };
}

[[nodiscard]] constexpr render::ShaderBindingKey gpu_pbr_lighting_texture_binding(
    const std::uint32_t binding
) noexcept {
    return render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = binding,
        .space = 2,
        .array_element = 0,
    };
}

[[nodiscard]] constexpr render::ShaderBindingKey gpu_pbr_lighting_sampler_binding(
    const std::uint32_t binding
) noexcept {
    return render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::sampler,
        .binding = binding,
        .space = 2,
        .array_element = 0,
    };
}
'''
scene = replace_once(scene, binding_anchor, binding_insert, "gpu pbr binding keys")

struct_anchor = '''struct SceneConstants {
    math::Mat4 view_projection{};
    math::Mat4 model{};
    float camera_position[4]{};
    float light_metadata[4]{};
    std::array<SceneLightConstants, max_runtime_lights> lights{};
    math::Mat4 shadow_view_projection{};
    float shadow_metadata[4]{};
    std::array<math::Mat4, runtime_shadow_cascade_count - 1U> shadow_cascade_view_projections{};
    float shadow_cascade_splits[4]{};
    float shadow_camera_forward[4]{};
    float environment_metadata[4]{};
    float environment_secondary_metadata[4]{};
    float environment_tertiary_metadata[4]{};
    std::array<SceneReflectionProjectionConstants, 3U> environment_projections{};
    math::Mat4 atmosphere_camera_from_model{};
    float atmosphere_sun_direction_intensity[4]{};
    float atmosphere_sun_color[4]{};
    float atmosphere_parameters[4]{};
    float atmosphere_secondary[4]{};
    float atmosphere_ground_albedo[4]{};
    std::array<ScenePointShadowConstants, runtime_point_shadow_light_count> point_shadows{};
};
'''
struct_insert = struct_anchor + r'''
struct GpuPbrSceneConstants {
    math::Mat4 view_projection{};
    float camera_position[4]{};
    float light_metadata[4]{};
    std::array<SceneLightConstants, max_runtime_lights> lights{};
    math::Mat4 shadow_view_projection{};
    float shadow_metadata[4]{};
    std::array<math::Mat4, runtime_shadow_cascade_count - 1U> shadow_cascade_view_projections{};
    float shadow_cascade_splits[4]{};
    float shadow_camera_forward[4]{};
    float environment_metadata[4]{};
    float environment_secondary_metadata[4]{};
    float environment_tertiary_metadata[4]{};
    std::array<SceneReflectionProjectionConstants, 3U> environment_projections{};
    math::Mat4 atmosphere_camera_from_model{};
    float atmosphere_sun_direction_intensity[4]{};
    float atmosphere_sun_color[4]{};
    float atmosphere_parameters[4]{};
    float atmosphere_secondary[4]{};
    float atmosphere_ground_albedo[4]{};
    std::array<ScenePointShadowConstants, runtime_point_shadow_light_count> point_shadows{};
};
'''
scene = replace_once(scene, struct_anchor, struct_insert, "gpu pbr constants struct")
scene = replace_once(
    scene,
    'static_assert(sizeof(SceneConstants) == 2256U);\n',
    'static_assert(sizeof(SceneConstants) == 2256U);\nstatic_assert(sizeof(GpuPbrSceneConstants) == 2192U);\n',
    "gpu pbr constants size",
)

buffer_anchor = '''[[nodiscard]] std::unique_ptr<render::Buffer> create_gpu_default_frame_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(GpuDefaultFrame);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime GPU default frame constants";
    return device.create_buffer(desc);
}
'''
buffer_insert = buffer_anchor + r'''
[[nodiscard]] std::unique_ptr<render::Buffer> create_gpu_pbr_frame_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(GpuPbrSceneConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime GPU PBR scene constants";
    return device.create_buffer(desc);
}
'''
scene = replace_once(scene, buffer_anchor, buffer_insert, "gpu pbr frame buffer")

model_anchor = '''math::Mat4 runtime_model_matrix(const RenderMeshInstance& instance) noexcept {
    return runtime_model_matrix_relative_to(instance, {});
}

std::optional<RuntimeCameraView> primary_camera_view(
'''
constants_fn = r'''math::Mat4 runtime_model_matrix(const RenderMeshInstance& instance) noexcept {
    return runtime_model_matrix_relative_to(instance, {});
}

[[nodiscard]] GpuPbrSceneConstants make_gpu_pbr_scene_constants(
    const math::Mat4& view_projection,
    const RuntimeCameraView& runtime_camera,
    const std::span<const RuntimeLight> runtime_lights,
    const std::optional<RuntimeShadowView>& shadow_view,
    const std::array<
        std::optional<RuntimePointShadowView>,
        runtime_point_shadow_light_count
    >& point_shadow_views,
    const RuntimeAtmosphereFrame& atmosphere,
    const detail::RuntimeEnvironmentBinding& global_environment
) noexcept {
    GpuPbrSceneConstants constants{
        .view_projection = view_projection,
        .camera_position = {
            runtime_camera.position.x,
            runtime_camera.position.y,
            runtime_camera.position.z,
            1.0F,
        },
    };
    constants.light_metadata[0] = static_cast<float>(runtime_lights.size());
    for (std::size_t light_index = 0U; light_index < runtime_lights.size(); ++light_index) {
        const auto& source = runtime_lights[light_index];
        auto& destination = constants.lights[light_index];
        if (source.light.type == world::LightType::directional) {
            destination.position_or_direction[0] = source.direction.x;
            destination.position_or_direction[1] = source.direction.y;
            destination.position_or_direction[2] = source.direction.z;
            destination.position_or_direction[3] = 0.0F;
        } else {
            destination.position_or_direction[0] = source.position.x;
            destination.position_or_direction[1] = source.position.y;
            destination.position_or_direction[2] = source.position.z;
            destination.position_or_direction[3] = 1.0F;
        }
        destination.color_intensity[0] = source.light.color.red;
        destination.color_intensity[1] = source.light.color.green;
        destination.color_intensity[2] = source.light.color.blue;
        destination.color_intensity[3] = source.light.intensity;
        destination.parameters[0] = source.light.range;
    }

    if (shadow_view.has_value()) {
        constants.shadow_view_projection = shadow_view->cascades[0].view_projection;
        for (std::size_t cascade_index = 1U; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
            constants.shadow_cascade_view_projections[cascade_index - 1U] =
                shadow_view->cascades[cascade_index].view_projection;
        }
        for (std::size_t cascade_index = 0U; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
            constants.shadow_cascade_splits[cascade_index] =
                shadow_view->cascades[cascade_index].split_far;
        }
        constants.shadow_camera_forward[0] = runtime_camera.forward.x;
        constants.shadow_camera_forward[1] = runtime_camera.forward.y;
        constants.shadow_camera_forward[2] = runtime_camera.forward.z;
        constants.shadow_metadata[0] = 1.0F;
        constants.shadow_metadata[1] = static_cast<float>(shadow_view->light_index);
        constants.shadow_metadata[2] = 1.0F / static_cast<float>(runtime_shadow_map_size);
        constants.shadow_metadata[3] = 0.0015F;
    }

    if (global_environment.available) {
        constants.environment_metadata[0] = global_environment.intensity;
        constants.environment_metadata[1] = global_environment.rotation_radians;
        constants.environment_metadata[2] = global_environment.max_specular_lod;
        constants.environment_metadata[3] = 1.0F;
    }

    constants.atmosphere_sun_direction_intensity[0] = atmosphere.sun_direction.x;
    constants.atmosphere_sun_direction_intensity[1] = atmosphere.sun_direction.y;
    constants.atmosphere_sun_direction_intensity[2] = atmosphere.sun_direction.z;
    constants.atmosphere_sun_direction_intensity[3] = atmosphere.sun_intensity;
    constants.atmosphere_sun_color[0] = atmosphere.sun_color.x;
    constants.atmosphere_sun_color[1] = atmosphere.sun_color.y;
    constants.atmosphere_sun_color[2] = atmosphere.sun_color.z;
    constants.atmosphere_sun_color[3] = 1.0F;
    constants.atmosphere_parameters[0] = atmosphere.settings.rayleigh_strength;
    constants.atmosphere_parameters[1] = atmosphere.settings.mie_strength;
    constants.atmosphere_parameters[2] = atmosphere.settings.mie_anisotropy;
    constants.atmosphere_parameters[3] = atmosphere.settings.aerial_density;
    constants.atmosphere_secondary[0] = atmosphere.settings.sun_angular_radius;
    constants.atmosphere_secondary[1] = atmosphere.settings.horizon_falloff;
    constants.atmosphere_secondary[2] = 1.0F;
    constants.atmosphere_ground_albedo[0] = atmosphere.settings.ground_albedo.x;
    constants.atmosphere_ground_albedo[1] = atmosphere.settings.ground_albedo.y;
    constants.atmosphere_ground_albedo[2] = atmosphere.settings.ground_albedo.z;
    constants.atmosphere_ground_albedo[3] = 1.0F;

    for (std::size_t light_slot = 0U; light_slot < point_shadow_views.size(); ++light_slot) {
        if (!point_shadow_views[light_slot].has_value()) continue;
        const auto& point_view = *point_shadow_views[light_slot];
        auto& point_constants = constants.point_shadows[light_slot];
        point_constants.metadata[0] = 1.0F;
        point_constants.metadata[1] = static_cast<float>(point_view.light_index);
        point_constants.metadata[2] = point_view.range;
        point_constants.metadata[3] = 1.0F / static_cast<float>(runtime_point_shadow_map_size);
        point_constants.face_view_projections = point_view.face_view_projections;
    }
    return constants;
}

std::optional<RuntimeCameraView> primary_camera_view(
'''
scene = replace_once(scene, model_anchor, constants_fn, "gpu pbr scene constants builder")

state_anchor = '''    RuntimeVisibilityCuller visibility;
    RuntimeGpuDrawBatcher gpu_draw_batches;
    RuntimeGpuInstanceTable gpu_instances;
    std::unique_ptr<render::Buffer> gpu_default_frame;
    std::unique_ptr<render::ShaderModule> vertex_shader;
'''
state_insert = '''    RuntimeVisibilityCuller visibility;
    RuntimeGpuDrawBatcher gpu_draw_batches;
    RuntimeGpuDrawBatcher gpu_pbr_draw_batches;
    RuntimeGpuInstanceTable gpu_instances;
    detail::RuntimeGpuMaterialSceneResources gpu_material_scene;
    std::unique_ptr<render::Buffer> gpu_default_frame;
    std::unique_ptr<render::Buffer> gpu_pbr_frame;
    std::unique_ptr<render::ShaderModule> gpu_pbr_vertex_shader;
    std::unique_ptr<render::ShaderModule> gpu_pbr_fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> gpu_pbr_pipeline;
    std::unique_ptr<render::ShaderModule> vertex_shader;
'''
scene = replace_once(scene, state_anchor, state_insert, "renderer gpu pbr state")

init_material_anchor = '''    if (!state_->materials.initialize(
            device,
            render::TextureFormat::rgba16_float,
            mesh_layouts,
            content_root,
            diagnostics
        )) {
        return false;
    }
    if (!state_->environment.initialize(device, content_root, diagnostics)) {
'''
init_material_new = '''    if (!state_->materials.initialize(
            device,
            render::TextureFormat::rgba16_float,
            mesh_layouts,
            content_root,
            diagnostics
        )) {
        return false;
    }
    auto gpu_material_build = build_runtime_gpu_material_table_plan(
        RuntimeGpuMaterialTableBuildInput{
            .materials = state_->materials.catalog().records(),
            .validate_texture_references = false,
        }
    );
    if (!gpu_material_build) {
        set_diagnostics(diagnostics, gpu_material_build.diagnostics);
        return false;
    }
    if (!state_->gpu_material_scene.initialize(
            device,
            std::move(gpu_material_build.plan),
            state_->materials,
            diagnostics
        )) {
        return false;
    }
    if (!state_->environment.initialize(device, content_root, diagnostics)) {
'''
scene = replace_once(scene, init_material_anchor, init_material_new, "gpu material scene init")

scene = replace_once(
    scene,
    '''    if (!state_->visibility.initialize(device, diagnostics) ||
        !state_->gpu_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_instances.initialize(device, diagnostics)) {
''',
    '''    if (!state_->visibility.initialize(device, diagnostics) ||
        !state_->gpu_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_pbr_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_instances.initialize(device, diagnostics)) {
''',
    "second gpu batcher init",
)

fallback_pipeline_anchor = '''    state_->pipeline = device.create_graphics_pipeline(pipeline_desc);
    if (!state_->pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the fallback scene graphics pipeline.");
        return false;
    }

    render::ShaderModuleDesc sky_vertex_desc;
'''
fallback_pipeline_new = r'''    state_->pipeline = device.create_graphics_pipeline(pipeline_desc);
    if (!state_->pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the fallback scene graphics pipeline.");
        return false;
    }

    const auto material_records = state_->materials.catalog().records();
    const auto gpu_pbr_material = std::find_if(
        material_records.begin(),
        material_records.end(),
        [](const CookedRenderMaterialRecord& record) {
            return render::render_material_uses_gpu_scene_pbr(record.asset.version);
        }
    );
    if (gpu_pbr_material != material_records.end()) {
        const auto vertex_manifest = (
            gpu_pbr_material->path.parent_path() /
            gpu_pbr_material->asset.vertex_shader_manifest
        ).lexically_normal();
        const auto fragment_manifest = (
            gpu_pbr_material->path.parent_path() /
            gpu_pbr_material->asset.fragment_shader_manifest
        ).lexically_normal();
        auto gpu_pbr_vertex = render::load_cooked_shader(device, vertex_manifest);
        auto gpu_pbr_fragment = render::load_cooked_shader(device, fragment_manifest);
        if (!gpu_pbr_vertex || !gpu_pbr_fragment ||
            gpu_pbr_vertex.module->stage() != render::ShaderStage::vertex ||
            gpu_pbr_fragment.module->stage() != render::ShaderStage::fragment) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not load the standard GPU PBR shader pair."
            );
            return false;
        }
        state_->gpu_pbr_vertex_shader = std::move(gpu_pbr_vertex.module);
        state_->gpu_pbr_fragment_shader = std::move(gpu_pbr_fragment.module);

        render::GraphicsPipelineDesc gpu_pbr_pipeline_desc;
        gpu_pbr_pipeline_desc.vertex_shader = state_->gpu_pbr_vertex_shader.get();
        gpu_pbr_pipeline_desc.fragment_shader = state_->gpu_pbr_fragment_shader.get();
        gpu_pbr_pipeline_desc.color_format = render::TextureFormat::rgba16_float;
        gpu_pbr_pipeline_desc.cull_mode = render::CullMode::none;
        gpu_pbr_pipeline_desc.depth.format = render::TextureFormat::depth32_float;
        gpu_pbr_pipeline_desc.depth.test_enabled = true;
        gpu_pbr_pipeline_desc.depth.write_enabled = true;
        gpu_pbr_pipeline_desc.depth.compare = render::CompareOp::less;
        gpu_pbr_pipeline_desc.vertex_buffers = mesh_layouts;
        gpu_pbr_pipeline_desc.debug_name = "VICE Runtime indexed GPU PBR pipeline";
        state_->gpu_pbr_pipeline = device.create_graphics_pipeline(gpu_pbr_pipeline_desc);
        state_->gpu_pbr_frame = create_gpu_pbr_frame_buffer(device);
        if (!state_->gpu_pbr_pipeline || !state_->gpu_pbr_frame) {
            set_diagnostics(diagnostics, "VICE Runtime could not create indexed GPU PBR resources.");
            return false;
        }
    }

    render::ShaderModuleDesc sky_vertex_desc;
'''
scene = replace_once(scene, fallback_pipeline_anchor, fallback_pipeline_new, "gpu pbr shared pipeline")

validation_anchor = '''        if (instance.material.has_value() &&
            state_->materials.find(*instance.material).pipeline == nullptr) {
            set_diagnostics(
                diagnostics,
                "Runtime World references missing cooked render material `" +
                    instance.material->value.to_string() + "`."
            );
            return false;
        }
'''
validation_new = r'''        if (instance.material.has_value()) {
            const auto* const material_record = state_->materials.catalog().find(*instance.material);
            if (material_record == nullptr) {
                set_diagnostics(
                    diagnostics,
                    "Runtime World references missing cooked render material `" +
                        instance.material->value.to_string() + "`."
                );
                return false;
            }
            if (render::render_material_uses_gpu_scene_pbr(material_record->asset.version)) {
                if (state_->gpu_pbr_pipeline == nullptr || state_->gpu_pbr_frame == nullptr) {
                    set_diagnostics(
                        diagnostics,
                        "Runtime World references a V4 GPU PBR material but the shared GPU PBR pipeline is unavailable."
                    );
                    return false;
                }
            } else if (state_->materials.find(*instance.material).pipeline == nullptr) {
                set_diagnostics(
                    diagnostics,
                    "Runtime World references missing cooked render material `" +
                        instance.material->value.to_string() + "`."
                );
                return false;
            }
        }
'''
scene = replace_once(scene, validation_anchor, validation_new, "v4 material validation")

batch_anchor = '''    std::vector<math::Mat4> gpu_instance_models;
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

    RuntimeGpuDrawBatchResult gpu_default_batch_result{};
    if (!gpu_default_batches.empty()) {
        if (!state_->gpu_instances.upload(*commands, gpu_instance_models, diagnostics) ||
            !state_->gpu_draw_batches.build(
                *commands,
                state_->visibility.result(),
                gpu_batch_assignments,
                gpu_default_batches,
                diagnostics
            )) {
            return false;
        }
        const GpuDefaultFrame gpu_frame{.view_projection = view_projection};
        if (!state_->gpu_default_frame->write(
                0U, std::as_bytes(std::span{&gpu_frame, 1})
            )) {
            set_diagnostics(diagnostics, "VICE Runtime could not update GPU default frame constants.");
            return false;
        }
        gpu_default_batch_result = state_->gpu_draw_batches.result();
    }
'''
batch_new = r'''    std::vector<math::Mat4> gpu_instance_models;
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

    const auto gpu_pbr_build = detail::build_runtime_gpu_pbr_batch_plan(
        instances,
        state_->catalog.records(),
        state_->materials.catalog().records()
    );
    if (!gpu_pbr_build) {
        set_diagnostics(diagnostics, gpu_pbr_build.diagnostics);
        return false;
    }
    const auto& gpu_pbr_plan = gpu_pbr_build.plan;

    RuntimeGpuDrawBatchResult gpu_default_batch_result{};
    RuntimeGpuDrawBatchResult gpu_pbr_batch_result{};
    if (!gpu_default_batches.empty() || !gpu_pbr_plan.batches.empty()) {
        if (!state_->gpu_instances.upload(*commands, gpu_instance_models, diagnostics)) {
            return false;
        }
    }
    if (!gpu_default_batches.empty()) {
        if (!state_->gpu_draw_batches.build(
                *commands,
                state_->visibility.result(),
                gpu_batch_assignments,
                gpu_default_batches,
                diagnostics
            )) {
            return false;
        }
        const GpuDefaultFrame gpu_frame{.view_projection = view_projection};
        if (!state_->gpu_default_frame->write(
                0U, std::as_bytes(std::span{&gpu_frame, 1})
            )) {
            set_diagnostics(diagnostics, "VICE Runtime could not update GPU default frame constants.");
            return false;
        }
        gpu_default_batch_result = state_->gpu_draw_batches.result();
    }
    if (!gpu_pbr_plan.batches.empty()) {
        if (!state_->gpu_pbr_draw_batches.build(
                *commands,
                state_->visibility.result(),
                gpu_pbr_plan.instance_batches,
                gpu_pbr_plan.batches,
                diagnostics
            ) || !state_->gpu_material_scene.upload(*commands, instances, diagnostics)) {
            return false;
        }
        const GpuPbrSceneConstants gpu_pbr_constants = make_gpu_pbr_scene_constants(
            view_projection,
            runtime_camera,
            runtime_lights,
            shadow_view,
            point_shadow_views,
            atmosphere,
            global_environment
        );
        if (!state_->gpu_pbr_frame->write(
                0U, std::as_bytes(std::span{&gpu_pbr_constants, 1})
            )) {
            set_diagnostics(diagnostics, "VICE Runtime could not update GPU PBR scene constants.");
            return false;
        }
        gpu_pbr_batch_result = state_->gpu_pbr_draw_batches.result();
    }
'''
scene = replace_once(scene, batch_anchor, batch_new, "gpu pbr batch construction")

hdr_anchor = '''    for (const auto& instance : instances) {
        if (!instance.material.has_value()) continue;
'''
pbr_draw_block = r'''    if (!gpu_pbr_plan.batches.empty()) {
        const auto instance_table = state_->gpu_instances.result();
        const auto material_scene = state_->gpu_material_scene.view();
        if (state_->gpu_pbr_pipeline == nullptr || state_->gpu_pbr_frame == nullptr ||
            instance_table.models == nullptr ||
            gpu_pbr_batch_result.compacted_instance_indices == nullptr ||
            gpu_pbr_batch_result.indirect_arguments == nullptr ||
            material_scene.materials.records == nullptr ||
            material_scene.instance_materials.records == nullptr ||
            material_scene.resources.textures.empty() ||
            material_scene.resources.samplers.empty()) {
            set_diagnostics(diagnostics, "VICE Runtime GPU PBR resources are unavailable.");
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
        const render::BufferViewDesc instance_material_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) *
                sizeof(RuntimeGpuInstanceMaterialRecord),
            .structure_stride = sizeof(RuntimeGpuInstanceMaterialRecord),
        };
        const render::BufferViewDesc material_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(material_scene.materials.material_count) *
                sizeof(RuntimeGpuMaterialRecord),
            .structure_stride = sizeof(RuntimeGpuMaterialRecord),
        };

        bool gpu_pbr_bound =
            commands->set_graphics_pipeline(*state_->gpu_pbr_pipeline) &&
            commands->set_graphics_constant_buffer(
                frame_binding, *state_->gpu_pbr_frame, 0U, sizeof(GpuPbrSceneConstants)
            ) &&
            commands->set_graphics_buffer(
                gpu_pbr_model_table_binding, *instance_table.models, model_view
            ) &&
            commands->set_graphics_buffer(
                gpu_pbr_compacted_instance_binding,
                *gpu_pbr_batch_result.compacted_instance_indices,
                compacted_view
            ) &&
            commands->set_graphics_buffer(
                gpu_pbr_instance_material_binding,
                *material_scene.instance_materials.records,
                instance_material_view
            ) &&
            commands->set_graphics_buffer(
                gpu_pbr_material_record_binding,
                *material_scene.materials.records,
                material_view
            );

        for (std::size_t texture_index = 0U;
            texture_index < material_scene.resources.textures.size() && gpu_pbr_bound;
            ++texture_index) {
            gpu_pbr_bound = material_scene.resources.textures[texture_index] != nullptr &&
                commands->set_graphics_texture(
                    gpu_pbr_material_texture_binding(static_cast<std::uint32_t>(texture_index)),
                    *material_scene.resources.textures[texture_index]
                );
        }
        for (std::size_t sampler_index = 0U;
            sampler_index < material_scene.resources.samplers.size() && gpu_pbr_bound;
            ++sampler_index) {
            gpu_pbr_bound = material_scene.resources.samplers[sampler_index] != nullptr &&
                commands->set_graphics_sampler(
                    gpu_pbr_material_sampler_binding(static_cast<std::uint32_t>(sampler_index)),
                    *material_scene.resources.samplers[sampler_index]
                );
        }

        for (std::size_t cascade_index = 0U;
            cascade_index < runtime_shadow_cascade_count && gpu_pbr_bound;
            ++cascade_index) {
            gpu_pbr_bound = state_->shadow_maps[cascade_index] != nullptr &&
                commands->set_graphics_texture(
                    gpu_pbr_lighting_texture_binding(static_cast<std::uint32_t>(cascade_index)),
                    *state_->shadow_maps[cascade_index]
                );
        }
        for (std::size_t light_slot = 0U;
            light_slot < runtime_point_shadow_light_count && gpu_pbr_bound;
            ++light_slot) {
            for (std::size_t face_index = 0U;
                face_index < runtime_point_shadow_face_count && gpu_pbr_bound;
                ++face_index) {
                const std::uint32_t binding = 4U + static_cast<std::uint32_t>(
                    light_slot * runtime_point_shadow_face_count + face_index
                );
                gpu_pbr_bound = state_->point_shadow_maps[light_slot][face_index] != nullptr &&
                    commands->set_graphics_texture(
                        gpu_pbr_lighting_texture_binding(binding),
                        *state_->point_shadow_maps[light_slot][face_index]
                    );
            }
        }
        gpu_pbr_bound = gpu_pbr_bound && state_->shadow_sampler != nullptr &&
            commands->set_graphics_sampler(
                gpu_pbr_lighting_sampler_binding(0U), *state_->shadow_sampler
            );

        render::Texture* environment_irradiance = material_scene.resources.textures.front();
        render::Texture* environment_specular = environment_irradiance;
        render::Texture* environment_brdf = environment_irradiance;
        render::Sampler* environment_sampler = material_scene.resources.samplers.front();
        if (global_environment.available) {
            environment_irradiance = global_environment.irradiance;
            environment_specular = global_environment.specular;
            environment_brdf = global_environment.brdf_lut;
            environment_sampler = global_environment.sampler;
        }
        gpu_pbr_bound = gpu_pbr_bound &&
            environment_irradiance != nullptr &&
            environment_specular != nullptr &&
            environment_brdf != nullptr &&
            environment_sampler != nullptr;
        for (std::size_t source_index = 0U; source_index < 3U && gpu_pbr_bound; ++source_index) {
            const std::uint32_t base = 16U + static_cast<std::uint32_t>(source_index) * 3U;
            gpu_pbr_bound =
                commands->set_graphics_texture(
                    gpu_pbr_lighting_texture_binding(base), *environment_irradiance
                ) &&
                commands->set_graphics_texture(
                    gpu_pbr_lighting_texture_binding(base + 1U), *environment_specular
                ) &&
                commands->set_graphics_texture(
                    gpu_pbr_lighting_texture_binding(base + 2U), *environment_brdf
                );
        }
        gpu_pbr_bound = gpu_pbr_bound && commands->set_graphics_sampler(
            gpu_pbr_lighting_sampler_binding(1U), *environment_sampler
        );
        if (!gpu_pbr_bound) {
            set_diagnostics(diagnostics, "VICE Runtime could not bind indexed GPU PBR resources.");
            return false;
        }

        for (std::size_t batch_index = 0U; batch_index < gpu_pbr_plan.batches.size(); ++batch_index) {
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
    }

    for (const auto& instance : instances) {
        if (!instance.material.has_value()) continue;
        const auto* const direct_material = state_->materials.catalog().find(*instance.material);
        if (direct_material != nullptr &&
            render::render_material_uses_gpu_scene_pbr(direct_material->asset.version)) {
            continue;
        }
'''
scene = replace_once(scene, hdr_anchor, pbr_draw_block, "gpu pbr hdr draw and legacy skip")

scene_path.write_text(scene)

shader_path = Path("assets/shaders/vice_gpu_pbr_surface.hlsl")
shader = shader_path.read_text()

shader = replace_once(
    shader,
    '''float3 point_shadow_relative_position(uint slot, float3 model_position) {
    return slot == 0
        ? mul(point_shadow_model_to_light_0, float4(model_position, 1.0)).xyz
        : mul(point_shadow_model_to_light_1, float4(model_position, 1.0)).xyz;
}

float point_shadow_visibility(uint slot, float3 model_position) {
    const float4 metadata = point_shadow_metadata(slot);
    if (metadata.x < 0.5 || metadata.z <= 0.0) return 1.0;
    const float3 relative_position = point_shadow_relative_position(slot, model_position);
''',
    '''float3 point_shadow_relative_position(uint slot, float3 world_position) {
    const float4 metadata = point_shadow_metadata(slot);
    const uint light_index = (uint)metadata.y;
    return world_position - lights[light_index].position_or_direction.xyz;
}

float point_shadow_visibility(uint slot, float3 world_position) {
    const float4 metadata = point_shadow_metadata(slot);
    if (metadata.x < 0.5 || metadata.z <= 0.0) return 1.0;
    const float3 relative_position = point_shadow_relative_position(slot, world_position);
''',
    "batch-safe point shadow position",
)
shader = replace_once(
    shader,
    'visibility = point_shadow_visibility(0, input.model_position);',
    'visibility = point_shadow_visibility(0, input.world_position);',
    "point shadow slot zero call",
)
shader = replace_once(
    shader,
    'visibility = point_shadow_visibility(1, input.model_position);',
    'visibility = point_shadow_visibility(1, input.world_position);',
    "point shadow slot one call",
)
shader = replace_once(
    shader,
    'const float3 camera_relative = mul(atmosphere_camera_from_model, float4(input.model_position, 1.0)).xyz;',
    'const float3 camera_relative = input.world_position - camera_position.xyz;',
    "batch-safe aerial perspective",
)
shader_path.write_text(shader)
