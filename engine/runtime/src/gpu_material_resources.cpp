#include "gpu_material_resources.hpp"
#include "material_pipeline.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace vice::runtime::detail {
namespace {

constexpr std::uint64_t virtual_texture_sampling_budget = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t virtual_texture_bootstrap_extent = 512U;
constexpr std::uint32_t virtual_table_width = 1024U;
constexpr std::uint32_t virtual_table_max_height = 16'384U;

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

[[nodiscard]] RuntimeMaterialTextureChannel material_channel(
    const RuntimeGpuMaterialTextureChannel channel
) noexcept {
    switch (channel) {
    case RuntimeGpuMaterialTextureChannel::base_color:
        return RuntimeMaterialTextureChannel::base_color;
    case RuntimeGpuMaterialTextureChannel::normal:
        return RuntimeMaterialTextureChannel::normal;
    case RuntimeGpuMaterialTextureChannel::orm:
        return RuntimeMaterialTextureChannel::orm;
    case RuntimeGpuMaterialTextureChannel::emissive:
        return RuntimeMaterialTextureChannel::emissive;
    }
    return RuntimeMaterialTextureChannel::base_color;
}

[[nodiscard]] bool resource_less(
    const world::ResourceId left,
    const world::ResourceId right
) noexcept {
    const auto left_bytes = left.value.bytes();
    const auto right_bytes = right.value.bytes();
    return std::lexicographical_compare(
        left_bytes.begin(), left_bytes.end(), right_bytes.begin(), right_bytes.end()
    );
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
    if (!plan.texture_channels.empty() && plan.texture_channels.size() != plan.texture_ids.size()) {
        set_diagnostics(diagnostics, "Runtime GPU material texture semantic table is misaligned.");
        return false;
    }

    std::vector<render::Texture*> next_textures;
    next_textures.reserve(plan.texture_ids.size());
    for (std::size_t index = 0U; index < plan.texture_ids.size(); ++index) {
        const world::ResourceId resource_id = plan.texture_ids[index];
        const RuntimeGpuMaterialTextureChannel channel = plan.texture_channels.empty()
            ? RuntimeGpuMaterialTextureChannel::base_color
            : plan.texture_channels[index];
        render::Texture* const texture = resolve_texture(resource_id, channel);
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

[[nodiscard]] bool virtual_tagged(const std::uint32_t index) noexcept {
    return index != runtime_gpu_resource_unassigned &&
        (index & runtime_gpu_virtual_texture_flag) != 0U;
}

[[nodiscard]] bool record_uses_virtual_texture(const RuntimeGpuMaterialRecord& record) noexcept {
    return virtual_tagged(record.base_color_texture) ||
        virtual_tagged(record.normal_texture) ||
        virtual_tagged(record.orm_texture) ||
        virtual_tagged(record.emissive_texture);
}

[[nodiscard]] std::vector<world::ResourceId> virtual_candidate_textures(
    const RuntimeGpuMaterialTablePlan& plan,
    const CookedTextureCatalog& catalog
) {
    std::vector<bool> used_slots(plan.texture_ids.size(), false);
    const auto mark = [&](const std::uint32_t index) {
        if (index != runtime_gpu_resource_unassigned && index < used_slots.size()) used_slots[index] = true;
    };
    for (const auto& record : plan.records) {
        if (record.sampler == runtime_gpu_resource_unassigned || record.sampler >= plan.samplers.size()) continue;
        const auto& sampler = plan.samplers[record.sampler];
        if (sampler.address_u != render::AddressMode::repeat ||
            sampler.address_v != render::AddressMode::repeat) {
            continue;
        }
        mark(record.base_color_texture);
        mark(record.normal_texture);
        mark(record.orm_texture);
        mark(record.emissive_texture);
    }

    std::vector<world::ResourceId> result;
    for (std::size_t slot = 0U; slot < used_slots.size(); ++slot) {
        if (!used_slots[slot]) continue;
        const auto texture = plan.texture_ids[slot];
        const auto* const record = catalog.find(texture);
        if (record == nullptr || record->asset.version != render::texture_asset_format_version_v2 ||
            record->path.empty() || record->asset.mip_levels.empty() || record->asset.virtual_tiles.empty()) {
            continue;
        }
        auto archive_path = record->path;
        archive_path.replace_extension(".vtilec");
        std::error_code error;
        if (!std::filesystem::is_regular_file(archive_path, error) || error) continue;
        result.push_back(texture);
    }
    std::sort(result.begin(), result.end(), resource_less);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

[[nodiscard]] std::vector<RuntimeVirtualTextureTileKey> bootstrap_requests(
    const CookedTextureCatalog& catalog,
    const std::span<const world::ResourceId> textures
) {
    std::vector<RuntimeVirtualTextureTileKey> result;
    for (const auto texture : textures) {
        const auto* const record = catalog.find(texture);
        if (record == nullptr || record->asset.mip_levels.empty()) continue;
        std::size_t first_mip = 0U;
        while (first_mip + 1U < record->asset.mip_levels.size() &&
            (record->asset.mip_levels[first_mip].width > virtual_texture_bootstrap_extent ||
             record->asset.mip_levels[first_mip].height > virtual_texture_bootstrap_extent)) {
            ++first_mip;
        }
        for (std::size_t mip = first_mip; mip < record->asset.mip_levels.size(); ++mip) {
            const auto& mip_desc = record->asset.mip_levels[mip];
            const std::uint64_t end = static_cast<std::uint64_t>(mip_desc.first_tile) + mip_desc.tile_count;
            if (end > record->asset.virtual_tiles.size()) return {};
            for (std::uint64_t tile_index = mip_desc.first_tile; tile_index < end; ++tile_index) {
                const auto& tile = record->asset.virtual_tiles[static_cast<std::size_t>(tile_index)];
                result.push_back(RuntimeVirtualTextureTileKey{
                    .texture = texture,
                    .mip_level = tile.mip_level,
                    .tile_x = tile.tile_x,
                    .tile_y = tile.tile_y,
                });
            }
        }
    }
    return result;
}

[[nodiscard]] std::unique_ptr<render::Texture> create_uint_table_texture(
    render::Device& device,
    const std::span<const std::uint32_t> words,
    const std::string_view debug_name
) {
    if (words.empty()) return nullptr;
    const std::uint64_t height64 =
        (static_cast<std::uint64_t>(words.size()) + virtual_table_width - 1U) /
        virtual_table_width;
    if (height64 == 0U || height64 > virtual_table_max_height) return nullptr;
    const auto height = static_cast<std::uint32_t>(height64);
    const std::uint64_t texel_count = static_cast<std::uint64_t>(virtual_table_width) * height;
    if (texel_count > std::numeric_limits<std::size_t>::max() / 4U) return nullptr;
    std::vector<std::byte> bytes(static_cast<std::size_t>(texel_count * 4U), std::byte{0U});
    for (std::size_t index = 0U; index < words.size(); ++index) {
        const std::uint32_t value = words[index];
        const std::size_t offset = index * 4U;
        bytes[offset + 0U] = static_cast<std::byte>(value & 0xFFU);
        bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
        bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
        bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    }

    render::TextureDesc desc;
    desc.dimension = render::TextureDimension::d2;
    desc.width = virtual_table_width;
    desc.height = height;
    desc.depth = 1U;
    desc.mip_levels = 1U;
    desc.array_layers = 1U;
    desc.sample_count = 1U;
    desc.format = render::TextureFormat::rgba8_unorm;
    desc.usage = render::TextureUsage::sampled | render::TextureUsage::copy_destination;
    desc.debug_name = debug_name;
    auto texture = device.create_texture(desc);
    if (!texture || !texture->write_2d(bytes, static_cast<std::uint64_t>(virtual_table_width) * 4U, 0U)) {
        return nullptr;
    }
    return texture;
}

void build_virtual_resources(
    render::Device& device,
    const RuntimeGpuMaterialTablePlan& conventional_plan,
    const RuntimeMaterialLibrary& material_library,
    std::vector<RuntimeGpuMaterialRecord>& gpu_records,
    std::vector<render::Texture*>& material_textures,
    std::unique_ptr<RuntimeVirtualTextureResidentPool>& resident_pool,
    RuntimeVirtualTextureSamplingPlan& sampling_plan,
    std::unique_ptr<render::Texture>& descriptor_table,
    std::unique_ptr<render::Texture>& page_table
) {
    const auto candidates = virtual_candidate_textures(
        conventional_plan, material_library.texture_catalog()
    );
    if (candidates.empty()) return;
    const auto requests = bootstrap_requests(material_library.texture_catalog(), candidates);
    if (requests.empty()) return;

    auto next_pool = std::make_unique<RuntimeVirtualTextureResidentPool>(virtual_texture_sampling_budget);
    const auto update = next_pool->update(device, material_library.texture_catalog(), requests);
    if (!update) return;
    auto next_sampling = build_runtime_virtual_texture_sampling_plan(
        material_library.texture_catalog(), *next_pool, candidates
    );
    if (!next_sampling) return;

    RuntimeGpuMaterialTablePlan tagged_plan = conventional_plan;
    if (!apply_runtime_virtual_texture_sampling(tagged_plan, next_sampling, nullptr)) return;

    const std::uint64_t conventional_count = material_textures.size();
    const std::uint64_t physical_count = next_sampling.physical_textures.size();
    if (conventional_count + physical_count + 2U > std::numeric_limits<std::uint32_t>::max()) return;
    const auto descriptor_slot = static_cast<std::uint32_t>(conventional_count + physical_count);
    const auto page_slot = descriptor_slot + 1U;

    std::vector<std::uint32_t> descriptor_words;
    descriptor_words.reserve(next_sampling.textures.size() * 4U);
    for (const auto& descriptor : next_sampling.textures) {
        descriptor_words.push_back(descriptor.width);
        descriptor_words.push_back(descriptor.height);
        descriptor_words.push_back(descriptor.mip_count);
        descriptor_words.push_back(descriptor.first_page);
    }
    std::vector<std::uint32_t> page_words;
    page_words.reserve(next_sampling.pages.size() * 4U);
    for (const auto& page : next_sampling.pages) {
        if (page.physical_slot == runtime_virtual_texture_invalid_index ||
            conventional_count + page.physical_slot > std::numeric_limits<std::uint32_t>::max()) {
            return;
        }
        page_words.push_back(static_cast<std::uint32_t>(conventional_count + page.physical_slot));
        page_words.push_back(page.resolved_mip);
        page_words.push_back(page.resolved_tile_x);
        page_words.push_back(page.resolved_tile_y);
    }

    auto next_descriptors = create_uint_table_texture(
        device, descriptor_words, "VICE Runtime virtual texture descriptors"
    );
    auto next_pages = create_uint_table_texture(
        device, page_words, "VICE Runtime virtual texture page table"
    );
    if (!next_descriptors || !next_pages) return;

    for (auto& record : tagged_plan.records) {
        if (!record_uses_virtual_texture(record)) continue;
        record.reserved[0] = descriptor_slot;
        record.reserved[1] = page_slot;
        record.reserved[2] = runtime_gpu_resource_unassigned;
    }

    material_textures.insert(
        material_textures.end(),
        next_sampling.physical_textures.begin(),
        next_sampling.physical_textures.end()
    );
    material_textures.push_back(next_descriptors.get());
    material_textures.push_back(next_pages.get());
    gpu_records = std::move(tagged_plan.records);
    resident_pool = std::move(next_pool);
    sampling_plan = std::move(next_sampling);
    descriptor_table = std::move(next_descriptors);
    page_table = std::move(next_pages);
}

} // namespace

void RuntimeGpuMaterialResourceTable::clear() noexcept {
    textures_.clear();
    samplers_.clear();
    owned_samplers_.clear();
    material_records_.clear();
    virtual_texture_descriptors_.reset();
    virtual_texture_pages_.reset();
    virtual_sampling_plan_ = {};
    virtual_resident_pool_.reset();
}

bool RuntimeGpuMaterialResourceTable::initialize(
    render::Device& device,
    const RuntimeGpuMaterialTablePlan& plan,
    const RuntimeTextureLibrary& texture_library,
    std::string* const diagnostics
) {
    clear();
    material_records_ = plan.records;
    return build_resources(
        device,
        plan,
        [&](const world::ResourceId resource_id, const RuntimeGpuMaterialTextureChannel channel) {
            return texture_library.resolve(resource_id, material_channel(channel));
        },
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
    material_records_ = plan.records;
    if (!build_resources(
            device,
            plan,
            [&](const world::ResourceId resource_id, const RuntimeGpuMaterialTextureChannel channel) {
                return material_library.resolve_texture(resource_id, material_channel(channel));
            },
            textures_,
            owned_samplers_,
            samplers_,
            diagnostics
        )) {
        return false;
    }
    build_virtual_resources(
        device,
        plan,
        material_library,
        material_records_,
        textures_,
        virtual_resident_pool_,
        virtual_sampling_plan_,
        virtual_texture_descriptors_,
        virtual_texture_pages_
    );
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeGpuMaterialResourceTableView RuntimeGpuMaterialResourceTable::view() const noexcept {
    return RuntimeGpuMaterialResourceTableView{
        .textures = textures_,
        .samplers = samplers_,
    };
}

std::span<const RuntimeGpuMaterialRecord> RuntimeGpuMaterialResourceTable::material_records() const noexcept {
    return material_records_;
}

} // namespace vice::runtime::detail
