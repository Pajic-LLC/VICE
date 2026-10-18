from pathlib import Path


def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


scene_path = Path("engine/runtime/src/scene.cpp")
scene = scene_path.read_text()

scene = once(scene, '''static constexpr std::array mesh_attributes{
    render::VertexAttributeDesc{
        .semantic = "POSITION",
        .semantic_index = 0,
        .shader_location = 0,
        .format = render::VertexFormat::float32x3,
        .offset = 0,
    },
    render::VertexAttributeDesc{
        .semantic = "COLOR",
        .semantic_index = 0,
        .shader_location = 1,
        .format = render::VertexFormat::float32x3,
        .offset = 12,
    },
};
''', '''static constexpr std::array mesh_attributes{
    render::VertexAttributeDesc{
        .semantic = "POSITION",
        .semantic_index = 0,
        .shader_location = 0,
        .format = render::VertexFormat::float32x3,
        .offset = 0,
    },
    render::VertexAttributeDesc{
        .semantic = "COLOR",
        .semantic_index = 0,
        .shader_location = 1,
        .format = render::VertexFormat::float32x3,
        .offset = 12,
    },
    render::VertexAttributeDesc{
        .semantic = "NORMAL",
        .semantic_index = 0,
        .shader_location = 2,
        .format = render::VertexFormat::float32x3,
        .offset = 24,
    },
    render::VertexAttributeDesc{
        .semantic = "TEXCOORD",
        .semantic_index = 0,
        .shader_location = 3,
        .format = render::VertexFormat::float32x2,
        .offset = 36,
    },
};
''', "mesh surface attributes")

scene = once(scene, '''constexpr render::ShaderBindingKey material_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 1,
    .space = 0,
    .array_element = 0,
};

struct SceneConstants {
    math::Mat4 view_projection{};
    math::Mat4 model{};
};

static_assert(sizeof(SceneConstants) == 128U);
''', '''constexpr render::ShaderBindingKey material_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 1,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey base_color_texture_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey material_sampler_binding{
    .register_class = render::ShaderRegisterClass::sampler,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

struct SceneConstants {
    math::Mat4 view_projection{};
    math::Mat4 model{};
    float camera_position[4]{};
    float light_direction[4]{};
    float light_color[4]{};
};

static_assert(sizeof(SceneConstants) == 176U);
''', "scene PBR constants")

scene = once(scene, '''[[nodiscard]] math::Mat4 fallback_view_projection(
    const std::span<const RenderMeshInstance> instances,
    const CookedMeshCatalog& catalog,
    const float aspect
) noexcept {
''', '''struct FallbackCamera {
    math::Mat4 view_projection{};
    math::Vec3 position{};
};

[[nodiscard]] FallbackCamera fallback_camera(
    const std::span<const RenderMeshInstance> instances,
    const CookedMeshCatalog& catalog,
    const float aspect
) noexcept {
''', "fallback camera signature")

scene = once(scene, '''        return math::multiply(
            math::perspective_rh_zo(runtime_fov, aspect, 0.05F, 250.0F),
            math::look_at_rh(eye, target, {0.0F, 1.0F, 0.0F})
        );
''', '''        return FallbackCamera{
            .view_projection = math::multiply(
                math::perspective_rh_zo(runtime_fov, aspect, 0.05F, 250.0F),
                math::look_at_rh(eye, target, {0.0F, 1.0F, 0.0F})
            ),
            .position = eye,
        };
''', "empty fallback camera")

scene = once(scene, '''    return math::multiply(
        math::perspective_rh_zo(runtime_fov, aspect, 0.05F, far_plane),
        math::look_at_rh(eye, target, {0.0F, 1.0F, 0.0F})
    );
''', '''    return FallbackCamera{
        .view_projection = math::multiply(
            math::perspective_rh_zo(runtime_fov, aspect, 0.05F, far_plane),
            math::look_at_rh(eye, target, {0.0F, 1.0F, 0.0F})
        ),
        .position = eye,
    };
''', "framed fallback camera")

scene = once(scene, '''    return RuntimeCameraView{
        .entity = *persistent_id,
        .camera = *camera,
        .view_projection = math::multiply(
''', '''    return RuntimeCameraView{
        .entity = *persistent_id,
        .camera = *camera,
        .position = eye,
        .view_projection = math::multiply(
''', "authored camera position")

scene = once(scene, '''    const auto authored_camera = primary_camera_view(world, aspect);
    const math::Mat4 view_projection = authored_camera.has_value()
        ? authored_camera->view_projection
        : fallback_view_projection(instances, state_->catalog, aspect);
''', '''    const auto authored_camera = primary_camera_view(world, aspect);
    const auto fallback = fallback_camera(instances, state_->catalog, aspect);
    const math::Mat4 view_projection = authored_camera.has_value()
        ? authored_camera->view_projection
        : fallback.view_projection;
    const math::Vec3 camera_position = authored_camera.has_value()
        ? authored_camera->position
        : fallback.position;
''', "camera selection")

scene = once(scene, '''        const SceneConstants constants{
            .view_projection = view_projection,
            .model = runtime_model_matrix(instance),
        };
''', '''        const SceneConstants constants{
            .view_projection = view_projection,
            .model = runtime_model_matrix(instance),
            .camera_position = {camera_position.x, camera_position.y, camera_position.z, 1.0F},
            .light_direction = {0.431934F, 0.863868F, 0.259161F, 0.0F},
            .light_color = {4.2F, 4.0F, 3.7F, 1.0F},
        };
''', "per-draw PBR scene constants")

scene = once(scene, '''        render::GraphicsPipeline* pipeline = state_->pipeline.get();
        render::Buffer* material_constants = nullptr;
        if (instance.material.has_value()) {
            const auto material = state_->materials.find(*instance.material);
            pipeline = material.pipeline;
            material_constants = material.constants;
        }
''', '''        render::GraphicsPipeline* pipeline = state_->pipeline.get();
        detail::RuntimeMaterialBinding material_state{};
        if (instance.material.has_value()) {
            material_state = state_->materials.find(*instance.material);
            pipeline = material_state.pipeline;
        }
''', "material binding state")

scene = scene.replace("material_constants != nullptr", "material_state.constants != nullptr", 1)
scene = scene.replace("*material_constants,\n                0,\n                sizeof(float) * 4U", "*material_state.constants,\n                0,\n                sizeof(float) * 8U", 1)

needle = '''        if (material_state.constants != nullptr &&
            !commands->set_graphics_constant_buffer(
                material_binding,
                *material_state.constants,
                0,
                sizeof(float) * 8U
            )) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not bind render material constants for mesh `" +
                    instance.mesh.value.to_string() + "`."
            );
            return false;
        }
'''
replacement = needle + '''        if (material_state.base_color_texture != nullptr &&
            (!commands->set_graphics_texture(
                base_color_texture_binding,
                *material_state.base_color_texture
            ) || material_state.sampler == nullptr ||
            !commands->set_graphics_sampler(material_sampler_binding, *material_state.sampler))) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not bind the PBR texture and sampler for mesh `" +
                    instance.mesh.value.to_string() + "`."
            );
            return false;
        }
'''
scene = once(scene, needle, replacement, "PBR texture bindings")
scene_path.write_text(scene)

cmake = Path("engine/runtime/CMakeLists.txt")
text = cmake.read_text()
text = once(text, "    src/scene.cpp\n", "    src/scene.cpp\n    src/texture_pipeline.cpp\n", "runtime texture source")
cmake.write_text(text)
