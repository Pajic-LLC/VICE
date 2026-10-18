from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {count}")
    file.write_text(text.replace(old, new, 1))


# Public Runtime shadow helpers.
replace_once(
    "engine/runtime/include/vice/runtime/scene.hpp",
    "#include <cstddef>\n",
    "#include <cstddef>\n#include <cstdint>\n",
)
replace_once(
    "engine/runtime/include/vice/runtime/scene.hpp",
    """inline constexpr std::size_t max_runtime_lights = 8U;\n\nstruct RuntimeLight {\n""",
    """inline constexpr std::size_t max_runtime_lights = 8U;\ninline constexpr std::uint32_t runtime_shadow_map_size = 2048U;\n\nstruct RuntimeLight {\n""",
)
replace_once(
    "engine/runtime/include/vice/runtime/scene.hpp",
    """[[nodiscard]] std::vector<RuntimeLight> collect_runtime_lights(const world::World& world);\n\n[[nodiscard]] std::vector<RenderMeshInstance> collect_render_mesh_instances(\n""",
    """struct RuntimeShadowView {\n    world::PersistentId light_entity{};\n    std::size_t light_index = 0;\n    math::Mat4 view_projection{};\n};\n\n[[nodiscard]] std::vector<RuntimeLight> collect_runtime_lights(const world::World& world);\n\n[[nodiscard]] std::optional<std::size_t> select_runtime_shadow_light(\n    std::span<const RuntimeLight> lights\n) noexcept;\n\n[[nodiscard]] std::optional<RuntimeShadowView> directional_shadow_view(\n    std::span<const RenderMeshInstance> instances,\n    const CookedMeshCatalog& catalog,\n    std::span<const RuntimeLight> lights\n) noexcept;\n\n[[nodiscard]] std::vector<RenderMeshInstance> collect_render_mesh_instances(\n""",
)

# Scene constants keep the existing prefix intact for older cooked shaders.
replace_once(
    "engine/runtime/src/scene.cpp",
    """struct SceneConstants {\n    math::Mat4 view_projection{};\n    math::Mat4 model{};\n    float camera_position[4]{};\n    float light_metadata[4]{};\n    std::array<SceneLightConstants, max_runtime_lights> lights{};\n};\n\nstatic_assert(sizeof(SceneLightConstants) == 48U);\nstatic_assert(sizeof(SceneConstants) == 544U);\n""",
    """struct SceneConstants {\n    math::Mat4 view_projection{};\n    math::Mat4 model{};\n    float camera_position[4]{};\n    float light_metadata[4]{};\n    std::array<SceneLightConstants, max_runtime_lights> lights{};\n    math::Mat4 shadow_view_projection{};\n    float shadow_metadata[4]{};\n};\n\nstruct ShadowConstants {\n    math::Mat4 view_projection{};\n    math::Mat4 model{};\n};\n\nstatic_assert(sizeof(SceneLightConstants) == 48U);\nstatic_assert(sizeof(SceneConstants) == 624U);\nstatic_assert(sizeof(ShadowConstants) == 128U);\n""",
)

# Built-in depth-only shadow vertex shaders.
replace_once(
    "engine/runtime/src/scene.cpp",
    ")\";\n\n[[nodiscard]] constexpr math::Vec3 add(\n",
    ")\";\n\nconstexpr std::string_view shadow_hlsl_source = R\"(\ncbuffer ShadowFrame : register(b0) {\n    float4x4 view_projection;\n    float4x4 model;\n};\nstruct VertexIn {\n    float3 position : POSITION0;\n};\nfloat4 vice_runtime_shadow_vs(VertexIn input) : SV_Position {\n    const float4 world_position = mul(model, float4(input.position, 1.0));\n    return mul(view_projection, world_position);\n}\n)\";\n\nconstexpr std::string_view shadow_metal_source = R\"(\n#include <metal_stdlib>\nusing namespace metal;\nstruct ShadowFrame {\n    float4x4 view_projection;\n    float4x4 model;\n};\nstruct VertexIn {\n    float3 position [[attribute(11)]];\n};\nvertex float4 vice_runtime_shadow_vs(\n    VertexIn input [[stage_in]],\n    constant ShadowFrame& frame [[buffer(0)]]) {\n    return frame.view_projection * frame.model * float4(input.position, 1.0);\n}\n)\";\n\n[[nodiscard]] constexpr math::Vec3 add(\n",
)

# Shared scene bounds for deterministic shadow fitting.
replace_once(
    "engine/runtime/src/scene.cpp",
    """struct FallbackCamera {\n    math::Mat4 view_projection{};\n    math::Vec3 position{};\n};\n\n[[nodiscard]] FallbackCamera fallback_camera(\n""",
    """struct SceneBounds {\n    math::Vec3 center{};\n    float radius = 1.0F;\n};\n\n[[nodiscard]] std::optional<SceneBounds> scene_bounds(\n    const std::span<const RenderMeshInstance> instances,\n    const CookedMeshCatalog& catalog\n) noexcept {\n    if (instances.empty()) return std::nullopt;\n\n    math::Vec3 minimum{\n        static_cast<float>(instances.front().spatial.global_position.x),\n        static_cast<float>(instances.front().spatial.global_position.y),\n        static_cast<float>(instances.front().spatial.global_position.z),\n    };\n    math::Vec3 maximum = minimum;\n    for (const auto& instance : instances) {\n        const math::Vec3 position{\n            static_cast<float>(instance.spatial.global_position.x),\n            static_cast<float>(instance.spatial.global_position.y),\n            static_cast<float>(instance.spatial.global_position.z),\n        };\n        float radius = 0.5F;\n        if (const auto* record = catalog.find(instance.mesh); record != nullptr) {\n            const float scale = std::max({\n                std::abs(instance.authored.scale.x),\n                std::abs(instance.authored.scale.y),\n                std::abs(instance.authored.scale.z),\n            });\n            radius = std::max(radius, mesh_radius(record->asset) * scale);\n        }\n        minimum.x = std::min(minimum.x, position.x - radius);\n        minimum.y = std::min(minimum.y, position.y - radius);\n        minimum.z = std::min(minimum.z, position.z - radius);\n        maximum.x = std::max(maximum.x, position.x + radius);\n        maximum.y = std::max(maximum.y, position.y + radius);\n        maximum.z = std::max(maximum.z, position.z + radius);\n    }\n\n    const math::Vec3 center{\n        (minimum.x + maximum.x) * 0.5F,\n        (minimum.y + maximum.y) * 0.5F,\n        (minimum.z + maximum.z) * 0.5F,\n    };\n    const math::Vec3 extent{\n        maximum.x - minimum.x,\n        maximum.y - minimum.y,\n        maximum.z - minimum.z,\n    };\n    return SceneBounds{\n        .center = center,\n        .radius = std::max(1.0F, 0.5F * std::sqrt(math::dot(extent, extent))),\n    };\n}\n\nstruct FallbackCamera {\n    math::Mat4 view_projection{};\n    math::Vec3 position{};\n};\n\n[[nodiscard]] FallbackCamera fallback_camera(\n""",
)

# Dedicated shadow constant buffer.
replace_once(
    "engine/runtime/src/scene.cpp",
    """[[nodiscard]] std::unique_ptr<render::Buffer> create_constant_buffer(render::Device& device) {\n    render::BufferDesc desc;\n    desc.size = sizeof(SceneConstants);\n    desc.usage = render::BufferUsage::uniform;\n    desc.memory = render::MemoryLocation::cpu_to_gpu;\n    desc.debug_name = \"VICE Runtime scene constants\";\n    return device.create_buffer(desc);\n}\n\n[[nodiscard]] float mesh_radius(\n""",
    """[[nodiscard]] std::unique_ptr<render::Buffer> create_constant_buffer(render::Device& device) {\n    render::BufferDesc desc;\n    desc.size = sizeof(SceneConstants);\n    desc.usage = render::BufferUsage::uniform;\n    desc.memory = render::MemoryLocation::cpu_to_gpu;\n    desc.debug_name = \"VICE Runtime scene constants\";\n    return device.create_buffer(desc);\n}\n\n[[nodiscard]] std::unique_ptr<render::Buffer> create_shadow_constant_buffer(\n    render::Device& device\n) {\n    render::BufferDesc desc;\n    desc.size = sizeof(ShadowConstants);\n    desc.usage = render::BufferUsage::uniform;\n    desc.memory = render::MemoryLocation::cpu_to_gpu;\n    desc.debug_name = \"VICE Runtime shadow constants\";\n    return device.create_buffer(desc);\n}\n\n[[nodiscard]] float mesh_radius(\n""",
)

# Runtime shadow-light selection and fitted directional view.
replace_once(
    "engine/runtime/src/scene.cpp",
    """math::Mat4 runtime_model_matrix(const RenderMeshInstance& instance) noexcept {\n""",
    """std::optional<std::size_t> select_runtime_shadow_light(\n    const std::span<const RuntimeLight> lights\n) noexcept {\n    for (std::size_t index = 0; index < lights.size(); ++index) {\n        if (lights[index].light.enabled &&\n            lights[index].light.type == world::LightType::directional) {\n            return index;\n        }\n    }\n    return std::nullopt;\n}\n\nstd::optional<RuntimeShadowView> directional_shadow_view(\n    const std::span<const RenderMeshInstance> instances,\n    const CookedMeshCatalog& catalog,\n    const std::span<const RuntimeLight> lights\n) noexcept {\n    const auto light_index = select_runtime_shadow_light(lights);\n    const auto bounds = scene_bounds(instances, catalog);\n    if (!light_index.has_value() || !bounds.has_value()) return std::nullopt;\n\n    const auto& light = lights[*light_index];\n    const math::Vec3 direction = math::normalize(light.direction);\n    if (math::dot(direction, direction) < 0.99F) return std::nullopt;\n\n    const float radius = std::max(1.0F, bounds->radius);\n    const float distance = radius * 2.0F + 1.0F;\n    const math::Vec3 eye = add(bounds->center, scaled(direction, -distance));\n    math::Vec3 up{0.0F, 1.0F, 0.0F};\n    if (std::abs(math::dot(direction, up)) > 0.95F) {\n        up = {0.0F, 0.0F, 1.0F};\n    }\n    const float extent = radius * 1.15F;\n    const float near_plane = 0.1F;\n    const float far_plane = distance + radius * 2.0F + 1.0F;\n    return RuntimeShadowView{\n        .light_entity = light.entity,\n        .light_index = *light_index,\n        .view_projection = math::multiply(\n            math::orthographic_rh_zo(\n                -extent, extent, -extent, extent, near_plane, far_plane\n            ),\n            math::look_at_rh(eye, bounds->center, up)\n        ),\n    };\n}\n\nmath::Mat4 runtime_model_matrix(const RenderMeshInstance& instance) noexcept {\n""",
)

# Renderer state owns shadow GPU resources and separate per-entity shadow constants.
replace_once(
    "engine/runtime/src/scene.cpp",
    """    std::unique_ptr<render::ShaderModule> fragment_shader;\n    std::unique_ptr<render::GraphicsPipeline> pipeline;\n    std::unique_ptr<render::Texture> depth;\n    std::unordered_map<world::ResourceId, MeshGpu, world::ResourceIdHash> meshes;\n    std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash> constants;\n""",
    """    std::unique_ptr<render::ShaderModule> fragment_shader;\n    std::unique_ptr<render::GraphicsPipeline> pipeline;\n    std::unique_ptr<render::ShaderModule> shadow_vertex_shader;\n    std::unique_ptr<render::GraphicsPipeline> shadow_pipeline;\n    std::unique_ptr<render::Texture> shadow_map;\n    std::unique_ptr<render::Texture> depth;\n    std::unordered_map<world::ResourceId, MeshGpu, world::ResourceIdHash> meshes;\n    std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash> constants;\n    std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash> shadow_constants;\n""",
)

# Create shadow shader, depth-only pipeline, and sampled depth texture.
replace_once(
    "engine/runtime/src/scene.cpp",
    """    if (!state_->pipeline) {\n        set_diagnostics(diagnostics, \"VICE Runtime could not create the fallback scene graphics pipeline.\");\n        return false;\n    }\n\n    for (const auto& record : state_->catalog.records()) {\n""",
    """    if (!state_->pipeline) {\n        set_diagnostics(diagnostics, \"VICE Runtime could not create the fallback scene graphics pipeline.\");\n        return false;\n    }\n\n    render::ShaderModuleDesc shadow_vertex_desc;\n    shadow_vertex_desc.stage = render::ShaderStage::vertex;\n    shadow_vertex_desc.language = vertex_desc.language;\n    shadow_vertex_desc.source = metal ? shadow_metal_source : shadow_hlsl_source;\n    shadow_vertex_desc.resources = vertex_resources;\n    shadow_vertex_desc.entry_point = \"vice_runtime_shadow_vs\";\n    shadow_vertex_desc.debug_name = \"VICE Runtime directional shadow vertex\";\n    state_->shadow_vertex_shader = device.create_shader_module(shadow_vertex_desc);\n    if (!state_->shadow_vertex_shader) {\n        set_diagnostics(diagnostics, \"VICE Runtime could not create the directional shadow shader.\");\n        return false;\n    }\n\n    render::GraphicsPipelineDesc shadow_pipeline_desc;\n    shadow_pipeline_desc.vertex_shader = state_->shadow_vertex_shader.get();\n    shadow_pipeline_desc.fragment_shader = nullptr;\n    shadow_pipeline_desc.color_format = render::TextureFormat::undefined;\n    shadow_pipeline_desc.cull_mode = render::CullMode::none;\n    shadow_pipeline_desc.depth.format = render::TextureFormat::depth32_float;\n    shadow_pipeline_desc.depth.test_enabled = true;\n    shadow_pipeline_desc.depth.write_enabled = true;\n    shadow_pipeline_desc.depth.compare = render::CompareOp::less;\n    shadow_pipeline_desc.vertex_buffers = mesh_layouts;\n    shadow_pipeline_desc.debug_name = \"VICE Runtime directional shadow pipeline\";\n    state_->shadow_pipeline = device.create_graphics_pipeline(shadow_pipeline_desc);\n    if (!state_->shadow_pipeline) {\n        set_diagnostics(diagnostics, \"VICE Runtime could not create the directional shadow pipeline.\");\n        return false;\n    }\n\n    render::TextureDesc shadow_desc;\n    shadow_desc.width = runtime_shadow_map_size;\n    shadow_desc.height = runtime_shadow_map_size;\n    shadow_desc.format = render::TextureFormat::depth32_float;\n    shadow_desc.usage = render::TextureUsage::depth_attachment | render::TextureUsage::sampled;\n    shadow_desc.debug_name = \"VICE Runtime directional shadow map\";\n    state_->shadow_map = device.create_texture(shadow_desc);\n    if (!state_->shadow_map) {\n        set_diagnostics(diagnostics, \"VICE Runtime could not create the directional shadow map.\");\n        return false;\n    }\n\n    for (const auto& record : state_->catalog.records()) {\n""",
)

# Compute authored lights once and derive the shadow view before encoding passes.
replace_once(
    "engine/runtime/src/scene.cpp",
    """    const math::Vec3 camera_position = authored_camera.has_value()\n        ? authored_camera->position\n        : fallback.position;\n\n    auto commands = state_->device->create_command_list();\n""",
    """    const math::Vec3 camera_position = authored_camera.has_value()\n        ? authored_camera->position\n        : fallback.position;\n\n    const auto authored_lights = collect_runtime_lights(world);\n    const auto shadow_view = directional_shadow_view(instances, state_->catalog, authored_lights);\n    auto runtime_lights = authored_lights;\n    if (runtime_lights.empty()) {\n        runtime_lights.push_back(RuntimeLight{\n            .light = world::Light{\n                .type = world::LightType::directional,\n                .color = {1.0F, 0.952381F, 0.880952F},\n                .intensity = 4.2F,\n                .range = 0.0F,\n                .enabled = true,\n            },\n            .direction = {-0.431934F, -0.863868F, -0.259161F},\n        });\n    }\n\n    auto commands = state_->device->create_command_list();\n""",
)

# Encode the depth-only shadow pass before the swapchain pass.
replace_once(
    "engine/runtime/src/scene.cpp",
    """    if (!commands) {\n        set_diagnostics(diagnostics, \"VICE Runtime could not create scene commands.\");\n        return false;\n    }\n\n    render::RenderPassDesc pass;\n""",
    """    if (!commands) {\n        set_diagnostics(diagnostics, \"VICE Runtime could not create scene commands.\");\n        return false;\n    }\n\n    if (shadow_view.has_value()) {\n        if (!commands->begin_depth_render_pass(*state_->shadow_map, 1.0F, true) ||\n            !commands->set_viewport(render::Viewport{\n                .width = static_cast<float>(runtime_shadow_map_size),\n                .height = static_cast<float>(runtime_shadow_map_size),\n            }) ||\n            !commands->set_scissor_rect(render::ScissorRect{\n                .width = runtime_shadow_map_size,\n                .height = runtime_shadow_map_size,\n            }) ||\n            !commands->set_graphics_pipeline(*state_->shadow_pipeline)) {\n            set_diagnostics(diagnostics, \"VICE Runtime could not begin the directional shadow pass.\");\n            return false;\n        }\n\n        for (const auto& instance : instances) {\n            auto shadow_iterator = state_->shadow_constants.find(instance.entity);\n            if (shadow_iterator == state_->shadow_constants.end()) {\n                auto constants = create_shadow_constant_buffer(*state_->device);\n                if (!constants) {\n                    set_diagnostics(diagnostics, \"VICE Runtime could not create shadow constants.\");\n                    return false;\n                }\n                shadow_iterator = state_->shadow_constants.emplace(\n                    instance.entity, std::move(constants)\n                ).first;\n            }\n\n            const ShadowConstants shadow_constants{\n                .view_projection = shadow_view->view_projection,\n                .model = runtime_model_matrix(instance),\n            };\n            if (!shadow_iterator->second->write(\n                    0, std::as_bytes(std::span{&shadow_constants, 1})\n                )) {\n                set_diagnostics(diagnostics, \"VICE Runtime could not update shadow constants.\");\n                return false;\n            }\n\n            auto& mesh = state_->meshes.at(instance.mesh);\n            if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||\n                !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||\n                !commands->set_graphics_constant_buffer(\n                    frame_binding,\n                    *shadow_iterator->second,\n                    0,\n                    sizeof(ShadowConstants)\n                ) ||\n                !commands->draw_indexed(mesh.index_count)) {\n                set_diagnostics(\n                    diagnostics,\n                    \"VICE Runtime could not encode directional shadow mesh `\" +\n                        instance.mesh.value.to_string() + \"`.\"\n                );\n                return false;\n            }\n        }\n        if (!commands->end_render_pass()) {\n            set_diagnostics(diagnostics, \"VICE Runtime could not finish the directional shadow pass.\");\n            return false;\n        }\n    }\n\n    render::RenderPassDesc pass;\n""",
)

# Main-pass constants consume the already collected light set and append shadow metadata.
old_lights = """        auto runtime_lights = collect_runtime_lights(world);\n        if (runtime_lights.empty()) {\n            runtime_lights.push_back(RuntimeLight{\n                .light = world::Light{\n                    .type = world::LightType::directional,\n                    .color = {1.0F, 0.952381F, 0.880952F},\n                    .intensity = 4.2F,\n                    .range = 0.0F,\n                    .enabled = true,\n                },\n                .direction = {-0.431934F, -0.863868F, -0.259161F},\n            });\n        }\n\n        SceneConstants constants{\n            .view_projection = view_projection,\n            .model = runtime_model_matrix(instance),\n            .camera_position = {camera_position.x, camera_position.y, camera_position.z, 1.0F},\n        };\n"""
new_lights = """        SceneConstants constants{\n            .view_projection = view_projection,\n            .model = runtime_model_matrix(instance),\n            .camera_position = {camera_position.x, camera_position.y, camera_position.z, 1.0F},\n        };\n"""
replace_once("engine/runtime/src/scene.cpp", old_lights, new_lights)
replace_once(
    "engine/runtime/src/scene.cpp",
    """        for (std::size_t light_index = 0; light_index < runtime_lights.size(); ++light_index) {\n            const auto& source = runtime_lights[light_index];\n            auto& destination = constants.lights[light_index];\n""",
    """        for (std::size_t light_index = 0; light_index < runtime_lights.size(); ++light_index) {\n            const auto& source = runtime_lights[light_index];\n            auto& destination = constants.lights[light_index];\n""",
)
replace_once(
    "engine/runtime/src/scene.cpp",
    """            destination.parameters[0] = source.light.range;\n        }\n        if (!constants_iterator->second->write(0, std::as_bytes(std::span{&constants, 1}))) {\n""",
    """            destination.parameters[0] = source.light.range;\n        }\n        if (shadow_view.has_value()) {\n            constants.shadow_view_projection = shadow_view->view_projection;\n            constants.shadow_metadata[0] = 1.0F;\n            constants.shadow_metadata[1] = static_cast<float>(shadow_view->light_index);\n            constants.shadow_metadata[2] = 1.0F / static_cast<float>(runtime_shadow_map_size);\n            constants.shadow_metadata[3] = 0.0015F;\n        }\n        if (!constants_iterator->second->write(0, std::as_bytes(std::span{&constants, 1}))) {\n""",
)

# Runtime light selection regression.
replace_once(
    "tests/runtime_light_tests.cpp",
    """    assert(lights.front().light.type == world::LightType::directional);\n""",
    """    assert(lights.front().light.type == world::LightType::directional);\n    assert(runtime::select_runtime_shadow_light(lights) == std::optional<std::size_t>{0U});\n""",
)
replace_once(
    "tests/runtime_light_tests.cpp",
    """    assert(lights.front().entity == id(1));\n    assert(lights.back().entity == id(8));\n    return 0;\n""",
    """    assert(lights.front().entity == id(1));\n    assert(lights.back().entity == id(8));\n    assert(!runtime::select_runtime_shadow_light(lights).has_value());\n    return 0;\n""",
)
replace_once(
    "tests/runtime_light_tests.cpp",
    "#include <cmath>\n",
    "#include <cmath>\n#include <optional>\n",
)

# Runtime scene regression for deterministic fitted shadow view and live light rotation.
replace_once(
    "tests/runtime_scene_tests.cpp",
    """    const auto first_camera = runtime::primary_camera_view(world_state, 16.0F / 9.0F);\n""",
    """    require(\n        !runtime::directional_shadow_view(\n            instances, catalog, runtime::collect_runtime_lights(world_state)\n        ).has_value(),\n        \"World without directional light has no shadow view\"\n    );\n\n    const auto first_camera = runtime::primary_camera_view(world_state, 16.0F / 9.0F);\n""",
)
replace_once(
    "tests/runtime_scene_tests.cpp",
    """    require(\n        matrix_differs(moved_camera->view_projection, rotated_camera->view_projection),\n        \"current authored camera rotation drives view matrix\"\n    );\n\n    std::filesystem::remove_all(root, error);\n""",
    """    require(\n        matrix_differs(moved_camera->view_projection, rotated_camera->view_projection),\n        \"current authored camera rotation drives view matrix\"\n    );\n\n    const auto light_id = world::PersistentId::parse(\"52000000-0000-4000-8000-000000000004\");\n    require(light_id.has_value(), \"shadow light ID parse\");\n    const auto light_entity = world_state.create_entity(*light_id);\n    require(light_entity.has_value(), \"shadow light entity creates\");\n    require(\n        world_state.set_spatial_state(\n            *light_entity,\n            world::SpatialState{\n                .global_position = {0.0, 10.0, 0.0},\n                .region = region.id,\n            }\n        ),\n        \"shadow light spatial state\"\n    );\n    require(\n        world_state.set_light(\n            *light_entity,\n            world::Light{\n                .type = world::LightType::directional,\n                .color = {1.0F, 1.0F, 1.0F},\n                .intensity = 3.0F,\n                .range = 0.0F,\n                .enabled = true,\n            }\n        ),\n        \"directional shadow light authored\"\n    );\n    auto shadow_lights = runtime::collect_runtime_lights(world_state);\n    const auto first_shadow = runtime::directional_shadow_view(instances, catalog, shadow_lights);\n    require(first_shadow.has_value(), \"directional shadow view resolves\");\n    require(first_shadow->light_entity == *light_id, \"shadow view keeps light identity\");\n    require(first_shadow->light_index == 0U, \"first directional light is selected deterministically\");\n    for (const float value : first_shadow->view_projection.values) {\n        require(std::isfinite(value), \"shadow matrix is finite\");\n    }\n\n    require(\n        world_state.set_authored_transform(\n            *light_entity,\n            world::AuthoredTransform{\n                .rotation = {0.0F, 0.70710678F, 0.0F, 0.70710678F},\n                .scale = {1.0F, 1.0F, 1.0F},\n            }\n        ),\n        \"shadow light rotation updates\"\n    );\n    shadow_lights = runtime::collect_runtime_lights(world_state);\n    const auto rotated_shadow = runtime::directional_shadow_view(instances, catalog, shadow_lights);\n    require(rotated_shadow.has_value(), \"rotated directional shadow view resolves\");\n    require(\n        matrix_differs(first_shadow->view_projection, rotated_shadow->view_projection),\n        \"authored directional rotation drives shadow view\"\n    );\n\n    std::filesystem::remove_all(root, error);\n""",
)
