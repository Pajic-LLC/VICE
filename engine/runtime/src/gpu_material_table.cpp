#include <vice/runtime/gpu_material_table.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] std::string resource_key(const world::ResourceId resource_id) {
    return resource_id.value.to_string();
}

[[nodiscard]] bool resource_less(
    const world::ResourceId left,
    const world::ResourceId right
) {
    return resource_key(left) < resource_key(right);
}

[[nodiscard]] RuntimeGpuMaterialSampler sampler_key(
    const render::RenderMaterialAsset& material
) noexcept {
    return RuntimeGpuMaterialSampler{
        .min_filter = material.min_filter,
        .mag_filter = material.mag_filter,
        .mip_filter = material.mip_filter,
        .address_u = material.address_u,
        .address_v = material.address_v,
        .address_w = material.address_w,
        .max_anisotropy = material.max_anisotropy,
    };
}

[[nodiscard]] auto sampler_sort_key(const RuntimeGpuMaterialSampler& sampler) noexcept {
    return std::tuple{
        static_cast<std::uint8_t>(sampler.min_filter),
        static_cast<std::uint8_t>(sampler.mag_filter),
        static_cast<std::uint8_t>(sampler.mip_filter),
        static_cast<std::uint8_t>(sampler.address_u),
        static_cast<std::uint8_t>(sampler.address_v),
        static_cast<std::uint8_t>(sampler.address_w),
        sampler.max_anisotropy,
    };
}

[[nodiscard]] std::optional<world::ResourceId> texture_resource(
    const render::TextureStableId& stable_id
) noexcept {
    if (!render::valid_texture_id(stable_id)) return std::nullopt;
    const world::ResourceId resource{world::PersistentId{stable_id}};
    return resource.valid() ? std::optional{resource} : std::nullopt;
}

[[nodiscard]] bool contains_resource(
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

[[nodiscard]] std::uint32_t sampler_index(
    const std::vector<RuntimeGpuMaterialSampler>& samplers,
    const RuntimeGpuMaterialSampler sampler
) noexcept {
    const auto key = sampler_sort_key(sampler);
    const auto found = std::lower_bound(
        samplers.begin(), samplers.end(), key,
        [](const RuntimeGpuMaterialSampler& candidate, const auto& search_key) {
            return sampler_sort_key(candidate) < search_key;
        }
    );
    if (found == samplers.end() || *found != sampler) return runtime_gpu_resource_unassigned;
    return static_cast<std::uint32_t>(std::distance(samplers.begin(), found));
}

[[nodiscard]] std::uint32_t grown_capacity(const std::uint32_t required) noexcept {
    if (required <= 1U) return 1U;
    std::uint32_t capacity = 1U;
    while (capacity < required && capacity <= std::numeric_limits<std::uint32_t>::max() / 2U) {
        capacity *= 2U;
    }
    return std::max(capacity, required);
}

} // namespace

std::optional<std::uint32_t> RuntimeGpuMaterialTablePlan::material_index(
    const world::ResourceId resource_id
) const noexcept {
    const auto found = std::lower_bound(
        material_ids.begin(), material_ids.end(), resource_id, resource_less
    );
    if (found == material_ids.end() || *found != resource_id) return std::nullopt;
    return static_cast<std::uint32_t>(std::distance(material_ids.begin(), found));
}

RuntimeGpuMaterialTableBuildResult build_runtime_gpu_material_table_plan(
    const RuntimeGpuMaterialTableBuildInput& input
) {
    RuntimeGpuMaterialTableBuildResult result;
    if (input.materials.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime GPU material count exceeds the GPU index contract.";
        return result;
    }

    std::vector<const CookedRenderMaterialRecord*> ordered_materials;
    ordered_materials.reserve(input.materials.size());
    for (const auto& material : input.materials) {
        if (!material.resource_id.valid()) {
            result.diagnostics = "Runtime GPU material table rejected a material without a stable ID.";
            return result;
        }
        ordered_materials.push_back(&material);
    }
    std::sort(
        ordered_materials.begin(), ordered_materials.end(),
        [](const auto* left, const auto* right) {
            return resource_less(left->resource_id, right->resource_id);
        }
    );
    for (std::size_t index = 1U; index < ordered_materials.size(); ++index) {
        if (ordered_materials[index - 1U]->resource_id == ordered_materials[index]->resource_id) {
            result.diagnostics = "Runtime GPU material table rejected duplicate stable material IDs.";
            return result;
        }
    }

    std::vector<world::ResourceId> available_textures;
    available_textures.reserve(input.textures.size());
    for (const auto& texture : input.textures) {
        if (texture.resource_id.valid()) available_textures.push_back(texture.resource_id);
    }
    std::sort(available_textures.begin(), available_textures.end(), resource_less);
    available_textures.erase(
        std::unique(available_textures.begin(), available_textures.end()),
        available_textures.end()
    );

    std::vector<TextureSlot> texture_slots;
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

    std::sort(
        samplers.begin(), samplers.end(),
        [](const auto& left, const auto& right) {
            return sampler_sort_key(left) < sampler_sort_key(right);
        }
    );
    samplers.erase(std::unique(samplers.begin(), samplers.end()), samplers.end());
    if (samplers.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime GPU sampler table exceeds the GPU index contract.";
        return result;
    }

    result.plan.records.reserve(ordered_materials.size());
    result.plan.material_ids.reserve(ordered_materials.size());
    for (const auto* material : ordered_materials) {
        const auto base_color = texture_resource(material->asset.base_color_texture);
        const auto normal = texture_resource(material->asset.normal_texture);
        const auto orm = texture_resource(material->asset.orm_texture);
        const auto emissive = texture_resource(material->asset.emissive_texture);
        const RuntimeGpuMaterialSampler sampler = sampler_key(material->asset);

        RuntimeGpuMaterialRecord record;
        record.base_color[0] = material->asset.base_color.red;
        record.base_color[1] = material->asset.base_color.green;
        record.base_color[2] = material->asset.base_color.blue;
        record.base_color[3] = material->asset.base_color.alpha;
        record.emissive_color[0] = material->asset.emissive_color.red;
        record.emissive_color[1] = material->asset.emissive_color.green;
        record.emissive_color[2] = material->asset.emissive_color.blue;
        record.emissive_color[3] = material->asset.emissive_color.alpha;
        record.metallic = material->asset.metallic;
        record.roughness = material->asset.roughness;
        record.normal_scale = material->asset.normal_scale;
        record.emissive_intensity = material->asset.emissive_intensity;
        record.base_color_texture = texture_index(
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
        record.sampler = sampler_index(samplers, sampler);
        if (record.sampler == runtime_gpu_resource_unassigned) {
            result.diagnostics = "Runtime GPU material table could not resolve a sampler index.";
            return result;
        }
        result.plan.material_ids.push_back(material->resource_id);
        result.plan.records.push_back(record);
    }
    result.plan.texture_ids.reserve(texture_slots.size());
    result.plan.texture_channels.reserve(texture_slots.size());
    for (const auto& slot : texture_slots) {
        result.plan.texture_ids.push_back(slot.resource);
        result.plan.texture_channels.push_back(slot.channel);
    }
    result.plan.samplers = std::move(samplers);
    return result;
}

struct RuntimeGpuMaterialTable::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::Buffer> upload;
    std::unique_ptr<render::Buffer> records;
    std::uint32_t material_count = 0U;
    std::uint32_t capacity = 0U;

    [[nodiscard]] bool ensure_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (device == nullptr || required == 0U) return false;
        if (required <= capacity && upload && records) return true;

        const std::uint32_t next_capacity = grown_capacity(required);
        const std::uint64_t size =
            static_cast<std::uint64_t>(next_capacity) * sizeof(RuntimeGpuMaterialRecord);

        render::BufferDesc upload_desc;
        upload_desc.size = size;
        upload_desc.usage = render::BufferUsage::copy_source;
        upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
        upload_desc.debug_name = "VICE Runtime GPU material record upload";
        auto next_upload = device->create_buffer(upload_desc);

        render::BufferDesc record_desc;
        record_desc.size = size;
        record_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination |
            render::BufferUsage::copy_source;
        record_desc.memory = render::MemoryLocation::gpu_only;
        record_desc.debug_name = "VICE Runtime GPU material record table";
        auto next_records = device->create_buffer(record_desc);

        if (!next_upload || !next_records) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate GPU material record buffers.");
            return false;
        }

        upload = std::move(next_upload);
        records = std::move(next_records);
        capacity = next_capacity;
        return true;
    }
};

RuntimeGpuMaterialTable::RuntimeGpuMaterialTable() : state_{std::make_unique<State>()} {}
RuntimeGpuMaterialTable::~RuntimeGpuMaterialTable() = default;
RuntimeGpuMaterialTable::RuntimeGpuMaterialTable(RuntimeGpuMaterialTable&&) noexcept = default;
RuntimeGpuMaterialTable& RuntimeGpuMaterialTable::operator=(RuntimeGpuMaterialTable&&) noexcept = default;

bool RuntimeGpuMaterialTable::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU material table state is unavailable.");
        return false;
    }
    state_->device = &device;
    state_->upload.reset();
    state_->records.reset();
    state_->material_count = 0U;
    state_->capacity = 0U;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuMaterialTable::upload(
    render::CommandList& commands,
    const std::span<const RuntimeGpuMaterialRecord> records,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU material table is not initialized.");
        return false;
    }
    if (records.empty()) {
        state_->material_count = 0U;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (records.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(diagnostics, "Runtime GPU material table exceeds the GPU material contract.");
        return false;
    }

    const std::uint32_t material_count = static_cast<std::uint32_t>(records.size());
    if (!state_->ensure_capacity(material_count, diagnostics)) return false;
    const auto bytes = std::as_bytes(records);
    if (!state_->upload->write(0U, bytes) ||
        !commands.copy_buffer(*state_->upload, 0U, *state_->records, 0U, bytes.size())) {
        set_diagnostics(diagnostics, "VICE Runtime could not upload GPU material records.");
        return false;
    }

    state_->material_count = material_count;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeGpuMaterialTableResult RuntimeGpuMaterialTable::result() const noexcept {
    if (state_ == nullptr) return {};
    return RuntimeGpuMaterialTableResult{
        .records = state_->records.get(),
        .material_count = state_->material_count,
        .capacity = state_->capacity,
    };
}

} // namespace vice::runtime
