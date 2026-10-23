#include <vice/render/rhi.hpp>
#include <vice/render/texture_asset.hpp>
#include <vice/render/virtual_texture_archive.hpp>
#include <vice/runtime/texture.hpp>
#include <vice/runtime/virtual_texture_sampling.hpp>
#include <vice/runtime/virtual_texture_streaming.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/resource.hpp>

#include "../engine/runtime/src/texture_pipeline.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

vice::world::PersistentId required_id(const char* text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    if (!parsed.has_value()) std::terminate();
    return *parsed;
}

[[nodiscard]] bool write_bytes(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes
) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

[[nodiscard]] constexpr std::uint32_t next_extent(const std::uint32_t value) noexcept {
    return value > 1U ? (value + 1U) / 2U : 1U;
}

[[nodiscard]] vice::render::TextureAsset make_virtual_texture_asset(
    const vice::world::PersistentId id
) {
    using namespace vice;
    render::TextureAsset asset;
    asset.version = render::texture_asset_format_version_v2;
    asset.stable_id = id.bytes();
    asset.source_fingerprint = 99U;
    asset.width = 256U;
    asset.height = 128U;
    asset.color_space = render::TextureColorSpace::srgb;

    std::uint32_t width = asset.width;
    std::uint32_t height = asset.height;
    std::uint32_t mip = 0U;
    for (;;) {
        const auto payload_offset = static_cast<std::uint64_t>(asset.rgba8.size());
        const auto payload_size = static_cast<std::uint64_t>(width) * height * 4U;
        const auto first_tile = static_cast<std::uint32_t>(asset.virtual_tiles.size());
        const std::uint32_t columns = (width + render::virtual_texture_tile_extent - 1U) /
            render::virtual_texture_tile_extent;
        const std::uint32_t rows = (height + render::virtual_texture_tile_extent - 1U) /
            render::virtual_texture_tile_extent;
        for (std::uint32_t tile_y = 0U; tile_y < rows; ++tile_y) {
            for (std::uint32_t tile_x = 0U; tile_x < columns; ++tile_x) {
                const std::uint32_t pixel_x = tile_x * render::virtual_texture_tile_extent;
                const std::uint32_t pixel_y = tile_y * render::virtual_texture_tile_extent;
                asset.virtual_tiles.push_back(render::VirtualTextureTile{
                    .mip_level = mip,
                    .tile_x = tile_x,
                    .tile_y = tile_y,
                    .width = std::min(render::virtual_texture_tile_extent, width - pixel_x),
                    .height = std::min(render::virtual_texture_tile_extent, height - pixel_y),
                });
            }
        }
        asset.mip_levels.push_back(render::TextureMipLevel{
            .width = width,
            .height = height,
            .payload_offset = payload_offset,
            .payload_size = payload_size,
            .first_tile = first_tile,
            .tile_count = columns * rows,
        });
        const std::size_t start = asset.rgba8.size();
        asset.rgba8.resize(start + static_cast<std::size_t>(payload_size));
        for (std::size_t index = 0U; index < static_cast<std::size_t>(payload_size); ++index) {
            asset.rgba8[start + index] = static_cast<std::byte>((index + mip * 37U) & 0xFFU);
        }
        if (width == 1U && height == 1U) break;
        width = next_extent(width);
        height = next_extent(height);
        ++mip;
    }
    return asset;
}

[[nodiscard]] std::vector<std::byte> canonical_tile_bytes(
    const vice::render::TextureAsset& asset,
    const vice::render::VirtualTextureArchiveTile& tile
) {
    const auto mip = vice::render::texture_mip_rgba8(asset, tile.mip_level);
    const auto& mip_desc = asset.mip_levels[tile.mip_level];
    const std::uint64_t origin_x = static_cast<std::uint64_t>(tile.tile_x) *
        vice::render::virtual_texture_tile_extent;
    const std::uint64_t origin_y = static_cast<std::uint64_t>(tile.tile_y) *
        vice::render::virtual_texture_tile_extent;
    const std::uint64_t source_row = static_cast<std::uint64_t>(mip_desc.width) * 4U;
    const std::uint64_t tile_row = static_cast<std::uint64_t>(tile.width) * 4U;
    std::vector<std::byte> result(static_cast<std::size_t>(tile_row * tile.height));
    for (std::uint32_t row = 0U; row < tile.height; ++row) {
        const auto source_offset = static_cast<std::size_t>((origin_y + row) * source_row + origin_x * 4U);
        const auto destination_offset = static_cast<std::size_t>(static_cast<std::uint64_t>(row) * tile_row);
        std::copy_n(
            mip.begin() + static_cast<std::ptrdiff_t>(source_offset),
            static_cast<std::size_t>(tile_row),
            result.begin() + static_cast<std::ptrdiff_t>(destination_offset)
        );
    }
    return result;
}

[[nodiscard]] bool same_keys(
    const std::span<const vice::runtime::RuntimeVirtualTextureResidentTile> tiles,
    const std::vector<vice::runtime::RuntimeVirtualTextureTileKey>& keys
) {
    if (tiles.size() != keys.size()) return false;
    for (const auto& key : keys) {
        if (std::find_if(tiles.begin(), tiles.end(), [&](const auto& tile) {
                return tile.key == key;
            }) == tiles.end()) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    using namespace vice;

    const auto root = std::filesystem::temp_directory_path() / "vice_runtime_texture_tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    if (error) return 1;

    const auto id = required_id("71000000-0000-4000-8000-000000000001");
    const world::ResourceId resource{id};
    const auto asset = make_virtual_texture_asset(id);
    const auto encoded = render::encode_texture_asset(asset);
    const auto archive = render::encode_virtual_texture_archive(asset);
    const auto archive_again = render::encode_virtual_texture_archive(asset);
    if (!encoded || !archive || !archive_again || archive.bytes != archive_again.bytes) return 1;

    const auto texture_path = root / "surface.vtexc";
    const auto archive_path = root / "surface.vtilec";
    if (!write_bytes(texture_path, encoded.bytes) || !write_bytes(archive_path, archive.bytes)) return 1;

    runtime::CookedTextureCatalog catalog;
    if (!catalog.load(root) || catalog.records().size() != 1U || catalog.find(resource) == nullptr) return 1;
    const auto* found = catalog.find(resource);
    if (found->asset.width != asset.width || render::texture_mip_count(found->asset) != asset.mip_levels.size()) {
        return 1;
    }

    auto device = render::Device::create();
    if (!device) return 1;
    runtime::detail::RuntimeTextureLibrary library;
    std::string diagnostics;
    if (!library.initialize(*device, root, &diagnostics, true) || !diagnostics.empty()) return 1;
    auto* const full_texture = library.find(resource);
    if (full_texture == nullptr || full_texture->mip_levels() != asset.mip_levels.size() ||
        library.resident_bytes() != asset.rgba8.size()) {
        return 1;
    }

    const auto directory = render::load_virtual_texture_archive_directory(archive_path);
    if (!directory.directory.has_value() ||
        !render::virtual_texture_archive_matches_asset(*directory.directory, asset) ||
        directory.directory->tiles.size() < 3U) {
        return 1;
    }

    const auto first_descriptor = directory.directory->tiles[0U];
    const auto second_descriptor = directory.directory->tiles[1U];
    const auto third_descriptor = directory.directory->tiles[2U];
    const auto first_tile = render::load_virtual_texture_archive_tile(
        archive_path,
        *directory.directory,
        first_descriptor.mip_level,
        first_descriptor.tile_x,
        first_descriptor.tile_y
    );
    if (!first_tile.tile.has_value() || first_tile.tile->rgba8 != canonical_tile_bytes(asset, first_descriptor)) {
        return 1;
    }

    auto corrupted = archive.bytes;
    corrupted[static_cast<std::size_t>(first_descriptor.payload_offset)] ^= std::byte{0x01U};
    const auto corrupt_path = root / "corrupt.vtilec";
    if (!write_bytes(corrupt_path, corrupted)) return 1;
    const auto corrupt_directory = render::load_virtual_texture_archive_directory(corrupt_path);
    if (!corrupt_directory.directory.has_value()) return 1;
    if (render::load_virtual_texture_archive_tile(
            corrupt_path,
            *corrupt_directory.directory,
            first_descriptor.mip_level,
            first_descriptor.tile_x,
            first_descriptor.tile_y
        ).tile.has_value()) {
        return 1;
    }
    if (!render::load_virtual_texture_archive_tile(
            corrupt_path,
            *corrupt_directory.directory,
            second_descriptor.mip_level,
            second_descriptor.tile_x,
            second_descriptor.tile_y
        ).tile.has_value()) {
        return 1;
    }

    auto corrupt_directory_bytes = archive.bytes;
    if (corrupt_directory_bytes.size() < 40U) return 1;
    corrupt_directory_bytes[corrupt_directory_bytes.size() - 40U] ^= std::byte{0x01U};
    const auto corrupt_directory_path = root / "corrupt_directory.vtilec";
    if (!write_bytes(corrupt_directory_path, corrupt_directory_bytes) ||
        render::load_virtual_texture_archive_directory(corrupt_directory_path).directory.has_value()) {
        return 1;
    }

    const runtime::RuntimeVirtualTextureTileKey key_a{
        .texture = resource,
        .mip_level = first_descriptor.mip_level,
        .tile_x = first_descriptor.tile_x,
        .tile_y = first_descriptor.tile_y,
    };
    const runtime::RuntimeVirtualTextureTileKey key_b{
        .texture = resource,
        .mip_level = second_descriptor.mip_level,
        .tile_x = second_descriptor.tile_x,
        .tile_y = second_descriptor.tile_y,
    };
    const runtime::RuntimeVirtualTextureTileKey key_c{
        .texture = resource,
        .mip_level = third_descriptor.mip_level,
        .tile_x = third_descriptor.tile_x,
        .tile_y = third_descriptor.tile_y,
    };
    const std::uint64_t budget = first_descriptor.payload_size + second_descriptor.payload_size;
    runtime::RuntimeVirtualTextureResidentPool pool{budget};

    const runtime::RuntimeVirtualTextureTileKey duplicate_a[]{key_a, key_a};
    const auto first_update = pool.update(*device, catalog, duplicate_a);
    if (!first_update || first_update.requested != 1U || first_update.loaded != 1U ||
        first_update.resident_hits != 0U || !pool.resident(key_a) || pool.find(key_a) == nullptr) {
        return 1;
    }

    const runtime::RuntimeVirtualTextureTileKey ab[]{key_a, key_b};
    const auto second_update = pool.update(*device, catalog, ab);
    if (!second_update || second_update.resident_hits != 1U || second_update.loaded != 1U ||
        !same_keys(pool.tiles(), {key_a, key_b})) {
        return 1;
    }

    const runtime::RuntimeVirtualTextureTileKey touch_b[]{key_b};
    const auto touch_update = pool.update(*device, catalog, touch_b);
    if (!touch_update || touch_update.resident_hits != 1U) return 1;

    const runtime::RuntimeVirtualTextureTileKey bc[]{key_b, key_c};
    const auto eviction_update = pool.update(*device, catalog, bc);
    if (!eviction_update || eviction_update.resident_hits != 1U || eviction_update.loaded != 1U ||
        eviction_update.evicted != 1U || pool.resident(key_a) ||
        !same_keys(pool.tiles(), {key_b, key_c})) {
        return 1;
    }

    const auto before_failure_bytes = pool.resident_bytes();
    const runtime::RuntimeVirtualTextureTileKey abc[]{key_a, key_b, key_c};
    const auto failed_update = pool.update(*device, catalog, abc);
    if (failed_update || pool.resident_bytes() != before_failure_bytes ||
        !same_keys(pool.tiles(), {key_b, key_c})) {
        return 1;
    }

    const auto terminal_descriptor = directory.directory->tiles.back();
    const runtime::RuntimeVirtualTextureTileKey terminal_key{
        .texture = resource,
        .mip_level = terminal_descriptor.mip_level,
        .tile_x = terminal_descriptor.tile_x,
        .tile_y = terminal_descriptor.tile_y,
    };
    runtime::RuntimeVirtualTextureResidentPool sampling_pool{asset.rgba8.size()};
    const runtime::RuntimeVirtualTextureTileKey sampling_requests[]{key_a, key_c, terminal_key};
    const auto sampling_update = sampling_pool.update(*device, catalog, sampling_requests);
    if (!sampling_update || !sampling_pool.resident(key_a) || !sampling_pool.resident(key_c) ||
        !sampling_pool.resident(terminal_key)) {
        return 1;
    }

    const world::ResourceId sampling_textures[]{resource};
    const auto sampling = runtime::build_runtime_virtual_texture_sampling_plan(
        catalog, sampling_pool, sampling_textures
    );
    if (!sampling || sampling.texture_ids.size() != 1U || sampling.textures.size() != 1U ||
        sampling.pages.size() != asset.virtual_tiles.size() || !sampling.texture_index(resource).has_value() ||
        *sampling.texture_index(resource) != 0U || sampling.physical_textures.size() != 3U) {
        return 1;
    }

    const auto& descriptor = sampling.textures.front();
    const auto exact_page_index = runtime::runtime_virtual_texture_page_index(
        descriptor, key_a.mip_level, key_a.tile_x, key_a.tile_y
    );
    const auto fallback_page_index = runtime::runtime_virtual_texture_page_index(
        descriptor, key_b.mip_level, key_b.tile_x, key_b.tile_y
    );
    const auto terminal_page_index = runtime::runtime_virtual_texture_page_index(
        descriptor, terminal_key.mip_level, terminal_key.tile_x, terminal_key.tile_y
    );
    if (exact_page_index == runtime::runtime_virtual_texture_invalid_index ||
        fallback_page_index == runtime::runtime_virtual_texture_invalid_index ||
        terminal_page_index == runtime::runtime_virtual_texture_invalid_index ||
        exact_page_index >= sampling.pages.size() || fallback_page_index >= sampling.pages.size() ||
        terminal_page_index >= sampling.pages.size()) {
        return 1;
    }
    const auto& exact_page = sampling.pages[exact_page_index];
    const auto& fallback_page = sampling.pages[fallback_page_index];
    const auto& terminal_page = sampling.pages[terminal_page_index];
    if (exact_page.resolved_mip != key_a.mip_level || exact_page.resolved_tile_x != key_a.tile_x ||
        exact_page.resolved_tile_y != key_a.tile_y ||
        fallback_page.resolved_mip != key_c.mip_level || fallback_page.resolved_tile_x != key_c.tile_x ||
        fallback_page.resolved_tile_y != key_c.tile_y ||
        terminal_page.resolved_mip != terminal_key.mip_level ||
        terminal_page.resolved_tile_x != terminal_key.tile_x ||
        terminal_page.resolved_tile_y != terminal_key.tile_y) {
        return 1;
    }

    runtime::RuntimeGpuMaterialTablePlan virtual_materials;
    virtual_materials.texture_ids = {resource};
    virtual_materials.samplers.resize(2U);
    virtual_materials.samplers[1U].address_u = render::AddressMode::clamp_to_edge;
    virtual_materials.samplers[1U].address_v = render::AddressMode::clamp_to_edge;
    virtual_materials.records.resize(2U);
    virtual_materials.records[0U].base_color_texture = 0U;
    virtual_materials.records[0U].sampler = 0U;
    virtual_materials.records[1U].base_color_texture = 0U;
    virtual_materials.records[1U].sampler = 1U;
    if (!runtime::apply_runtime_virtual_texture_sampling(virtual_materials, sampling, &diagnostics) ||
        !diagnostics.empty()) {
        return 1;
    }
    if ((virtual_materials.records[0U].base_color_texture & runtime::runtime_gpu_virtual_texture_flag) == 0U ||
        (virtual_materials.records[0U].base_color_texture & runtime::runtime_gpu_virtual_texture_index_mask) != 0U ||
        virtual_materials.records[1U].base_color_texture != 0U ||
        virtual_materials.records[0U].reserved[0U] != runtime::runtime_gpu_resource_unassigned ||
        virtual_materials.records[0U].reserved[1U] != runtime::runtime_gpu_resource_unassigned) {
        return 1;
    }

    runtime::RuntimeVirtualTextureResidentPool missing_terminal_pool{asset.rgba8.size()};
    const runtime::RuntimeVirtualTextureTileKey missing_terminal_requests[]{key_a, key_c};
    if (!missing_terminal_pool.update(*device, catalog, missing_terminal_requests)) return 1;
    if (runtime::build_runtime_virtual_texture_sampling_plan(
            catalog, missing_terminal_pool, sampling_textures
        )) {
        return 1;
    }

    if (!device->wait_idle()) return 1;

    if (!write_bytes(root / "duplicate.vtexc", encoded.bytes)) return 1;
    if (catalog.load(root) || catalog.diagnostics().find(id.to_string()) == std::string::npos) return 1;

    std::filesystem::remove_all(root, error);
    return error ? 1 : 0;
}
