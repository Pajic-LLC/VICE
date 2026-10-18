from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text()


def write(path: str, text: str) -> None:
    Path(path).write_text(text)


def replace_once(text: str, old: str, new: str, path: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {count}: {old[:160]!r}")
    return text.replace(old, new, 1)


scene_path = "engine/runtime/src/scene.cpp"
scene = read(scene_path)

scene = replace_once(
    scene,
    '''constexpr render::ShaderBindingKey shadow_texture_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1,
    .space = 0,
    .array_element = 0,
};
''',
    '''static constexpr std::array shadow_texture_bindings{
    render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 1,
        .space = 0,
        .array_element = 0,
    },
    render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 2,
        .space = 0,
        .array_element = 0,
    },
    render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 3,
        .space = 0,
        .array_element = 0,
    },
    render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 4,
        .space = 0,
        .array_element = 0,
    },
};
''',
    scene_path,
)

scene = replace_once(
    scene,
    '''    math::Mat4 shadow_view_projection{};
    float shadow_metadata[4]{};
};

struct ShadowConstants {
''',
    '''    math::Mat4 shadow_view_projection{};
    float shadow_metadata[4]{};
    std::array<math::Mat4, runtime_shadow_cascade_count - 1U> shadow_cascade_view_projections{};
    float shadow_cascade_splits[4]{};
    float shadow_camera_forward[4]{};
};

struct ShadowConstants {
''',
    scene_path,
)
scene = replace_once(
    scene,
    "static_assert(sizeof(SceneConstants) == 624U);",
    "static_assert(sizeof(SceneConstants) == 848U);",
    scene_path,
)

bounds_start = scene.index("struct SceneBounds {")
bounds_end = scene.index("struct FallbackCamera {", bounds_start)
scene = scene[:bounds_start] + scene[bounds_end:]

fallback_start = scene.index("struct FallbackCamera {")
fallback_end = scene.index("void set_diagnostics", fallback_start)
new_fallback = '''[[nodiscard]] RuntimeCameraView fallback_camera(
    const std::span<const RenderMeshInstance> instances,
    const CookedMeshCatalog& catalog,
    const float aspect
) noexcept {
    if (instances.empty()) {
        const math::Vec3 target{};
        const math::Vec3 eye{6.0F, 4.5F, 8.0F};
        const math::Vec3 forward = math::normalize(target - eye);
        const math::Vec3 up{0.0F, 1.0F, 0.0F};
        const world::Camera camera{
            .projection = world::CameraProjection::perspective,
            .vertical_fov_radians = runtime_fov,
            .near_plane = 0.05F,
            .far_plane = 250.0F,
            .primary = false,
        };
        return RuntimeCameraView{
            .camera = camera,
            .position = eye,
            .forward = forward,
            .up = up,
            .aspect_ratio = aspect,
            .view_projection = math::multiply(
                math::perspective_rh_zo(
                    camera.vertical_fov_radians,
                    aspect,
                    camera.near_plane,
                    camera.far_plane
                ),
                math::look_at_rh(eye, target, up)
            ),
        };
    }

    math::Vec3 minimum{
        static_cast<float>(instances.front().spatial.global_position.x),
        static_cast<float>(instances.front().spatial.global_position.y),
        static_cast<float>(instances.front().spatial.global_position.z),
    };
    math::Vec3 maximum = minimum;

    for (const auto& instance : instances) {
        const math::Vec3 position{
            static_cast<float>(instance.spatial.global_position.x),
            static_cast<float>(instance.spatial.global_position.y),
            static_cast<float>(instance.spatial.global_position.z),
        };
        float radius = 0.5F;
        if (const auto* record = catalog.find(instance.mesh); record != nullptr) {
            const float scale = std::max({
                std::abs(instance.authored.scale.x),
                std::abs(instance.authored.scale.y),
                std::abs(instance.authored.scale.z),
            });
            radius = std::max(radius, mesh_radius(record->asset) * scale);
        }
        minimum.x = std::min(minimum.x, position.x - radius);
        minimum.y = std::min(minimum.y, position.y - radius);
        minimum.z = std::min(minimum.z, position.z - radius);
        maximum.x = std::max(maximum.x, position.x + radius);
        maximum.y = std::max(maximum.y, position.y + radius);
        maximum.z = std::max(maximum.z, position.z + radius);
    }

    const math::Vec3 target{
        (minimum.x + maximum.x) * 0.5F,
        (minimum.y + maximum.y) * 0.5F,
        (minimum.z + maximum.z) * 0.5F,
    };
    const math::Vec3 extent{
        maximum.x - minimum.x,
        maximum.y - minimum.y,
        maximum.z - minimum.z,
    };
    const float radius = std::max(1.0F, 0.5F * std::sqrt(math::dot(extent, extent)));
    const float distance = std::max(5.0F, radius / std::tan(runtime_fov * 0.5F) * 1.35F);
    const math::Vec3 direction = math::normalize({1.0F, 0.7F, 1.0F});
    const math::Vec3 eye = add(target, scaled(direction, distance));
    const math::Vec3 forward = math::normalize(target - eye);
    const math::Vec3 up{0.0F, 1.0F, 0.0F};
    const float far_plane = std::max(250.0F, distance + radius * 6.0F);
    const world::Camera camera{
        .projection = world::CameraProjection::perspective,
        .vertical_fov_radians = runtime_fov,
        .near_plane = 0.05F,
        .far_plane = far_plane,
        .primary = false,
    };

    return RuntimeCameraView{
        .camera = camera,
        .position = eye,
        .forward = forward,
        .up = up,
        .aspect_ratio = aspect,
        .view_projection = math::multiply(
            math::perspective_rh_zo(
                camera.vertical_fov_radians,
                aspect,
                camera.near_plane,
                camera.far_plane
            ),
            math::look_at_rh(eye, target, up)
        ),
    };
}

'''
scene = scene[:fallback_start] + new_fallback + scene[fallback_end:]

shadow_start = scene.index("std::optional<RuntimeShadowView> directional_shadow_view(")
shadow_end = scene.index("math::Mat4 runtime_model_matrix", shadow_start)
new_shadow = '''std::array<float, runtime_shadow_cascade_count> runtime_shadow_cascade_splits(
    const RuntimeCameraView& camera
) noexcept {
    std::array<float, runtime_shadow_cascade_count> splits{};
    const float near_plane = std::max(0.001F, camera.camera.near_plane);
    const float far_plane = std::min(camera.camera.far_plane, runtime_shadow_distance);
    if (!std::isfinite(camera.camera.vertical_fov_radians) ||
        !std::isfinite(camera.aspect_ratio) ||
        !std::isfinite(near_plane) || !std::isfinite(far_plane) ||
        camera.camera.vertical_fov_radians <= 0.0F || camera.aspect_ratio <= 0.0F ||
        far_plane <= near_plane) {
        return splits;
    }

    for (std::size_t index = 0; index < splits.size(); ++index) {
        const float ratio = static_cast<float>(index + 1U) /
            static_cast<float>(splits.size());
        const float logarithmic = near_plane * std::pow(far_plane / near_plane, ratio);
        const float uniform = near_plane + (far_plane - near_plane) * ratio;
        splits[index] = runtime_shadow_split_lambda * logarithmic +
            (1.0F - runtime_shadow_split_lambda) * uniform;
    }
    splits.back() = far_plane;
    return splits;
}

namespace {

[[nodiscard]] std::array<math::Vec3, 8U> camera_slice_corners(
    const RuntimeCameraView& camera,
    const float near_plane,
    const float far_plane
) noexcept {
    std::array<math::Vec3, 8U> corners{};
    const math::Vec3 forward = math::normalize(camera.forward);
    const math::Vec3 up = math::normalize(camera.up);
    const math::Vec3 right = math::normalize(math::cross(forward, up));
    const float tangent = std::tan(camera.camera.vertical_fov_radians * 0.5F);

    const auto write_plane = [&](const float distance, const std::size_t offset) {
        const float half_height = tangent * distance;
        const float half_width = half_height * camera.aspect_ratio;
        const math::Vec3 center = add(camera.position, scaled(forward, distance));
        const math::Vec3 horizontal = scaled(right, half_width);
        const math::Vec3 vertical = scaled(up, half_height);
        corners[offset + 0U] = add(add(center, horizontal), vertical);
        corners[offset + 1U] = add(add(center, scaled(horizontal, -1.0F)), vertical);
        corners[offset + 2U] = add(add(center, horizontal), scaled(vertical, -1.0F));
        corners[offset + 3U] = add(
            add(center, scaled(horizontal, -1.0F)),
            scaled(vertical, -1.0F)
        );
    };

    write_plane(near_plane, 0U);
    write_plane(far_plane, 4U);
    return corners;
}

[[nodiscard]] RuntimeShadowCascade fitted_shadow_cascade(
    const RuntimeCameraView& camera,
    const RuntimeLight& light,
    const float split_near,
    const float split_far
) noexcept {
    const auto corners = camera_slice_corners(camera, split_near, split_far);
    math::Vec3 center{};
    for (const auto corner : corners) center = add(center, scaled(corner, 1.0F / 8.0F));

    float radius = 1.0F;
    for (const auto corner : corners) {
        const math::Vec3 offset = corner - center;
        radius = std::max(radius, std::sqrt(math::dot(offset, offset)));
    }
    radius *= 1.05F;
    radius = std::ceil(radius * 16.0F) / 16.0F;

    const math::Vec3 direction = math::normalize(light.direction);
    const math::Vec3 backward = scaled(direction, -1.0F);
    math::Vec3 up_reference{0.0F, 1.0F, 0.0F};
    if (std::abs(math::dot(direction, up_reference)) > 0.95F) {
        up_reference = {0.0F, 0.0F, 1.0F};
    }
    const math::Vec3 light_right = math::normalize(math::cross(up_reference, backward));
    const math::Vec3 light_up = math::cross(backward, light_right);

    const float texel_size = (radius * 2.0F) /
        static_cast<float>(runtime_shadow_map_size);
    const float center_right = math::dot(light_right, center);
    const float center_up = math::dot(light_up, center);
    const float snapped_right = std::round(center_right / texel_size) * texel_size;
    const float snapped_up = std::round(center_up / texel_size) * texel_size;
    const math::Vec3 snapped_center = add(
        center,
        add(
            scaled(light_right, snapped_right - center_right),
            scaled(light_up, snapped_up - center_up)
        )
    );

    const float distance = radius * 4.0F + 50.0F;
    const math::Vec3 eye = add(snapped_center, scaled(direction, -distance));
    const float near_depth = 0.1F;
    const float far_depth = distance * 2.0F;
    return RuntimeShadowCascade{
        .split_near = split_near,
        .split_far = split_far,
        .view_projection = math::multiply(
            math::orthographic_rh_zo(
                -radius,
                radius,
                -radius,
                radius,
                near_depth,
                far_depth
            ),
            math::look_at_rh(eye, snapped_center, up_reference)
        ),
    };
}

} // namespace

std::optional<RuntimeShadowView> directional_shadow_view(
    const RuntimeCameraView& camera,
    const std::span<const RuntimeLight> lights
) noexcept {
    const auto light_index = select_runtime_shadow_light(lights);
    if (!light_index.has_value()) return std::nullopt;

    const auto& light = lights[*light_index];
    const math::Vec3 direction = math::normalize(light.direction);
    if (math::dot(direction, direction) < 0.99F) return std::nullopt;

    const auto splits = runtime_shadow_cascade_splits(camera);
    if (splits.front() <= camera.camera.near_plane || splits.back() <= splits.front()) {
        return std::nullopt;
    }

    RuntimeShadowView result{
        .light_entity = light.entity,
        .light_index = *light_index,
    };
    float split_near = std::max(0.001F, camera.camera.near_plane);
    for (std::size_t index = 0; index < result.cascades.size(); ++index) {
        result.cascades[index] = fitted_shadow_cascade(
            camera,
            light,
            split_near,
            splits[index]
        );
        split_near = splits[index];
    }
    return result;
}

'''
scene = scene[:shadow_start] + new_shadow + scene[shadow_end:]

scene = replace_once(
    scene,
    '''    return RuntimeCameraView{
        .entity = *persistent_id,
        .camera = *camera,
        .position = eye,
        .view_projection = math::multiply(
''',
    '''    return RuntimeCameraView{
        .entity = *persistent_id,
        .camera = *camera,
        .position = eye,
        .forward = forward,
        .up = up,
        .aspect_ratio = aspect_ratio,
        .view_projection = math::multiply(
''',
    scene_path,
)

scene = replace_once(
    scene,
    '''    std::unique_ptr<render::Texture> shadow_map;
    std::unique_ptr<render::Sampler> shadow_sampler;
''',
    '''    std::array<std::unique_ptr<render::Texture>, runtime_shadow_cascade_count> shadow_maps;
    std::unique_ptr<render::Sampler> shadow_sampler;
''',
    scene_path,
)
scene = replace_once(
    scene,
    '''    std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash> shadow_constants;
''',
    '''    std::array<
        std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash>,
        runtime_shadow_cascade_count
    > shadow_constants;
''',
    scene_path,
)

shadow_create_old = '''    render::TextureDesc shadow_desc;
    shadow_desc.width = runtime_shadow_map_size;
    shadow_desc.height = runtime_shadow_map_size;
    shadow_desc.format = render::TextureFormat::depth32_float;
    shadow_desc.usage = render::TextureUsage::depth_attachment | render::TextureUsage::sampled;
    shadow_desc.debug_name = "VICE Runtime directional shadow map";
    state_->shadow_map = device.create_texture(shadow_desc);
    if (!state_->shadow_map) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the directional shadow map.");
        return false;
    }
'''
shadow_create_new = '''    render::TextureDesc shadow_desc;
    shadow_desc.width = runtime_shadow_map_size;
    shadow_desc.height = runtime_shadow_map_size;
    shadow_desc.format = render::TextureFormat::depth32_float;
    shadow_desc.usage = render::TextureUsage::depth_attachment | render::TextureUsage::sampled;
    shadow_desc.debug_name = "VICE Runtime directional shadow cascade";
    for (auto& shadow_map : state_->shadow_maps) {
        shadow_map = device.create_texture(shadow_desc);
        if (!shadow_map) {
            set_diagnostics(diagnostics, "VICE Runtime could not create a directional shadow cascade.");
            return false;
        }
    }
'''
scene = replace_once(scene, shadow_create_old, shadow_create_new, scene_path)

scene = replace_once(
    scene,
    '''    const auto authored_camera = primary_camera_view(world, aspect);
    const auto fallback = fallback_camera(instances, state_->catalog, aspect);
    const math::Mat4 view_projection = authored_camera.has_value()
        ? authored_camera->view_projection
        : fallback.view_projection;
    const math::Vec3 camera_position = authored_camera.has_value()
        ? authored_camera->position
        : fallback.position;

    const auto authored_lights = collect_runtime_lights(world);
    const auto shadow_view = directional_shadow_view(instances, state_->catalog, authored_lights);
''',
    '''    const auto authored_camera = primary_camera_view(world, aspect);
    const RuntimeCameraView runtime_camera = authored_camera.has_value()
        ? *authored_camera
        : fallback_camera(instances, state_->catalog, aspect);
    const math::Mat4 view_projection = runtime_camera.view_projection;
    const math::Vec3 camera_position = runtime_camera.position;

    const auto authored_lights = collect_runtime_lights(world);
    const auto shadow_view = directional_shadow_view(runtime_camera, authored_lights);
''',
    scene_path,
)

pass_start = scene.index("    if (shadow_view.has_value()) {", scene.index("auto commands = state_->device->create_command_list()"))
pass_end = scene.index("    render::RenderPassDesc pass;", pass_start)
new_pass = '''    if (shadow_view.has_value()) {
        for (std::size_t cascade_index = 0; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
            if (!commands->begin_depth_render_pass(*state_->shadow_maps[cascade_index], 1.0F, true) ||
                !commands->set_viewport(render::Viewport{
                    .width = static_cast<float>(runtime_shadow_map_size),
                    .height = static_cast<float>(runtime_shadow_map_size),
                }) ||
                !commands->set_scissor_rect(render::ScissorRect{
                    .width = runtime_shadow_map_size,
                    .height = runtime_shadow_map_size,
                }) ||
                !commands->set_graphics_pipeline(*state_->shadow_pipeline)) {
                set_diagnostics(diagnostics, "VICE Runtime could not begin a directional shadow cascade.");
                return false;
            }

            auto& cascade_constants = state_->shadow_constants[cascade_index];
            for (const auto& instance : instances) {
                auto shadow_iterator = cascade_constants.find(instance.entity);
                if (shadow_iterator == cascade_constants.end()) {
                    auto constants = create_shadow_constant_buffer(*state_->device);
                    if (!constants) {
                        set_diagnostics(diagnostics, "VICE Runtime could not create shadow constants.");
                        return false;
                    }
                    shadow_iterator = cascade_constants.emplace(
                        instance.entity, std::move(constants)
                    ).first;
                }

                const ShadowConstants shadow_constants{
                    .view_projection = shadow_view->cascades[cascade_index].view_projection,
                    .model = runtime_model_matrix(instance),
                };
                if (!shadow_iterator->second->write(
                        0, std::as_bytes(std::span{&shadow_constants, 1})
                    )) {
                    set_diagnostics(diagnostics, "VICE Runtime could not update shadow constants.");
                    return false;
                }

                auto& mesh = state_->meshes.at(instance.mesh);
                if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                    !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                    !commands->set_graphics_constant_buffer(
                        frame_binding,
                        *shadow_iterator->second,
                        0,
                        sizeof(ShadowConstants)
                    ) ||
                    !commands->draw_indexed(mesh.index_count)) {
                    set_diagnostics(
                        diagnostics,
                        "VICE Runtime could not encode directional shadow mesh `" +
                            instance.mesh.value.to_string() + "`."
                    );
                    return false;
                }
            }
            if (!commands->end_render_pass()) {
                set_diagnostics(diagnostics, "VICE Runtime could not finish a directional shadow cascade.");
                return false;
            }
        }
    }

'''
scene = scene[:pass_start] + new_pass + scene[pass_end:]

scene = replace_once(
    scene,
    '''        if (shadow_view.has_value()) {
            constants.shadow_view_projection = shadow_view->view_projection;
            constants.shadow_metadata[0] = 1.0F;
            constants.shadow_metadata[1] = static_cast<float>(shadow_view->light_index);
            constants.shadow_metadata[2] = 1.0F / static_cast<float>(runtime_shadow_map_size);
            constants.shadow_metadata[3] = 0.0015F;
        }
''',
    '''        if (shadow_view.has_value()) {
            constants.shadow_view_projection = shadow_view->cascades[0].view_projection;
            for (std::size_t cascade_index = 1; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
                constants.shadow_cascade_view_projections[cascade_index - 1U] =
                    shadow_view->cascades[cascade_index].view_projection;
            }
            for (std::size_t cascade_index = 0; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
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
''',
    scene_path,
)

scene = replace_once(
    scene,
    '''        if (material_state.supports_directional_shadow &&
            (!commands->set_graphics_texture(shadow_texture_binding, *state_->shadow_map) ||
                !commands->set_graphics_sampler(shadow_sampler_binding, *state_->shadow_sampler))) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not bind the directional shadow map for mesh `" +
                    instance.mesh.value.to_string() + "`."
            );
            return false;
        }
''',
    '''        if (material_state.directional_shadow_cascade_count > 0U) {
            const std::size_t cascade_count = std::min<std::size_t>(
                material_state.directional_shadow_cascade_count,
                runtime_shadow_cascade_count
            );
            bool shadows_bound = state_->shadow_sampler != nullptr;
            for (std::size_t cascade_index = 0; cascade_index < cascade_count && shadows_bound; ++cascade_index) {
                shadows_bound = commands->set_graphics_texture(
                    shadow_texture_bindings[cascade_index],
                    *state_->shadow_maps[cascade_index]
                );
            }
            shadows_bound = shadows_bound && commands->set_graphics_sampler(
                shadow_sampler_binding,
                *state_->shadow_sampler
            );
            if (!shadows_bound) {
                set_diagnostics(
                    diagnostics,
                    "VICE Runtime could not bind directional shadow cascades for mesh `" +
                        instance.mesh.value.to_string() + "`."
                );
                return false;
            }
        }
''',
    scene_path,
)

write(scene_path, scene)

material_path = "engine/runtime/src/material_pipeline.cpp"
material = read(material_path)
material = replace_once(
    material,
    '''struct V2FragmentResourceContract {
    bool valid = false;
    bool supports_directional_shadow = false;
};
''',
    '''struct V2FragmentResourceContract {
    bool valid = false;
    std::uint32_t directional_shadow_cascade_count = 0;
};
''',
    material_path,
)

contract_start = material.index("[[nodiscard]] V2FragmentResourceContract v2_fragment_resource_contract(")
contract_end = material.index("[[nodiscard]] std::unique_ptr<render::Buffer> create_material_constants", contract_start)
new_contract = '''[[nodiscard]] V2FragmentResourceContract v2_fragment_resource_contract(
    const render::ShaderModule& module
) noexcept {
    bool material_constants = false;
    bool base_color_texture = false;
    bool base_color_sampler = false;
    std::array<bool, runtime_shadow_cascade_count> shadow_textures{};
    bool shadow_sampler = false;
    for (const auto& resource : module.resources()) {
        if (resource.space != 0U || resource.count != 1U || resource.unbounded) return {};
        switch (resource.register_class) {
        case render::ShaderRegisterClass::constant_buffer:
            if (resource.binding != 0U && resource.binding != 1U) return {};
            if (resource.binding == 1U) material_constants = true;
            break;
        case render::ShaderRegisterClass::shader_resource:
            if (resource.binding == 0U) {
                base_color_texture = true;
            } else if (resource.binding >= 1U &&
                resource.binding <= runtime_shadow_cascade_count) {
                shadow_textures[resource.binding - 1U] = true;
            } else {
                return {};
            }
            break;
        case render::ShaderRegisterClass::sampler:
            if (resource.binding == 0U) base_color_sampler = true;
            else if (resource.binding == 1U) shadow_sampler = true;
            else return {};
            break;
        case render::ShaderRegisterClass::unordered_access:
            return {};
        }
    }

    const bool any_shadow_texture = std::any_of(
        shadow_textures.begin(), shadow_textures.end(), [](const bool value) { return value; }
    );
    const bool all_shadow_textures = std::all_of(
        shadow_textures.begin(), shadow_textures.end(), [](const bool value) { return value; }
    );
    const bool legacy_shadow_set = shadow_textures[0] && shadow_sampler &&
        std::none_of(
            shadow_textures.begin() + 1,
            shadow_textures.end(),
            [](const bool value) { return value; }
        );
    const bool cascaded_shadow_set = all_shadow_textures && shadow_sampler;
    const bool no_shadow_set = !any_shadow_texture && !shadow_sampler;
    const bool shadow_set_valid = no_shadow_set || legacy_shadow_set || cascaded_shadow_set;

    return V2FragmentResourceContract{
        .valid = material_constants && base_color_texture && base_color_sampler && shadow_set_valid,
        .directional_shadow_cascade_count = cascaded_shadow_set
            ? static_cast<std::uint32_t>(runtime_shadow_cascade_count)
            : legacy_shadow_set ? 1U : 0U,
    };
}

'''
material = material[:contract_start] + new_contract + material[contract_end:]

material = replace_once(
    material,
    "        bool supports_directional_shadow = false;",
    "        std::uint32_t directional_shadow_cascade_count = 0;",
    material_path,
)
material = replace_once(
    material,
    '''        bool supports_directional_shadow = false;
        bool resources_valid = false;
''',
    '''        std::uint32_t directional_shadow_cascade_count = 0;
        bool resources_valid = false;
''',
    material_path,
)
material = replace_once(
    material,
    "            supports_directional_shadow = fragment_contract.supports_directional_shadow;",
    "            directional_shadow_cascade_count = fragment_contract.directional_shadow_cascade_count;",
    material_path,
)
material = replace_once(
    material,
    "            .supports_directional_shadow = supports_directional_shadow,",
    "            .directional_shadow_cascade_count = directional_shadow_cascade_count,",
    material_path,
)
material = replace_once(
    material,
    "        .supports_directional_shadow = found->second.supports_directional_shadow,",
    "        .directional_shadow_cascade_count = found->second.directional_shadow_cascade_count,",
    material_path,
)
write(material_path, material)

test_path = "tests/runtime_scene_tests.cpp"
test = read(test_path)
test = replace_once(
    test,
    '''[[nodiscard]] bool matrix_differs(
    const vice::math::Mat4& left,
    const vice::math::Mat4& right
) noexcept {
    for (std::size_t index = 0; index < left.values.size(); ++index) {
        if (std::abs(left.values[index] - right.values[index]) > 0.0001F) return true;
    }
    return false;
}
''',
    '''[[nodiscard]] bool matrix_differs(
    const vice::math::Mat4& left,
    const vice::math::Mat4& right
) noexcept {
    for (std::size_t index = 0; index < left.values.size(); ++index) {
        if (std::abs(left.values[index] - right.values[index]) > 0.0001F) return true;
    }
    return false;
}

[[nodiscard]] bool matrix_matches(
    const vice::math::Mat4& left,
    const vice::math::Mat4& right,
    const float epsilon = 0.000001F
) noexcept {
    for (std::size_t index = 0; index < left.values.size(); ++index) {
        if (std::abs(left.values[index] - right.values[index]) > epsilon) return false;
    }
    return true;
}
''',
    test_path,
)

old_camera_section = '''    require(
        !runtime::directional_shadow_view(
            instances, catalog, runtime::collect_runtime_lights(world_state)
        ).has_value(),
        "World without directional light has no shadow view"
    );

    const auto first_camera = runtime::primary_camera_view(world_state, 16.0F / 9.0F);
    require(first_camera.has_value(), "runtime resolves authored primary camera");
    require(first_camera->entity == *camera_id, "runtime camera identity");
    require(first_camera->camera.primary, "runtime camera retains primary flag");
'''
new_camera_section = '''    const auto first_camera = runtime::primary_camera_view(world_state, 16.0F / 9.0F);
    require(first_camera.has_value(), "runtime resolves authored primary camera");
    require(first_camera->entity == *camera_id, "runtime camera identity");
    require(first_camera->camera.primary, "runtime camera retains primary flag");
    require(first_camera->aspect_ratio == 16.0F / 9.0F, "runtime camera retains aspect ratio");
    require(
        !runtime::directional_shadow_view(
            *first_camera, runtime::collect_runtime_lights(world_state)
        ).has_value(),
        "World without directional light has no shadow view"
    );
'''
test = replace_once(test, old_camera_section, new_camera_section, test_path)

test = replace_once(
    test,
    "    const auto first_shadow = runtime::directional_shadow_view(instances, catalog, shadow_lights);",
    "    const auto first_shadow = runtime::directional_shadow_view(*rotated_camera, shadow_lights);",
    test_path,
)
old_shadow_checks = '''    require(first_shadow.has_value(), "directional shadow view resolves");
    require(first_shadow->light_entity == *light_id, "shadow view keeps light identity");
    require(first_shadow->light_index == 0U, "first directional light is selected deterministically");
    for (const float value : first_shadow->view_projection.values) {
        require(std::isfinite(value), "shadow matrix is finite");
    }
'''
new_shadow_checks = '''    require(first_shadow.has_value(), "directional shadow view resolves");
    require(first_shadow->light_entity == *light_id, "shadow view keeps light identity");
    require(first_shadow->light_index == 0U, "first directional light is selected deterministically");
    const auto splits = runtime::runtime_shadow_cascade_splits(*rotated_camera);
    require(splits[0] > rotated_camera->camera.near_plane, "first cascade split follows camera near plane");
    require(splits[0] < splits[1] && splits[1] < splits[2] && splits[2] < splits[3], "cascade splits increase");
    require(splits[3] <= runtime::runtime_shadow_distance, "cascade distance is bounded");
    for (std::size_t cascade_index = 0; cascade_index < first_shadow->cascades.size(); ++cascade_index) {
        const auto& cascade = first_shadow->cascades[cascade_index];
        require(cascade.split_far == splits[cascade_index], "cascade stores split far distance");
        require(cascade.split_far > cascade.split_near, "cascade split range is valid");
        for (const float value : cascade.view_projection.values) {
            require(std::isfinite(value), "cascade shadow matrix is finite");
        }
    }
'''
test = replace_once(test, old_shadow_checks, new_shadow_checks, test_path)

test = replace_once(
    test,
    "    const auto rotated_shadow = runtime::directional_shadow_view(instances, catalog, shadow_lights);",
    "    const auto rotated_shadow = runtime::directional_shadow_view(*rotated_camera, shadow_lights);",
    test_path,
)
test = replace_once(
    test,
    '''        matrix_differs(first_shadow->view_projection, rotated_shadow->view_projection),
        "authored directional rotation drives shadow view"
    );

    std::filesystem::remove_all(root, error);
''',
    '''        matrix_differs(
            first_shadow->cascades.front().view_projection,
            rotated_shadow->cascades.front().view_projection
        ),
        "authored directional rotation drives shadow view"
    );

    runtime::RuntimeCameraView stable_camera{
        .camera = world::Camera{
            .projection = world::CameraProjection::perspective,
            .vertical_fov_radians = 1.0471975512F,
            .near_plane = 0.1F,
            .far_plane = 500.0F,
            .primary = true,
        },
        .position = {0.0F, 2.0F, 8.0F},
        .forward = {0.0F, 0.0F, -1.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .aspect_ratio = 16.0F / 9.0F,
    };
    auto stable_lights = shadow_lights;
    stable_lights.front().direction = {0.0F, -1.0F, 0.0F};
    const auto stable_shadow = runtime::directional_shadow_view(stable_camera, stable_lights);
    require(stable_shadow.has_value(), "stable shadow view resolves");
    stable_camera.position.x += 0.001F;
    const auto sub_texel_shadow = runtime::directional_shadow_view(stable_camera, stable_lights);
    require(sub_texel_shadow.has_value(), "sub-texel moved shadow view resolves");
    for (std::size_t cascade_index = 0; cascade_index < runtime::runtime_shadow_cascade_count; ++cascade_index) {
        require(
            matrix_matches(
                stable_shadow->cascades[cascade_index].view_projection,
                sub_texel_shadow->cascades[cascade_index].view_projection
            ),
            "texel snapping stabilizes sub-texel camera translation"
        );
    }

    std::filesystem::remove_all(root, error);
''',
    test_path,
)
write(test_path, test)
