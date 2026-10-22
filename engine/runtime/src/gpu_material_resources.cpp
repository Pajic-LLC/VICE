#include "gpu_material_resources.hpp"
#include "material_pipeline.hpp"

#include <optional>
#include <utility>

namespace vice::runtime::detail {
namespace {

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] render::SamplerDesc sampler_desc(
    const RuntimeGpuMaterialSampler& source
) noexcept {
    render::SamplerDesc desc;
    desc.min_filter = source.min_filter;
    desc.mag_filter = source.mag_filter;
    desc.mip_filter = source.mip_filter;
    desc.address_u = source.address_u;
    desc.address_v = source.address_v;
    desc.address_w = source.address_w;
    desc.max_anisotropy = source.max_anisotropy;
    desc.debug_name = "VICE Runtime indexed material sampler";
    return desc;
}

[[nodiscard]] world::ResourceId texture_resource_id(
    const render::TextureStableId& stable_id
) noexcept {
    return world::ResourceId{world::PersistentId{stable_id}};
}

[[nodiscard]] render::Texture* find_shared_material_texture(
    const RuntimeMaterialLibrary& material_library,
    const world::ResourceId resource_id
) noexcept {
    for (const auto& record : material_library.catalog().records()) {
        const RuntimeMaterialBinding binding = material_library.find(record.resource_id);
        if (texture_resource_id(record.asset.base_color_texture) == resource_id) {
            return binding.base_color_texture;
        }
        if (texture_resource_id(record.asset.normal_texture) == resource_id) {
            return binding.normal_texture;
        }
        if (texture_resource_id(record.asset.orm_texture) == resource_id) {
            return binding.orm_texture;
        }
        if (texture_resource_id(record.asset.emissive_texture) == resource_id) {
            return binding.emissive_texture;
        }
    }
    return nullptr;
}

template <typename TextureResolver>
[[nodiscard]] bool build_resources(
    render::Device& device,
    const RuntimeGpuMaterialTablePlan& plan,
    TextureResolver&& resolve_texture,
    std::vector<render::Texture*>& textures,
    std::vector<std::unique_ptr<render::Sampler>>& owned_samplers,
    std::vector<render::Sampler*>& samplers,
    std::string* const diagnostics
) {
    std::vector<render::Texture*> next_textures;
    next_textures.reserve(plan.texture_ids.size());
    for (const world::ResourceId resource_id : plan.texture_ids) {
        render::Texture* const texture = resolve_texture(resource_id);
        if (texture == nullptr) {
            set_diagnostics(
                diagnostics,
                "Runtime GPU material resource table could not resolve texture `" +
                    resource_id.value.to_string() + "`."
            );
            return false;
        }
        next_textures.push_back(texture);
    }

    std::vector<std::unique_ptr<render::Sampler>> next_owned_samplers;
    std::vector<render::Sampler*> next_samplers;
    next_owned_samplers.reserve(plan.samplers.size());
    next_samplers.reserve(plan.samplers.size());
    for (const RuntimeGpuMaterialSampler& source : plan.samplers) {
        const render::SamplerDesc desc = sampler_desc(source);
        if (!render::is_valid(desc)) {
            set_diagnostics(
                diagnostics,
                "Runtime GPU material resource table rejected an invalid sampler descriptor."
            );
            return false;
        }
        auto sampler = device.create_sampler(desc);
        if (!sampler) {
            set_diagnostics(
                diagnostics,
                "Runtime GPU material resource table could not create an indexed material sampler."
            );
            return false;
        }
        next_samplers.push_back(sampler.get());
        next_owned_samplers.push_back(std::move(sampler));
    }

    textures = std::move(next_textures);
    owned_samplers = std::move(next_owned_samplers);
    samplers = std::move(next_samplers);
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace

void RuntimeGpuMaterialResourceTable::clear() noexcept {
    samplers_.clear();
    owned_samplers_.clear();
    textures_.clear();
}

bool RuntimeGpuMaterialResourceTable::initialize(
    render::Device& device,
    const RuntimeGpuMaterialTablePlan& plan,
    const RuntimeTextureLibrary& texture_library,
    std::string* const diagnostics
) {
    clear();
    return build_resources(
        device,
        plan,
        [&](const world::ResourceId resource_id) { return texture_library.find(resource_id); },
        textures_,
        owned_samplers_,
        samplers_,
        diagnostics
    );
}

bool RuntimeGpuMaterialResourceTable::initialize(
    render::Device& device,
    const RuntimeGpuMaterialTablePlan& plan,
    const RuntimeMaterialLibrary& material_library,
    std::string* const diagnostics
) {
    clear();
    return build_resources(
        device,
        plan,
        [&](const world::ResourceId resource_id) {
            return find_shared_material_texture(material_library, resource_id);
        },
        textures_,
        owned_samplers_,
        samplers_,
        diagnostics
    );
}

RuntimeGpuMaterialResourceTableView RuntimeGpuMaterialResourceTable::view() const noexcept {
    return RuntimeGpuMaterialResourceTableView{
        .textures = textures_,
        .samplers = samplers_,
    };
}

} // namespace vice::runtime::detail
