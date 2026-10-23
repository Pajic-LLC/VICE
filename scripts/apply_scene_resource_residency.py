from pathlib import Path

path = Path('engine/runtime/src/scene.cpp')
text = path.read_text()

anchor = '#include <vice/runtime/render_streaming.hpp>\n'
replacement = '#include <vice/runtime/render_streaming.hpp>\n#include <vice/runtime/resource_residency.hpp>\n'
if anchor not in text:
    raise SystemExit('resource residency include anchor not found')
text = text.replace(anchor, replacement, 1)

anchor = '''    RuntimePostProcessPlan post_process_plan = default_runtime_post_process_plan();
    std::size_t history_write_index = 0U;
    float exposure = 1.0F;
'''
replacement = '''    RuntimePostProcessPlan post_process_plan = default_runtime_post_process_plan();
    RuntimeRenderResourceBudgets resource_budgets{};
    std::uint64_t gpu_material_texture_generation = 0U;
    std::size_t history_write_index = 0U;
    float exposure = 1.0F;
'''
if anchor not in text:
    raise SystemExit('renderer state budget anchor not found')
text = text.replace(anchor, replacement, 1)

start = text.find('''    for (const auto& record : state_->catalog.records()) {
        auto vertices = create_buffer_with_data(
''')
end_anchor = '''
    state_->initialized = true;
'''
if start < 0:
    raise SystemExit('eager mesh upload start not found')
end = text.find(end_anchor, start)
if end < 0:
    raise SystemExit('eager mesh upload end not found')
text = text[:start] + end_anchor + text[end + len(end_anchor):]

anchor = '''bool RuntimeSceneRenderer::render(
'''
setter = '''bool RuntimeSceneRenderer::set_resource_budgets(
    const RuntimeRenderResourceBudgets budgets,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->initialized) {
        set_diagnostics(diagnostics, "Runtime scene renderer is not initialized.");
        return false;
    }
    state_->resource_budgets = budgets;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeSceneRenderer::render(
'''
if anchor not in text:
    raise SystemExit('render function anchor not found')
text = text.replace(anchor, setter, 1)

old = '''    const auto& resident_scene = resident_scene_build.scene;
    const auto& instances = resident_scene.meshes;
    for (const auto& instance : instances) {
        if (!state_->meshes.contains(instance.mesh)) {
            set_diagnostics(
                diagnostics,
                "Runtime World references missing cooked mesh `" + instance.mesh.value.to_string() + "`."
            );
            return false;
        }
        if (instance.material.has_value()) {
'''
new = '''    const auto& resident_scene = resident_scene_build.scene;
    auto resource_residency_build = build_runtime_render_resource_residency(
        resident_scene,
        state_->catalog,
        state_->materials.catalog(),
        state_->materials.texture_catalog(),
        state_->resource_budgets
    );
    if (!resource_residency_build) {
        set_diagnostics(diagnostics, resource_residency_build.diagnostics);
        return false;
    }
    const auto& resource_residency = resource_residency_build.residency;

    if (!state_->materials.set_texture_residency(
            resource_residency.textures.resources,
            diagnostics
        )) {
        return false;
    }
    const std::uint64_t texture_generation = state_->materials.texture_residency_generation();
    if (texture_generation != state_->gpu_material_texture_generation) {
        if (!state_->gpu_material_scene.refresh_resources(
                *state_->device,
                state_->materials,
                diagnostics
            )) {
            return false;
        }
        state_->gpu_material_texture_generation = texture_generation;
    }

    std::erase_if(state_->meshes, [&](const auto& entry) {
        return !resource_residency.geometry.resident(entry.first);
    });
    for (const world::ResourceId resource : resource_residency.geometry.resources) {
        if (state_->meshes.contains(resource)) continue;
        const auto* const record = state_->catalog.find(resource);
        if (record == nullptr) {
            set_diagnostics(
                diagnostics,
                "Runtime geometry residency references missing cooked mesh `" +
                    resource.value.to_string() + "`."
            );
            return false;
        }
        auto vertices = create_buffer_with_data(
            *state_->device,
            render::BufferUsage::vertex,
            std::as_bytes(std::span{record->asset.vertices}),
            "VICE Runtime resident mesh vertices"
        );
        auto indices = create_buffer_with_data(
            *state_->device,
            render::BufferUsage::index,
            std::as_bytes(std::span{record->asset.indices}),
            "VICE Runtime resident mesh indices"
        );
        if (!vertices || !indices) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not upload resident cooked mesh `" +
                    record->path.generic_string() + "`."
            );
            return false;
        }
        state_->meshes.emplace(
            resource,
            State::MeshGpu{
                .vertices = std::move(vertices),
                .indices = std::move(indices),
                .index_count = static_cast<std::uint32_t>(record->asset.indices.size()),
                .bounds = runtime_mesh_bounding_sphere(record->asset.vertices),
            }
        );
    }

    const auto instances = filter_runtime_resident_geometry(
        resident_scene.meshes,
        resource_residency.geometry
    );
    for (const auto& instance : instances) {
        if (instance.material.has_value()) {
'''
if old not in text:
    raise SystemExit('resident scene render anchor not found')
text = text.replace(old, new, 1)

path.write_text(text)
