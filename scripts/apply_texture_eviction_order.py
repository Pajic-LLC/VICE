from pathlib import Path

path = Path('engine/runtime/src/texture_pipeline.cpp')
text = path.read_text()
old = '''    std::vector<std::pair<world::ResourceId, std::unique_ptr<render::Texture>>> uploads;
    for (const auto resource : resources) {
        if (state_->textures.contains(resource)) continue;
        const auto* const record = state_->catalog.find(resource);
        auto texture = create_texture(*state_->device, *record);
        if (!texture) {
            return fail("Could not upload resident GPU texture `" + resource.value.to_string() + "`.");
        }
        uploads.emplace_back(resource, std::move(texture));
    }

    std::erase_if(state_->textures, [&](const auto& entry) {
        return !requested.contains(entry.first);
    });
    for (auto& upload : uploads) {
        state_->textures.emplace(upload.first, std::move(upload.second));
    }
'''
new = '''    std::erase_if(state_->textures, [&](const auto& entry) {
        return !requested.contains(entry.first);
    });

    for (const auto resource : resources) {
        if (state_->textures.contains(resource)) continue;
        const auto* const record = state_->catalog.find(resource);
        auto texture = create_texture(*state_->device, *record);
        if (!texture) {
            return fail("Could not upload resident GPU texture `" + resource.value.to_string() + "`.");
        }
        state_->textures.emplace(resource, std::move(texture));
    }
'''
if old not in text:
    raise SystemExit('texture eviction ordering anchor not found')
path.write_text(text.replace(old, new, 1))
