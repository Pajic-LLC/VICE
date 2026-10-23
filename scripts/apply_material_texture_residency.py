from pathlib import Path

path = Path('engine/runtime/src/material_pipeline.cpp')
text = path.read_text()

replacements = []
replacements.append(('''        render::Texture* base_color_texture = nullptr;
        render::Texture* normal_texture = nullptr;
        render::Texture* orm_texture = nullptr;
        render::Texture* emissive_texture = nullptr;
''', '''        world::ResourceId base_color_texture{};
        world::ResourceId normal_texture{};
        world::ResourceId orm_texture{};
        world::ResourceId emissive_texture{};
'''))
replacements.append(('''    if (!state_->textures.initialize(device, content_root, diagnostics)) return false;
''', '''    if (!state_->textures.initialize(device, content_root, diagnostics, false)) return false;
'''))

old_gpu = '''        if (gpu_scene_pbr) {
            const world::ResourceId base_color_id{world::PersistentId{record.asset.base_color_texture}};
            const world::ResourceId normal_id{world::PersistentId{record.asset.normal_texture}};
            const world::ResourceId orm_id{world::PersistentId{record.asset.orm_texture}};
            const world::ResourceId emissive_id{world::PersistentId{record.asset.emissive_texture}};
            render::Texture* const base_color_texture = state_->textures.find(base_color_id);
            render::Texture* const normal_texture = state_->textures.find(normal_id);
            render::Texture* const orm_texture = state_->textures.find(orm_id);
            render::Texture* const emissive_texture = state_->textures.find(emissive_id);
            if (base_color_texture == nullptr || normal_texture == nullptr ||
                orm_texture == nullptr || emissive_texture == nullptr) {
                set_diagnostics(diagnostics, "Render material `" + record.resource_id.value.to_string() +
                    "` references one or more missing V4 GPU-scene PBR textures.");
                return false;
            }
            state_->materials.emplace(record.resource_id, State::MaterialGpu{
                .base_color_texture = base_color_texture,
                .normal_texture = normal_texture,
                .orm_texture = orm_texture,
                .emissive_texture = emissive_texture,
            });
            continue;
        }
'''
new_gpu = '''        if (gpu_scene_pbr) {
            const world::ResourceId base_color_id{world::PersistentId{record.asset.base_color_texture}};
            const world::ResourceId normal_id{world::PersistentId{record.asset.normal_texture}};
            const world::ResourceId orm_id{world::PersistentId{record.asset.orm_texture}};
            const world::ResourceId emissive_id{world::PersistentId{record.asset.emissive_texture}};
            if (state_->textures.catalog().find(base_color_id) == nullptr ||
                state_->textures.catalog().find(normal_id) == nullptr ||
                state_->textures.catalog().find(orm_id) == nullptr ||
                state_->textures.catalog().find(emissive_id) == nullptr) {
                set_diagnostics(diagnostics, "Render material `" + record.resource_id.value.to_string() +
                    "` references one or more missing V4 GPU-scene PBR textures.");
                return false;
            }
            state_->materials.emplace(record.resource_id, State::MaterialGpu{
                .base_color_texture = base_color_id,
                .normal_texture = normal_id,
                .orm_texture = orm_id,
                .emissive_texture = emissive_id,
            });
            continue;
        }
'''
replacements.append((old_gpu, new_gpu))

old_legacy = '''        render::Texture* base_color_texture = nullptr;
        render::Texture* normal_texture = nullptr;
        render::Texture* orm_texture = nullptr;
        render::Texture* emissive_texture = nullptr;
        std::unique_ptr<render::Sampler> sampler;
        if (textured) {
            const world::ResourceId texture_id{world::PersistentId{record.asset.base_color_texture}};
            base_color_texture = state_->textures.find(texture_id);
            if (base_color_texture == nullptr) {
                set_diagnostics(diagnostics, "Render material `" + record.resource_id.value.to_string() +
                    "` references missing cooked texture `" + texture_id.value.to_string() + "`.");
                return false;
            }
            if (extended_pbr) {
                const world::ResourceId normal_id{world::PersistentId{record.asset.normal_texture}};
                const world::ResourceId orm_id{world::PersistentId{record.asset.orm_texture}};
                const world::ResourceId emissive_id{world::PersistentId{record.asset.emissive_texture}};
                normal_texture = state_->textures.find(normal_id);
                orm_texture = state_->textures.find(orm_id);
                emissive_texture = state_->textures.find(emissive_id);
                if (normal_texture == nullptr || orm_texture == nullptr || emissive_texture == nullptr) {
                    set_diagnostics(diagnostics, "Render material `" + record.resource_id.value.to_string() +
                        "` references one or more missing V3 PBR textures.");
                    return false;
                }
            }
            sampler = create_material_sampler(device, record.asset);
'''
new_legacy = '''        world::ResourceId base_color_texture{};
        world::ResourceId normal_texture{};
        world::ResourceId orm_texture{};
        world::ResourceId emissive_texture{};
        std::unique_ptr<render::Sampler> sampler;
        if (textured) {
            const world::ResourceId texture_id{world::PersistentId{record.asset.base_color_texture}};
            base_color_texture = texture_id;
            if (state_->textures.catalog().find(texture_id) == nullptr) {
                set_diagnostics(diagnostics, "Render material `" + record.resource_id.value.to_string() +
                    "` references missing cooked texture `" + texture_id.value.to_string() + "`.");
                return false;
            }
            if (extended_pbr) {
                const world::ResourceId normal_id{world::PersistentId{record.asset.normal_texture}};
                const world::ResourceId orm_id{world::PersistentId{record.asset.orm_texture}};
                const world::ResourceId emissive_id{world::PersistentId{record.asset.emissive_texture}};
                normal_texture = normal_id;
                orm_texture = orm_id;
                emissive_texture = emissive_id;
                if (state_->textures.catalog().find(normal_id) == nullptr ||
                    state_->textures.catalog().find(orm_id) == nullptr ||
                    state_->textures.catalog().find(emissive_id) == nullptr) {
                    set_diagnostics(diagnostics, "Render material `" + record.resource_id.value.to_string() +
                        "` references one or more missing V3 PBR textures.");
                    return false;
                }
            }
            sampler = create_material_sampler(device, record.asset);
'''
replacements.append((old_legacy, new_legacy))

old_find = '''    return RuntimeMaterialBinding{
        .pipeline = found->second.pipeline.get(),
        .constants = found->second.constants.get(),
        .base_color_texture = found->second.base_color_texture,
        .normal_texture = found->second.normal_texture,
        .orm_texture = found->second.orm_texture,
        .emissive_texture = found->second.emissive_texture,
        .sampler = found->second.sampler.get(),
'''
new_find = '''    const auto resolve = [&](const world::ResourceId texture, const RuntimeMaterialTextureChannel channel) {
        return texture.valid() ? state_->textures.resolve(texture, channel) : nullptr;
    };
    return RuntimeMaterialBinding{
        .pipeline = found->second.pipeline.get(),
        .constants = found->second.constants.get(),
        .base_color_texture = resolve(found->second.base_color_texture, RuntimeMaterialTextureChannel::base_color),
        .normal_texture = resolve(found->second.normal_texture, RuntimeMaterialTextureChannel::normal),
        .orm_texture = resolve(found->second.orm_texture, RuntimeMaterialTextureChannel::orm),
        .emissive_texture = resolve(found->second.emissive_texture, RuntimeMaterialTextureChannel::emissive),
        .sampler = found->second.sampler.get(),
'''
replacements.append((old_find, new_find))

for old, new in replacements:
    if old not in text:
        raise SystemExit('material residency anchor missing:\n' + old[:120])
    text = text.replace(old, new, 1)

anchor = '''const CookedRenderMaterialCatalog& RuntimeMaterialLibrary::catalog() const noexcept {
    return state_->catalog;
}
'''
insert = '''bool RuntimeMaterialLibrary::set_texture_residency(
    const std::span<const world::ResourceId> resources,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime material library state is unavailable.");
        return false;
    }
    return state_->textures.set_residency(resources, diagnostics);
}

render::Texture* RuntimeMaterialLibrary::resolve_texture(
    const world::ResourceId resource_id,
    const RuntimeMaterialTextureChannel channel
) const noexcept {
    return state_ != nullptr ? state_->textures.resolve(resource_id, channel) : nullptr;
}

const CookedRenderMaterialCatalog& RuntimeMaterialLibrary::catalog() const noexcept {
    return state_->catalog;
}

const CookedTextureCatalog& RuntimeMaterialLibrary::texture_catalog() const noexcept {
    return state_->textures.catalog();
}

std::uint64_t RuntimeMaterialLibrary::texture_residency_generation() const noexcept {
    return state_ != nullptr ? state_->textures.residency_generation() : 0U;
}

std::uint64_t RuntimeMaterialLibrary::resident_texture_bytes() const noexcept {
    return state_ != nullptr ? state_->textures.resident_bytes() : 0U;
}
'''
if anchor not in text:
    raise SystemExit('material footer anchor missing')
text = text.replace(anchor, insert, 1)
path.write_text(text)
