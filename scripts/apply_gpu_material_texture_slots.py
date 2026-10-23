from pathlib import Path

path = Path('engine/runtime/src/gpu_material_table.cpp')
text = path.read_text()

old = '''[[nodiscard]] bool contains_resource(
    const std::span<const world::ResourceId> resources,
    const world::ResourceId resource
) noexcept {
    return std::find(resources.begin(), resources.end(), resource) != resources.end();
}

[[nodiscard]] std::uint32_t resource_index(
    const std::vector<world::ResourceId>& resources,
    const std::optional<world::ResourceId> resource
) noexcept {
    if (!resource.has_value()) return runtime_gpu_resource_unassigned;
    const auto found = std::lower_bound(
        resources.begin(), resources.end(), *resource, resource_less
    );
    if (found == resources.end() || *found != *resource) return runtime_gpu_resource_unassigned;
    return static_cast<std::uint32_t>(std::distance(resources.begin(), found));
}
'''
new = '''[[nodiscard]] bool contains_resource(
    const std::span<const world::ResourceId> resources,
    const world::ResourceId resource
) noexcept {
    return std::find(resources.begin(), resources.end(), resource) != resources.end();
}

struct TextureSlot {
    world::ResourceId resource{};
    RuntimeGpuMaterialTextureChannel channel = RuntimeGpuMaterialTextureChannel::base_color;

    friend bool operator==(const TextureSlot&, const TextureSlot&) = default;
};

[[nodiscard]] bool texture_slot_less(const TextureSlot& left, const TextureSlot& right) noexcept {
    if (left.resource != right.resource) return resource_less(left.resource, right.resource);
    return static_cast<std::uint8_t>(left.channel) < static_cast<std::uint8_t>(right.channel);
}

[[nodiscard]] std::uint32_t texture_index(
    const std::vector<TextureSlot>& slots,
    const std::optional<world::ResourceId> resource,
    const RuntimeGpuMaterialTextureChannel channel
) noexcept {
    if (!resource.has_value()) return runtime_gpu_resource_unassigned;
    const TextureSlot key{.resource = *resource, .channel = channel};
    const auto found = std::lower_bound(slots.begin(), slots.end(), key, texture_slot_less);
    if (found == slots.end() || *found != key) return runtime_gpu_resource_unassigned;
    return static_cast<std::uint32_t>(std::distance(slots.begin(), found));
}
'''
if old not in text:
    raise SystemExit('resource-index anchor not found')
text = text.replace(old, new, 1)

old = '''    std::vector<world::ResourceId> texture_ids;
    std::vector<RuntimeGpuMaterialSampler> samplers;
    for (const auto* material : ordered_materials) {
        const std::array texture_resources{
            texture_resource(material->asset.base_color_texture),
            texture_resource(material->asset.normal_texture),
            texture_resource(material->asset.orm_texture),
            texture_resource(material->asset.emissive_texture),
        };
        for (const auto& texture : texture_resources) {
            if (!texture.has_value()) continue;
            if (input.validate_texture_references &&
                !contains_resource(available_textures, *texture)) {
                result.diagnostics = "Runtime GPU material `" +
                    material->resource_id.value.to_string() +
                    "` references missing texture `" + texture->value.to_string() + "`.";
                return result;
            }
            texture_ids.push_back(*texture);
        }
        samplers.push_back(sampler_key(material->asset));
    }

    std::sort(texture_ids.begin(), texture_ids.end(), resource_less);
    texture_ids.erase(std::unique(texture_ids.begin(), texture_ids.end()), texture_ids.end());
    if (texture_ids.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime GPU texture table exceeds the GPU index contract.";
        return result;
    }
'''
new = '''    std::vector<TextureSlot> texture_slots;
    std::vector<RuntimeGpuMaterialSampler> samplers;
    for (const auto* material : ordered_materials) {
        const std::array texture_resources{
            std::pair{texture_resource(material->asset.base_color_texture), RuntimeGpuMaterialTextureChannel::base_color},
            std::pair{texture_resource(material->asset.normal_texture), RuntimeGpuMaterialTextureChannel::normal},
            std::pair{texture_resource(material->asset.orm_texture), RuntimeGpuMaterialTextureChannel::orm},
            std::pair{texture_resource(material->asset.emissive_texture), RuntimeGpuMaterialTextureChannel::emissive},
        };
        for (const auto& [texture, channel] : texture_resources) {
            if (!texture.has_value()) continue;
            if (input.validate_texture_references &&
                !contains_resource(available_textures, *texture)) {
                result.diagnostics = "Runtime GPU material `" +
                    material->resource_id.value.to_string() +
                    "` references missing texture `" + texture->value.to_string() + "`.";
                return result;
            }
            texture_slots.push_back(TextureSlot{.resource = *texture, .channel = channel});
        }
        samplers.push_back(sampler_key(material->asset));
    }

    std::sort(texture_slots.begin(), texture_slots.end(), texture_slot_less);
    texture_slots.erase(std::unique(texture_slots.begin(), texture_slots.end()), texture_slots.end());
    if (texture_slots.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime GPU texture table exceeds the GPU index contract.";
        return result;
    }
'''
if old not in text:
    raise SystemExit('texture collection anchor not found')
text = text.replace(old, new, 1)

old = '''        record.base_color_texture = resource_index(texture_ids, base_color);
        record.normal_texture = resource_index(texture_ids, normal);
        record.orm_texture = resource_index(texture_ids, orm);
        record.emissive_texture = resource_index(texture_ids, emissive);
'''
new = '''        record.base_color_texture = texture_index(
            texture_slots, base_color, RuntimeGpuMaterialTextureChannel::base_color
        );
        record.normal_texture = texture_index(
            texture_slots, normal, RuntimeGpuMaterialTextureChannel::normal
        );
        record.orm_texture = texture_index(
            texture_slots, orm, RuntimeGpuMaterialTextureChannel::orm
        );
        record.emissive_texture = texture_index(
            texture_slots, emissive, RuntimeGpuMaterialTextureChannel::emissive
        );
'''
if old not in text:
    raise SystemExit('record texture-index anchor not found')
text = text.replace(old, new, 1)

old = '''    result.plan.texture_ids = std::move(texture_ids);
    result.plan.samplers = std::move(samplers);
'''
new = '''    result.plan.texture_ids.reserve(texture_slots.size());
    result.plan.texture_channels.reserve(texture_slots.size());
    for (const auto& slot : texture_slots) {
        result.plan.texture_ids.push_back(slot.resource);
        result.plan.texture_channels.push_back(slot.channel);
    }
    result.plan.samplers = std::move(samplers);
'''
if old not in text:
    raise SystemExit('plan output anchor not found')
text = text.replace(old, new, 1)

path.write_text(text)
