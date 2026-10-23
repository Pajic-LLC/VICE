#include <vice/runtime/virtual_texture_sampling.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <tuple>
#include <vector>

namespace vice::runtime {
namespace {

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

[[nodiscard]] bool key_less(
    const RuntimeVirtualTextureTileKey& left,
    const RuntimeVirtualTextureTileKey& right
) noexcept {
    if (left.texture != right.texture) return resource_less(left.texture, right.texture);
    return std::tie(left.mip_level, left.tile_y, left.tile_x) <
        std::tie(right.mip_level, right.tile_y, right.tile_x);
}

[[nodiscard]] constexpr std::uint32_t next_mip_extent(const std::uint32_t value) noexcept {
    return value > 1U ? (value + 1U) / 2U : 1U;
}

[[nodiscard]] std::uint32_t physical_slot(
    const std::span<const RuntimeVirtualTextureTileKey> keys,
    const RuntimeVirtualTextureTileKey& key
) noexcept {
    const auto found = std::lower_bound(keys.begin(), keys.end(), key, key_less);
    if (found == keys.end() || *found != key) return runtime_virtual_texture_invalid_index;
    const auto index = static_cast<std::uint64_t>(std::distance(keys.begin(), found));
    return index <= std::numeric_limits<std::uint32_t>::max()
        ? static_cast<std::uint32_t>(index)
        : runtime_virtual_texture_invalid_index;
}

} // namespace

std::optional<std::uint32_t> RuntimeVirtualTextureSamplingPlan::texture_index(
    const world::ResourceId texture
) const noexcept {
    const auto found = std::lower_bound(texture_ids.begin(), texture_ids.end(), texture, resource_less);
    if (found == texture_ids.end() || *found != texture) return std::nullopt;
    return static_cast<std::uint32_t>(std::distance(texture_ids.begin(), found));
}

std::uint32_t runtime_virtual_texture_page_index(
    const RuntimeGpuVirtualTextureDescriptor& descriptor,
    const std::uint32_t mip_level,
    const std::uint32_t tile_x,
    const std::uint32_t tile_y
) noexcept {
    if (descriptor.width == 0U || descriptor.height == 0U || descriptor.mip_count == 0U ||
        mip_level >= descriptor.mip_count) {
        return runtime_virtual_texture_invalid_index;
    }
    std::uint64_t page = descriptor.first_page;
    std::uint32_t width = descriptor.width;
    std::uint32_t height = descriptor.height;
    for (std::uint32_t mip = 0U; mip < mip_level; ++mip) {
        const std::uint64_t columns =
            (static_cast<std::uint64_t>(width) + render::virtual_texture_tile_extent - 1U) /
            render::virtual_texture_tile_extent;
        const std::uint64_t rows =
            (static_cast<std::uint64_t>(height) + render::virtual_texture_tile_extent - 1U) /
            render::virtual_texture_tile_extent;
        if (columns != 0U && rows > std::numeric_limits<std::uint64_t>::max() / columns) {
            return runtime_virtual_texture_invalid_index;
        }
        page += columns * rows;
        if (page > std::numeric_limits<std::uint32_t>::max()) {
            return runtime_virtual_texture_invalid_index;
        }
        width = next_mip_extent(width);
        height = next_mip_extent(height);
    }
    const std::uint64_t columns =
        (static_cast<std::uint64_t>(width) + render::virtual_texture_tile_extent - 1U) /
        render::virtual_texture_tile_extent;
    const std::uint64_t rows =
        (static_cast<std::uint64_t>(height) + render::virtual_texture_tile_extent - 1U) /
        render::virtual_texture_tile_extent;
    if (tile_x >= columns || tile_y >= rows ||
        static_cast<std::uint64_t>(tile_y) >
            (std::numeric_limits<std::uint64_t>::max() - tile_x) / std::max<std::uint64_t>(columns, 1U)) {
        return runtime_virtual_texture_invalid_index;
    }
    page += static_cast<std::uint64_t>(tile_y) * columns + tile_x;
    return page <= std::numeric_limits<std::uint32_t>::max()
        ? static_cast<std::uint32_t>(page)
        : runtime_virtual_texture_invalid_index;
}

RuntimeVirtualTextureSamplingPlan build_runtime_virtual_texture_sampling_plan(
    const CookedTextureCatalog& catalog,
    const RuntimeVirtualTextureResidentPool& resident_pool,
    const std::span<const world::ResourceId> textures
) {
    RuntimeVirtualTextureSamplingPlan result;
    result.texture_ids.assign(textures.begin(), textures.end());
    for (const auto texture : result.texture_ids) {
        if (!texture.valid()) {
            result.diagnostics = "Runtime virtual texture sampling request contains an invalid texture identity.";
            return result;
        }
    }
    std::sort(result.texture_ids.begin(), result.texture_ids.end(), resource_less);
    result.texture_ids.erase(
        std::unique(result.texture_ids.begin(), result.texture_ids.end()),
        result.texture_ids.end()
    );
    if (result.texture_ids.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime virtual texture sampling texture count exceeds the GPU index contract.";
        return result;
    }

    for (const auto& resident : resident_pool.tiles()) {
        if (!std::binary_search(
                result.texture_ids.begin(), result.texture_ids.end(), resident.key.texture, resource_less
            )) {
            continue;
        }
        if (resident.texture == nullptr) {
            result.diagnostics = "Runtime virtual texture sampling found resident metadata without a physical texture.";
            return result;
        }
        result.physical_keys.push_back(resident.key);
        result.physical_textures.push_back(resident.texture.get());
    }
    if (result.physical_keys.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime virtual texture physical descriptor array exceeds the GPU index contract.";
        return result;
    }

    result.textures.reserve(result.texture_ids.size());
    for (const auto texture_id : result.texture_ids) {
        const auto* const record = catalog.find(texture_id);
        if (record == nullptr || record->asset.version != render::texture_asset_format_version_v2 ||
            record->asset.mip_levels.empty() || record->asset.virtual_tiles.empty()) {
            result.diagnostics = "Runtime virtual texture sampling requires canonical TextureAsset v2 metadata.";
            return result;
        }
        if (record->asset.mip_levels.size() > std::numeric_limits<std::uint32_t>::max() ||
            result.pages.size() > std::numeric_limits<std::uint32_t>::max()) {
            result.diagnostics = "Runtime virtual texture sampling metadata exceeds the GPU index contract.";
            return result;
        }
        const std::uint32_t mip_count = static_cast<std::uint32_t>(record->asset.mip_levels.size());
        const RuntimeVirtualTextureTileKey terminal{
            .texture = texture_id,
            .mip_level = mip_count - 1U,
            .tile_x = 0U,
            .tile_y = 0U,
        };
        if (physical_slot(result.physical_keys, terminal) == runtime_virtual_texture_invalid_index) {
            result.diagnostics = "Runtime virtual texture sampling requires the terminal mip tile to be resident.";
            return result;
        }

        const RuntimeGpuVirtualTextureDescriptor descriptor{
            .width = record->asset.width,
            .height = record->asset.height,
            .mip_count = mip_count,
            .first_page = static_cast<std::uint32_t>(result.pages.size()),
        };
        result.textures.push_back(descriptor);
        result.pages.reserve(result.pages.size() + record->asset.virtual_tiles.size());

        for (const auto& tile : record->asset.virtual_tiles) {
            RuntimeVirtualTextureTileKey resolved{
                .texture = texture_id,
                .mip_level = tile.mip_level,
                .tile_x = tile.tile_x,
                .tile_y = tile.tile_y,
            };
            std::uint32_t slot = physical_slot(result.physical_keys, resolved);
            while (slot == runtime_virtual_texture_invalid_index && resolved.mip_level + 1U < mip_count) {
                ++resolved.mip_level;
                resolved.tile_x /= 2U;
                resolved.tile_y /= 2U;
                slot = physical_slot(result.physical_keys, resolved);
            }
            if (slot == runtime_virtual_texture_invalid_index) {
                result.diagnostics = "Runtime virtual texture page could not resolve a resident ancestor.";
                return result;
            }
            result.pages.push_back(RuntimeGpuVirtualTexturePage{
                .physical_slot = slot,
                .resolved_mip = resolved.mip_level,
                .resolved_tile_x = resolved.tile_x,
                .resolved_tile_y = resolved.tile_y,
            });
        }
    }

    if (result.pages.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime virtual texture page table exceeds the GPU index contract.";
        return result;
    }
    return result;
}

} // namespace vice::runtime
