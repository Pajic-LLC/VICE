from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {count}; anchor={old[:220]!r}")
    file.write_text(text.replace(old, new, 1))


def replace_between(path: str, start_marker: str, end_marker: str, replacement: str) -> None:
    file = Path(path)
    text = file.read_text()
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    file.write_text(text[:start] + replacement + text[end:])


# Runtime material v2 accepts the optional paired t1/s1 directional-shadow resources.
replace_between(
    "engine/runtime/src/material_pipeline.cpp",
    "[[nodiscard]] bool valid_v2_fragment_resources",
    "[[nodiscard]] std::unique_ptr<render::Buffer> create_material_constants",
    """struct V2FragmentResourceContract {\n    bool valid = false;\n    bool supports_directional_shadow = false;\n};\n\n[[nodiscard]] V2FragmentResourceContract v2_fragment_resource_contract(\n    const render::ShaderModule& module\n) noexcept {\n    bool material_constants = false;\n    bool base_color_texture = false;\n    bool base_color_sampler = false;\n    bool shadow_texture = false;\n    bool shadow_sampler = false;\n    for (const auto& resource : module.resources()) {\n        if (resource.space != 0U || resource.count != 1U || resource.unbounded) return {};\n        switch (resource.register_class) {\n        case render::ShaderRegisterClass::constant_buffer:\n            if (resource.binding != 0U && resource.binding != 1U) return {};\n            if (resource.binding == 1U) material_constants = true;\n            break;\n        case render::ShaderRegisterClass::shader_resource:\n            if (resource.binding == 0U) base_color_texture = true;\n            else if (resource.binding == 1U) shadow_texture = true;\n            else return {};\n            break;\n        case render::ShaderRegisterClass::sampler:\n            if (resource.binding == 0U) base_color_sampler = true;\n            else if (resource.binding == 1U) shadow_sampler = true;\n            else return {};\n            break;\n        case render::ShaderRegisterClass::unordered_access:\n            return {};\n        }\n    }\n    return V2FragmentResourceContract{\n        .valid = material_constants && base_color_texture && base_color_sampler &&\n            shadow_texture == shadow_sampler,\n        .supports_directional_shadow = shadow_texture && shadow_sampler,\n    };\n}\n\n""",
)
replace_once(
    "engine/runtime/src/material_pipeline.cpp",
    """        const bool textured = record.asset.version >= render::render_material_asset_format_version_v2;\n        const bool resources_valid = textured\n            ? valid_v2_vertex_resources(*vertex.module) && valid_v2_fragment_resources(*fragment.module)\n            : valid_v1_resources(*vertex.module, 0U) && valid_v1_resources(*fragment.module, 1U);\n""",
    """        const bool textured = record.asset.version >= render::render_material_asset_format_version_v2;\n        bool supports_directional_shadow = false;\n        bool resources_valid = false;\n        if (textured) {\n            const auto fragment_contract = v2_fragment_resource_contract(*fragment.module);\n            resources_valid = valid_v2_vertex_resources(*vertex.module) && fragment_contract.valid;\n            supports_directional_shadow = fragment_contract.supports_directional_shadow;\n        } else {\n            resources_valid = valid_v1_resources(*vertex.module, 0U) &&\n                valid_v1_resources(*fragment.module, 1U);\n        }\n""",
)
replace_once(
    "engine/runtime/src/material_pipeline.cpp",
    "        std::unique_ptr<render::Sampler> sampler;\n",
    "        std::unique_ptr<render::Sampler> sampler;\n        bool supports_directional_shadow = false;\n",
)
replace_once(
    "engine/runtime/src/material_pipeline.cpp",
    """            .base_color_texture = base_color_texture,\n            .sampler = std::move(sampler),\n        });\n""",
    """            .base_color_texture = base_color_texture,\n            .sampler = std::move(sampler),\n            .supports_directional_shadow = supports_directional_shadow,\n        });\n""",
)
replace_once(
    "engine/runtime/src/material_pipeline.cpp",
    """        .base_color_texture = found->second.base_color_texture,\n        .sampler = found->second.sampler.get(),\n    };\n""",
    """        .base_color_texture = found->second.base_color_texture,\n        .sampler = found->second.sampler.get(),\n        .supports_directional_shadow = found->second.supports_directional_shadow,\n    };\n""",
)

# Runtime scene binding keys and shadow sampler.
replace_once(
    "engine/runtime/src/scene.cpp",
    """constexpr render::ShaderBindingKey material_sampler_binding{\n    .register_class = render::ShaderRegisterClass::sampler,\n    .binding = 0,\n    .space = 0,\n    .array_element = 0,\n};\n\nstruct SceneLightConstants {\n""",
    """constexpr render::ShaderBindingKey material_sampler_binding{\n    .register_class = render::ShaderRegisterClass::sampler,\n    .binding = 0,\n    .space = 0,\n    .array_element = 0,\n};\n\nconstexpr render::ShaderBindingKey shadow_texture_binding{\n    .register_class = render::ShaderRegisterClass::shader_resource,\n    .binding = 1,\n    .space = 0,\n    .array_element = 0,\n};\n\nconstexpr render::ShaderBindingKey shadow_sampler_binding{\n    .register_class = render::ShaderRegisterClass::sampler,\n    .binding = 1,\n    .space = 0,\n    .array_element = 0,\n};\n\nstruct SceneLightConstants {\n""",
)
replace_once(
    "engine/runtime/src/scene.cpp",
    "    std::unique_ptr<render::Texture> shadow_map;\n",
    "    std::unique_ptr<render::Texture> shadow_map;\n    std::unique_ptr<render::Sampler> shadow_sampler;\n",
)
replace_once(
    "engine/runtime/src/scene.cpp",
    """    if (!state_->shadow_map) {\n        set_diagnostics(diagnostics, \"VICE Runtime could not create the directional shadow map.\");\n        return false;\n    }\n\n    for (const auto& record : state_->catalog.records()) {\n""",
    """    if (!state_->shadow_map) {\n        set_diagnostics(diagnostics, \"VICE Runtime could not create the directional shadow map.\");\n        return false;\n    }\n\n    render::SamplerDesc shadow_sampler_desc;\n    shadow_sampler_desc.min_filter = render::Filter::nearest;\n    shadow_sampler_desc.mag_filter = render::Filter::nearest;\n    shadow_sampler_desc.mip_filter = render::Filter::nearest;\n    shadow_sampler_desc.address_u = render::AddressMode::clamp_to_edge;\n    shadow_sampler_desc.address_v = render::AddressMode::clamp_to_edge;\n    shadow_sampler_desc.address_w = render::AddressMode::clamp_to_edge;\n    shadow_sampler_desc.debug_name = \"VICE Runtime directional shadow sampler\";\n    state_->shadow_sampler = device.create_sampler(shadow_sampler_desc);\n    if (!state_->shadow_sampler) {\n        set_diagnostics(diagnostics, \"VICE Runtime could not create the directional shadow sampler.\");\n        return false;\n    }\n\n    for (const auto& record : state_->catalog.records()) {\n""",
)
replace_once(
    "engine/runtime/src/scene.cpp",
    """        if (!commands->draw_indexed(mesh.index_count)) {\n            set_diagnostics(\n                diagnostics,\n                \"VICE Runtime could not encode mesh `\" + instance.mesh.value.to_string() + \"`.\"\n            );\n            return false;\n        }\n""",
    """        if (material_state.supports_directional_shadow &&\n            (!commands->set_graphics_texture(shadow_texture_binding, *state_->shadow_map) ||\n                !commands->set_graphics_sampler(shadow_sampler_binding, *state_->shadow_sampler))) {\n            set_diagnostics(\n                diagnostics,\n                \"VICE Runtime could not bind the directional shadow map for mesh `\" +\n                    instance.mesh.value.to_string() + \"`.\"\n            );\n            return false;\n        }\n        if (!commands->draw_indexed(mesh.index_count)) {\n            set_diagnostics(\n                diagnostics,\n                \"VICE Runtime could not encode mesh `\" + instance.mesh.value.to_string() + \"`.\"\n            );\n            return false;\n        }\n""",
)
