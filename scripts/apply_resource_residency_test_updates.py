from pathlib import Path

# GPU material table semantic-slot expectations.
path = Path('tests/runtime_gpu_material_table_tests.cpp')
text = path.read_text()
text = text.replace(
'''    if (forward.plan.material_ids != reverse.plan.material_ids ||
        forward.plan.texture_ids != reverse.plan.texture_ids ||
        forward.plan.samplers != reverse.plan.samplers ||
''',
'''    if (forward.plan.material_ids != reverse.plan.material_ids ||
        forward.plan.texture_ids != reverse.plan.texture_ids ||
        forward.plan.texture_channels != reverse.plan.texture_channels ||
        forward.plan.samplers != reverse.plan.samplers ||
''', 1)
text = text.replace(
'''    if (forward.plan.records.size() != 3U ||
        forward.plan.texture_ids.size() != 2U ||
        forward.plan.samplers.size() != 2U) {
''',
'''    if (forward.plan.records.size() != 3U ||
        forward.plan.texture_ids.size() != 4U ||
        forward.plan.texture_channels.size() != 4U ||
        forward.plan.samplers.size() != 2U) {
''', 1)
text = text.replace(
'''    if (!empty || !empty.plan.records.empty() || !empty.plan.texture_ids.empty() ||
        !empty.plan.samplers.empty()) {
''',
'''    if (!empty || !empty.plan.records.empty() || !empty.plan.texture_ids.empty() ||
        !empty.plan.texture_channels.empty() || !empty.plan.samplers.empty()) {
''', 1)
path.write_text(text)

# Actual texture residency transitions and semantic GPU resource binding.
path = Path('tests/runtime_gpu_material_resources_tests.cpp')
text = path.read_text()
anchor = '''    assert(textures.initialize(*device, root, &diagnostics));
    assert(diagnostics.empty());

    RuntimeGpuMaterialSampler linear_repeat;
'''
insert = '''    assert(textures.initialize(*device, root, &diagnostics));
    assert(diagnostics.empty());
    const world::ResourceId texture_a_resource{texture_a_id};
    const world::ResourceId texture_b_resource{texture_b_id};
    const world::ResourceId missing_resource{missing_id};
    assert(textures.resident(texture_a_resource));
    assert(textures.resident(texture_b_resource));
    assert(textures.resident_bytes() == 8U);
    const auto initial_generation = textures.residency_generation();
    assert(textures.set_residency({}, &diagnostics));
    assert(textures.residency_generation() == initial_generation + 1U);
    assert(textures.find(texture_a_resource) == nullptr);
    assert(textures.find(texture_b_resource) == nullptr);
    assert(textures.resident_bytes() == 0U);
    auto* const fallback_base = textures.resolve(
        texture_a_resource, detail::RuntimeMaterialTextureChannel::base_color
    );
    auto* const fallback_normal = textures.resolve(
        texture_b_resource, detail::RuntimeMaterialTextureChannel::normal
    );
    assert(fallback_base != nullptr);
    assert(fallback_normal != nullptr);
    assert(fallback_base != fallback_normal);
    assert(textures.resolve(
        missing_resource, detail::RuntimeMaterialTextureChannel::base_color
    ) == nullptr);
    const std::array resident_a{texture_a_resource};
    assert(textures.set_residency(resident_a, &diagnostics));
    assert(textures.find(texture_a_resource) != nullptr);
    assert(textures.find(texture_b_resource) == nullptr);
    assert(textures.resident_bytes() == 4U);
    assert(textures.resolve(
        texture_b_resource, detail::RuntimeMaterialTextureChannel::normal
    ) == fallback_normal);
    const std::array resident_all{texture_a_resource, texture_b_resource};
    assert(textures.set_residency(resident_all, &diagnostics));
    assert(textures.find(texture_a_resource) != nullptr);
    assert(textures.find(texture_b_resource) != nullptr);
    assert(textures.resident_bytes() == 8U);

    RuntimeGpuMaterialSampler linear_repeat;
'''
if anchor not in text:
    raise SystemExit('texture residency test anchor not found')
text = text.replace(anchor, insert, 1)

old = '''    RuntimeGpuMaterialTablePlan resident_plan;
    resident_plan.texture_ids = {
        world::ResourceId{texture_b_id},
        world::ResourceId{texture_a_id},
    };
    resident_plan.samplers = {linear_repeat};
    assert(resources.initialize(*device, resident_plan, materials, &diagnostics));
    assert(diagnostics.empty());
    view = resources.view();
    assert(view.textures.size() == 2U);
    assert(view.textures[0] == gpu_scene_binding.normal_texture);
    assert(view.textures[1] == gpu_scene_binding.base_color_texture);
'''
new = '''    RuntimeGpuMaterialTablePlan resident_plan;
    resident_plan.texture_ids = {
        world::ResourceId{texture_b_id},
        world::ResourceId{texture_a_id},
    };
    resident_plan.texture_channels = {
        RuntimeGpuMaterialTextureChannel::normal,
        RuntimeGpuMaterialTextureChannel::base_color,
    };
    resident_plan.samplers = {linear_repeat};
    assert(resources.initialize(*device, resident_plan, materials, &diagnostics));
    assert(diagnostics.empty());
    view = resources.view();
    assert(view.textures.size() == 2U);
    assert(view.textures[0] == gpu_scene_binding.normal_texture);
    assert(view.textures[1] == gpu_scene_binding.base_color_texture);

    assert(materials.set_texture_residency(resident_all, &diagnostics));
    const detail::RuntimeMaterialBinding resident_binding = materials.find(world::ResourceId{material_id});
    assert(resident_binding.base_color_texture != nullptr);
    assert(resident_binding.normal_texture != nullptr);
    assert(resident_binding.base_color_texture != gpu_scene_binding.base_color_texture);
    assert(resident_binding.normal_texture != gpu_scene_binding.normal_texture);
    assert(materials.resident_texture_bytes() == 8U);
    assert(resources.initialize(*device, resident_plan, materials, &diagnostics));
    assert(diagnostics.empty());
    view = resources.view();
    assert(view.textures[0] == resident_binding.normal_texture);
    assert(view.textures[1] == resident_binding.base_color_texture);
'''
if old not in text:
    raise SystemExit('material sharing test anchor not found')
text = text.replace(old, new, 1)
path.write_text(text)
