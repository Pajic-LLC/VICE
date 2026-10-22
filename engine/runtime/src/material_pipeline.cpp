#include "material_pipeline.hpp"
#include "texture_pipeline.hpp"

#include <vice/render/material_asset.hpp>
#include <vice/render/shader_asset.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vice::runtime {
namespace {

struct MaterialConstants {
    float base_color[4]{};
    float metallic = 0.0F;
    float roughness = 1.0F;
    float normal_scale = 1.0F;
    float emissive_intensity = 0.0F;
    float emissive_color[4]{};
};

static_assert(sizeof(MaterialConstants) == 48U);

[[nodiscard]] bool valid_v1_resources(
    const render::ShaderModule& module,
    const std::uint32_t required_binding
) noexcept {
    bool required_found = false;
    for (const auto& resource : module.resources()) {
        if (resource.register_class != render::ShaderRegisterClass::constant_buffer ||
            resource.space != 0U || (resource.binding != 0U && resource.binding != 1U) ||
            resource.count != 1U || resource.unbounded) {
            return false;
        }
        if (resource.binding == required_binding) required_found = true;
    }
    return required_found;
}

[[nodiscard]] bool valid_v2_vertex_resources(const render::ShaderModule& module) noexcept {
    return valid_v1_resources(module, 0U);
}

struct FragmentResourceContract {
    bool valid = false;
    std::uint32_t directional_shadow_cascade_count = 0;
    std::uint32_t shadow_texture_binding_base = 1U;
    std::uint32_t point_shadow_light_capacity = 0U;
    std::uint32_t point_shadow_texture_binding_base = 5U;
    std::uint32_t environment_source_count = 0U;
};

[[nodiscard]] FragmentResourceContract v2_fragment_resource_contract(
    const render::ShaderModule& module
) noexcept {
    bool material_constants = false;
    bool base_color_texture = false;
    bool base_color_sampler = false;
    std::array<bool, runtime_shadow_cascade_count> shadow_textures{};
    std::array<bool, runtime_point_shadow_light_count * runtime_point_shadow_face_count> point_shadow_textures{};
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
            } else if (resource.binding >= 5U && resource.binding <
                5U + runtime_point_shadow_light_count * runtime_point_shadow_face_count) {
                point_shadow_textures[resource.binding - 5U] = true;
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
    const bool legacy_shadow_set = shadow_textures[0] &&
        std::none_of(
            shadow_textures.begin() + 1,
            shadow_textures.end(),
            [](const bool value) { return value; }
        );
    const bool cascaded_shadow_set = all_shadow_textures;
    const bool directional_shadow_set_valid = !any_shadow_texture || legacy_shadow_set || cascaded_shadow_set;
    const bool any_point_shadow_texture = std::any_of(
        point_shadow_textures.begin(), point_shadow_textures.end(), [](const bool value) { return value; }
    );
    const bool all_point_shadow_textures = std::all_of(
        point_shadow_textures.begin(), point_shadow_textures.end(), [](const bool value) { return value; }
    );
    const bool point_shadow_set_valid = !any_point_shadow_texture || all_point_shadow_textures;
    const bool any_shadow_resource = any_shadow_texture || any_point_shadow_texture;
    const bool sampler_valid = shadow_sampler == any_shadow_resource;

    return FragmentResourceContract{
        .valid = material_constants && base_color_texture && base_color_sampler &&
            directional_shadow_set_valid && point_shadow_set_valid && sampler_valid,
        .directional_shadow_cascade_count = cascaded_shadow_set
            ? static_cast<std::uint32_t>(runtime_shadow_cascade_count)
            : legacy_shadow_set ? 1U : 0U,
        .shadow_texture_binding_base = 1U,
        .point_shadow_light_capacity = all_point_shadow_textures
            ? static_cast<std::uint32_t>(runtime_point_shadow_light_count) : 0U,
        .point_shadow_texture_binding_base = 5U,
    };
}

[[nodiscard]] FragmentResourceContract v3_fragment_resource_contract(
    const render::ShaderModule& module
) noexcept {
    bool material_constants = false;
    std::array<bool, 4U> material_textures{};
    bool material_sampler = false;
    std::array<bool, runtime_shadow_cascade_count> shadow_textures{};
    bool shadow_sampler = false;
    std::array<bool, 9U> environment_textures{};
    bool environment_sampler = false;
    for (const auto& resource : module.resources()) {
        if (resource.space != 0U || resource.count != 1U || resource.unbounded) return {};
        switch (resource.register_class) {
        case render::ShaderRegisterClass::constant_buffer:
            if (resource.binding != 0U && resource.binding != 1U) return {};
            if (resource.binding == 1U) material_constants = true;
            break;
        case render::ShaderRegisterClass::shader_resource:
            if (resource.binding <= 3U) {
                material_textures[resource.binding] = true;
            } else if (resource.binding >= 4U && resource.binding < 4U + runtime_shadow_cascade_count) {
                shadow_textures[resource.binding - 4U] = true;
            } else if (resource.binding >= 8U && resource.binding <= 16U) {
                environment_textures[resource.binding - 8U] = true;
            } else {
                return {};
            }
            break;
        case render::ShaderRegisterClass::sampler:
            if (resource.binding == 0U) material_sampler = true;
            else if (resource.binding == 1U) shadow_sampler = true;
            else if (resource.binding == 2U) environment_sampler = true;
            else return {};
            break;
        case render::ShaderRegisterClass::unordered_access:
            return {};
        }
    }

    const bool all_material_textures = std::all_of(
        material_textures.begin(), material_textures.end(), [](const bool value) { return value; }
    );
    const bool any_shadow_texture = std::any_of(
        shadow_textures.begin(), shadow_textures.end(), [](const bool value) { return value; }
    );
    const bool all_shadow_textures = std::all_of(
        shadow_textures.begin(), shadow_textures.end(), [](const bool value) { return value; }
    );
    const bool legacy_shadow_set = shadow_textures[0] && shadow_sampler &&
        std::none_of(
            shadow_textures.begin() + 1, shadow_textures.end(), [](const bool value) { return value; }
        );
    const bool cascaded_shadow_set = all_shadow_textures && shadow_sampler;
    const bool no_shadow_set = !any_shadow_texture && !shadow_sampler;
    const bool shadow_set_valid = no_shadow_set || legacy_shadow_set || cascaded_shadow_set;

    const bool any_environment_texture = std::any_of(
        environment_textures.begin(), environment_textures.end(), [](const bool value) { return value; }
    );
    const bool primary_environment = std::all_of(
        environment_textures.begin(), environment_textures.begin() + 3,
        [](const bool value) { return value; }
    );
    const bool additional_environment = std::all_of(
        environment_textures.begin() + 3, environment_textures.end(),
        [](const bool value) { return value; }
    );
    const bool no_additional_environment = std::none_of(
        environment_textures.begin() + 3, environment_textures.end(),
        [](const bool value) { return value; }
    );
    const bool no_environment_set = !any_environment_texture && !environment_sampler;
    const bool legacy_environment_set = primary_environment && no_additional_environment && environment_sampler;
    const bool probe_environment_set = primary_environment && additional_environment && environment_sampler;

    return FragmentResourceContract{
        .valid = material_constants && all_material_textures && material_sampler && shadow_set_valid &&
            (no_environment_set || legacy_environment_set || probe_environment_set),
        .directional_shadow_cascade_count = cascaded_shadow_set
            ? static_cast<std::uint32_t>(runtime_shadow_cascade_count)
            : legacy_shadow_set ? 1U : 0U,
        .shadow_texture_binding_base = 4U,
        .environment_source_count = probe_environment_set ? 3U : legacy_environment_set ? 1U : 0U,
    };
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_material_constants(
    render::Device& device,
    const render::RenderMaterialAsset& material
) {
    render::BufferDesc desc;
    desc.size = sizeof(MaterialConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime material constants";
    auto buffer = device.create_buffer(desc);
    if (!buffer) return nullptr;

    const MaterialConstants constants{
        .base_color = {
            material.base_color.red,
            material.base_color.green,
            material.base_color.blue,
            material.base_color.alpha,
        },
        .metallic = material.metallic,
        .roughness = material.roughness,
        .normal_scale = material.normal_scale,
        .emissive_intensity = material.emissive_intensity,
        .emissive_color = {
            material.emissive_color.red,
            material.emissive_color.green,
            material.emissive_color.blue,
            material.emissive_color.alpha,
        },
    };
    if (!buffer->write(0U, std::as_bytes(std::span{&constants, 1}))) return nullptr;
    return buffer;
}

[[nodiscard]] std::unique_ptr<render::Sampler> create_material_sampler(
    render::Device& device,
    const render::RenderMaterialAsset& material
) {
    render::SamplerDesc desc;
    desc.min_filter = material.min_filter;
    desc.mag_filter = material.mag_filter;
    desc.mip_filter = material.mip_filter;
    desc.address_u = material.address_u;
    desc.address_v = material.address_v;
    desc.address_w = material.address_w;
    desc.max_anisotropy = material.max_anisotropy;
    desc.debug_name = "VICE Runtime project material sampler";
    return device.create_sampler(desc);
}

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

} // namespace

bool CookedRenderMaterialCatalog::load(const std::filesystem::path& content_root) {
    records_.clear();
    diagnostics_.clear();

    std::error_code error;
    if (content_root.empty() || !std::filesystem::is_directory(content_root, error)) {
        diagnostics_ = error
            ? "Could not inspect runtime content root: " + error.message()
            : "Runtime content root is not available.";
        return false;
    }

    std::unordered_set<world::ResourceId, world::ResourceIdHash> seen;
    std::filesystem::recursive_directory_iterator iterator{
        content_root,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        diagnostics_ = "Could not enumerate runtime render materials: " + error.message();
        return false;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            diagnostics_ = "Could not enumerate runtime render materials: " + error.message();
            return false;
        }
        if (!iterator->is_regular_file() || iterator->path().extension() != ".vmatc") continue;

        auto loaded = render::load_render_material_asset(iterator->path());
        if (!loaded.asset.has_value()) {
            diagnostics_ = "Could not load cooked runtime render material `" +
                iterator->path().generic_string() + "`: " + loaded.diagnostics;
            return false;
        }
        const world::ResourceId resource{world::PersistentId{loaded.asset->stable_id}};
        if (!resource.valid()) {
            diagnostics_ = "Cooked runtime render material has no stable ID: " +
                iterator->path().generic_string();
            return false;
        }
        if (!seen.insert(resource).second) {
            diagnostics_ = "Duplicate cooked runtime render material stable ID `" +
                resource.value.to_string() + "`.";
            return false;
        }
        records_.push_back(CookedRenderMaterialRecord{
            .resource_id = resource,
            .path = iterator->path(),
            .asset = std::move(*loaded.asset),
        });
    }
    return true;
}

const CookedRenderMaterialRecord* CookedRenderMaterialCatalog::find(
    const world::ResourceId resource_id
) const noexcept {
    const auto found = std::find_if(records_.begin(), records_.end(), [resource_id](const auto& record) {
        return record.resource_id == resource_id;
    });
    return found == records_.end() ? nullptr : &*found;
}

std::span<const CookedRenderMaterialRecord> CookedRenderMaterialCatalog::records() const noexcept {
    return records_;
}

const std::string& CookedRenderMaterialCatalog::diagnostics() const noexcept {
    return diagnostics_;
}

namespace detail {

struct RuntimeMaterialLibrary::State {
    struct MaterialGpu {
        std::unique_ptr<render::ShaderModule> vertex_shader;
        std::unique_ptr<render::ShaderModule> fragment_shader;
        std::unique_ptr<render::GraphicsPipeline> pipeline;
        std::unique_ptr<render::Buffer> constants;
        render::Texture* base_color_texture = nullptr;
        render::Texture* normal_texture = nullptr;
        render::Texture* orm_texture = nullptr;
        render::Texture* emissive_texture = nullptr;
        std::unique_ptr<render::Sampler> sampler;
        std::uint32_t directional_shadow_cascade_count = 0;
        std::uint32_t shadow_texture_binding_base = 1U;
        std::uint32_t point_shadow_light_capacity = 0U;
        std::uint32_t point_shadow_texture_binding_base = 5U;
        std::uint32_t environment_source_count = 0U;
    };

    CookedRenderMaterialCatalog catalog;
    RuntimeTextureLibrary textures;
    std::unordered_map<world::ResourceId, MaterialGpu, world::ResourceIdHash> materials;
};

RuntimeMaterialLibrary::RuntimeMaterialLibrary() : state_{std::make_unique<State>()} {}
RuntimeMaterialLibrary::~RuntimeMaterialLibrary() = default;
RuntimeMaterialLibrary::RuntimeMaterialLibrary(RuntimeMaterialLibrary&&) noexcept = default;
RuntimeMaterialLibrary& RuntimeMaterialLibrary::operator=(RuntimeMaterialLibrary&&) noexcept = default;

bool RuntimeMaterialLibrary::initialize(
    render::Device& device,
    const render::TextureFormat color_format,
    const std::span<const render::VertexBufferLayoutDesc> vertex_layouts,
    const std::filesystem::path& content_root,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime material library state is unavailable.");
        return false;
    }
    state_->materials.clear();
    if (!state_->catalog.load(content_root)) {
        set_diagnostics(diagnostics, state_->catalog.diagnostics());
        return false;
    }
    if (!state_->textures.initialize(device, content_root, diagnostics)) return false;

    for (const auto& record : state_->catalog.records()) {
        const bool textured = record.asset.version >= render::render_material_asset_format_version_v2;
        const bool extended_pbr = record.asset.version >= render::render_material_asset_format_version_v3;
        const bool gpu_scene_pbr = render::render_material_uses_gpu_scene_pbr(record.asset.version);

        if (gpu_scene_pbr) {
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

        const auto vertex_manifest = (
            record.path.parent_path() / record.asset.vertex_shader_manifest
        ).lexically_normal();
        const auto fragment_manifest = (
            record.path.parent_path() / record.asset.fragment_shader_manifest
        ).lexically_normal();
        auto vertex = render::load_cooked_shader(device, vertex_manifest);
        if (!vertex) {
            set_diagnostics(diagnostics, "Could not load vertex shader for render material `" +
                record.resource_id.value.to_string() + "`: " + vertex.diagnostics);
            return false;
        }
        auto fragment = render::load_cooked_shader(device, fragment_manifest);
        if (!fragment) {
            set_diagnostics(diagnostics, "Could not load fragment shader for render material `" +
                record.resource_id.value.to_string() + "`: " + fragment.diagnostics);
            return false;
        }

        std::uint32_t directional_shadow_cascade_count = 0;
        std::uint32_t shadow_texture_binding_base = 1U;
        std::uint32_t point_shadow_light_capacity = 0U;
        std::uint32_t point_shadow_texture_binding_base = 5U;
        std::uint32_t environment_source_count = 0U;
        bool resources_valid = false;
        if (extended_pbr) {
            const auto fragment_contract = v3_fragment_resource_contract(*fragment.module);
            resources_valid = valid_v2_vertex_resources(*vertex.module) && fragment_contract.valid;
            directional_shadow_cascade_count = fragment_contract.directional_shadow_cascade_count;
            shadow_texture_binding_base = fragment_contract.shadow_texture_binding_base;
            environment_source_count = fragment_contract.environment_source_count;
        } else if (textured) {
            const auto fragment_contract = v2_fragment_resource_contract(*fragment.module);
            resources_valid = valid_v2_vertex_resources(*vertex.module) && fragment_contract.valid;
            directional_shadow_cascade_count = fragment_contract.directional_shadow_cascade_count;
            shadow_texture_binding_base = fragment_contract.shadow_texture_binding_base;
            point_shadow_light_capacity = fragment_contract.point_shadow_light_capacity;
            point_shadow_texture_binding_base = fragment_contract.point_shadow_texture_binding_base;
        } else {
            resources_valid = valid_v1_resources(*vertex.module, 0U) &&
                valid_v1_resources(*fragment.module, 1U);
        }
        if (vertex.module->stage() != render::ShaderStage::vertex ||
            fragment.module->stage() != render::ShaderStage::fragment || !resources_valid) {
            set_diagnostics(diagnostics, "Render material `" + record.resource_id.value.to_string() +
                "` does not satisfy its VICE shader binding contract.");
            return false;
        }

        render::Texture* base_color_texture = nullptr;
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
            if (!sampler) {
                set_diagnostics(diagnostics, "Could not create sampler for render material `" +
                    record.resource_id.value.to_string() + "`.");
                return false;
            }
        }

        render::GraphicsPipelineDesc pipeline_desc;
        pipeline_desc.vertex_shader = vertex.module.get();
        pipeline_desc.fragment_shader = fragment.module.get();
        pipeline_desc.color_format = color_format;
        pipeline_desc.cull_mode = render::CullMode::none;
        pipeline_desc.depth.format = render::TextureFormat::depth32_float;
        pipeline_desc.depth.test_enabled = true;
        pipeline_desc.depth.write_enabled = true;
        pipeline_desc.depth.compare = render::CompareOp::less;
        pipeline_desc.vertex_buffers = vertex_layouts;
        pipeline_desc.debug_name = "VICE Runtime project material pipeline";
        auto pipeline = device.create_graphics_pipeline(pipeline_desc);
        if (!pipeline) {
            set_diagnostics(diagnostics, "Renderer rejected pipeline for render material `" +
                record.resource_id.value.to_string() + "`.");
            return false;
        }
        auto constants = create_material_constants(device, record.asset);
        if (!constants) {
            set_diagnostics(diagnostics, "Could not create constants for render material `" +
                record.resource_id.value.to_string() + "`.");
            return false;
        }

        state_->materials.emplace(record.resource_id, State::MaterialGpu{
            .vertex_shader = std::move(vertex.module),
            .fragment_shader = std::move(fragment.module),
            .pipeline = std::move(pipeline),
            .constants = std::move(constants),
            .base_color_texture = base_color_texture,
            .normal_texture = normal_texture,
            .orm_texture = orm_texture,
            .emissive_texture = emissive_texture,
            .sampler = std::move(sampler),
            .directional_shadow_cascade_count = directional_shadow_cascade_count,
            .shadow_texture_binding_base = shadow_texture_binding_base,
            .point_shadow_light_capacity = point_shadow_light_capacity,
            .point_shadow_texture_binding_base = point_shadow_texture_binding_base,
            .environment_source_count = environment_source_count,
        });
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeMaterialBinding RuntimeMaterialLibrary::find(
    const world::ResourceId resource_id
) const noexcept {
    if (state_ == nullptr) return {};
    const auto found = state_->materials.find(resource_id);
    if (found == state_->materials.end()) return {};
    return RuntimeMaterialBinding{
        .pipeline = found->second.pipeline.get(),
        .constants = found->second.constants.get(),
        .base_color_texture = found->second.base_color_texture,
        .normal_texture = found->second.normal_texture,
        .orm_texture = found->second.orm_texture,
        .emissive_texture = found->second.emissive_texture,
        .sampler = found->second.sampler.get(),
        .constants_size = found->second.constants != nullptr ? sizeof(MaterialConstants) : 0U,
        .directional_shadow_cascade_count = found->second.directional_shadow_cascade_count,
        .shadow_texture_binding_base = found->second.shadow_texture_binding_base,
        .point_shadow_light_capacity = found->second.point_shadow_light_capacity,
        .point_shadow_texture_binding_base = found->second.point_shadow_texture_binding_base,
        .environment_source_count = found->second.environment_source_count,
    };
}

const CookedRenderMaterialCatalog& RuntimeMaterialLibrary::catalog() const noexcept {
    return state_->catalog;
}

} // namespace detail
} // namespace vice::runtime
