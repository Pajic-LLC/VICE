#include <vice/render/texture_asset.hpp>
#include <vice/render/virtual_texture_archive.hpp>
#include <vice/runtime/texture.hpp>
#include <vice/runtime/virtual_texture_streaming.hpp>
#include <vice/tools/texture_cooker.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/resource.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto id = vice::world::PersistentId::parse(text);
    if (!id.has_value()) std::terminate();
    return *id;
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

[[nodiscard]] std::vector<std::byte> ppm_fixture(
    const std::uint32_t width,
    const std::uint32_t height
) {
    const std::string header = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
    const auto header_bytes = std::as_bytes(std::span{header.data(), header.size()});
    std::vector<std::byte> bytes(header_bytes.begin(), header_bytes.end());
    bytes.reserve(bytes.size() + static_cast<std::size_t>(width) * height * 3U);
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            bytes.push_back(static_cast<std::byte>((x * 3U + y) & 0xFFU));
            bytes.push_back(static_cast<std::byte>((x + y * 5U) & 0xFFU));
            bytes.push_back(static_cast<std::byte>((x * 7U + y * 11U) & 0xFFU));
        }
    }
    return bytes;
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
        const auto found = std::find_if(tiles.begin(), tiles.end(), [&](const auto& tile) {
            return tile.key == key;
        });
        if (found == tiles.end()) return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace vice;

    const auto root = std::filesystem::temp_directory_path() / "vice_virtual_texture_streaming_tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    if (error) {
        std::cerr << "Could not create virtual texture test directory.\n";
        return 1;
    }

    const auto persistent_id = required_id("8a000000-0000-4000-8000-000000000001");
    const world::ResourceId resource{persistent_id};
    tools::texture_cooker::TextureSource source;
    source.stable_id = persistent_id.bytes();
    source.image_source = "texture.ppm";
    source.color_space = render::TextureColorSpace::srgb;
    source.manifest_fingerprint = 0x12345678ULL;

    const auto cooked = tools::texture_cooker::cook(source, ppm_fixture(256U, 128U));
    if (!cooked.success) {
        std::cerr << "Could not cook virtual texture fixture: " << cooked.diagnostics << '\n';
        return 1;
    }
    const auto decoded = render::decode_texture_asset(cooked.bytes);
    if (!decoded.asset.has_value() || decoded.asset->version != render::texture_asset_format_version_v2 ||
        decoded.asset->virtual_tiles.size() < 3U) {
        std::cerr << "Virtual texture fixture did not decode as canonical v2.\n";
        return 1;
    }

    const auto archive = render::encode_virtual_texture_archive(*decoded.asset);
    const auto archive_again = render::encode_virtual_texture_archive(*decoded.asset);
    if (!archive || !archive_again || archive.bytes != archive_again.bytes) {
        std::cerr << "Virtual texture archive cooking is not deterministic: " << archive.diagnostics << '\n';
        return 1;
    }

    const auto texture_path = root / "surface.vtexc";
    const auto archive_path = root / "surface.vtilec";
    if (!write_bytes(texture_path, cooked.bytes) || !write_bytes(archive_path, archive.bytes)) {
        std::cerr << "Could not write virtual texture fixture files.\n";
        return 1;
    }

    const auto directory = render::load_virtual_texture_archive_directory(archive_path);
    if (!directory.directory.has_value() ||
        !render::virtual_texture_archive_matches_asset(*directory.directory, *decoded.asset) ||
        directory.directory->tiles.size() != decoded.asset->virtual_tiles.size()) {
        std::cerr << "Could not load canonical virtual texture archive directory: "
                  << directory.diagnostics << '\n';
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
    if (!first_tile.tile.has_value() ||
        first_tile.tile->rgba8 != canonical_tile_bytes(*decoded.asset, first_descriptor)) {
        std::cerr << "Direct virtual texture tile seek did not reproduce the canonical mip subrect.\n";
        return 1;
    }

    auto corrupted = archive.bytes;
    if (first_descriptor.payload_offset >= corrupted.size()) return 1;
    corrupted[static_cast<std::size_t>(first_descriptor.payload_offset)] ^= std::byte{0x01U};
    const auto corrupt_path = root / "corrupt.vtilec";
    if (!write_bytes(corrupt_path, corrupted)) return 1;
    const auto corrupt_directory = render::load_virtual_texture_archive_directory(corrupt_path);
    if (!corrupt_directory.directory.has_value()) {
        std::cerr << "Payload corruption incorrectly prevented directory-only loading.\n";
        return 1;
    }
    if (render::load_virtual_texture_archive_tile(
            corrupt_path,
            *corrupt_directory.directory,
            first_descriptor.mip_level,
            first_descriptor.tile_x,
            first_descriptor.tile_y
        ).tile.has_value()) {
        std::cerr << "Corrupted virtual texture tile payload passed its fingerprint.\n";
        return 1;
    }
    if (!render::load_virtual_texture_archive_tile(
            corrupt_path,
            *corrupt_directory.directory,
            second_descriptor.mip_level,
            second_descriptor.tile_x,
            second_descriptor.tile_y
        ).tile.has_value()) {
        std::cerr << "One corrupt tile poisoned an unrelated tile payload.\n";
        return 1;
    }

    auto corrupt_directory_bytes = archive.bytes;
    if (corrupt_directory_bytes.size() < 40U) return 1;
    corrupt_directory_bytes[corrupt_directory_bytes.size() - 40U] ^= std::byte{0x01U};
    const auto corrupt_directory_path = root / "corrupt_directory.vtilec";
    if (!write_bytes(corrupt_directory_path, corrupt_directory_bytes) ||
        render::load_virtual_texture_archive_directory(corrupt_directory_path).directory.has_value()) {
        std::cerr << "Corrupted virtual texture directory was accepted.\n";
        return 1;
    }

    runtime::CookedTextureCatalog catalog;
    if (!catalog.load(root) || catalog.find(resource) == nullptr) {
        std::cerr << "Could not load virtual texture fixture into Runtime catalog: "
                  << catalog.diagnostics() << '\n';
        return 1;
    }
    auto device = render::Device::create();
    if (!device) {
        std::cerr << "Could not create Tier-1 render device for tile residency test.\n";
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
        std::cerr << "Virtual texture request dedupe/initial upload failed: "
                  << first_update.diagnostics << '\n';
        return 1;
    }

    const runtime::RuntimeVirtualTextureTileKey ab[]{key_a, key_b};
    const auto second_update = pool.update(*device, catalog, ab);
    if (!second_update || second_update.resident_hits != 1U || second_update.loaded != 1U ||
        !same_keys(pool.tiles(), {key_a, key_b})) {
        std::cerr << "Virtual texture resident hit/second upload failed: "
                  << second_update.diagnostics << '\n';
        return 1;
    }

    const runtime::RuntimeVirtualTextureTileKey touch_b[]{key_b};
    const auto touch_update = pool.update(*device, catalog, touch_b);
    if (!touch_update || touch_update.resident_hits != 1U) {
        std::cerr << "Virtual texture LRU touch failed.\n";
        return 1;
    }

    const runtime::RuntimeVirtualTextureTileKey bc[]{key_b, key_c};
    const auto eviction_update = pool.update(*device, catalog, bc);
    if (!eviction_update || eviction_update.resident_hits != 1U || eviction_update.loaded != 1U ||
        eviction_update.evicted != 1U || pool.resident(key_a) ||
        !same_keys(pool.tiles(), {key_b, key_c})) {
        std::cerr << "Virtual texture deterministic LRU eviction failed: "
                  << eviction_update.diagnostics << '\n';
        return 1;
    }

    const auto before_failure_bytes = pool.resident_bytes();
    const runtime::RuntimeVirtualTextureTileKey abc[]{key_a, key_b, key_c};
    const auto failed_update = pool.update(*device, catalog, abc);
    if (failed_update || pool.resident_bytes() != before_failure_bytes ||
        !same_keys(pool.tiles(), {key_b, key_c})) {
        std::cerr << "Virtual texture over-budget update was not transactional.\n";
        return 1;
    }

    if (!device->wait_idle()) {
        std::cerr << "Virtual texture residency device did not become idle.\n";
        return 1;
    }
    std::filesystem::remove_all(root, error);
    return error ? 1 : 0;
}
