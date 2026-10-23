#include <vice/render/texture_asset.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace vice::render {
namespace {

constexpr std::array<std::byte, texture_asset_magic_size> texture_magic{
    std::byte{'V'}, std::byte{'I'}, std::byte{'C'}, std::byte{'E'},
    std::byte{'T'}, std::byte{'E'}, std::byte{'X'}, std::byte{'R'},
};
constexpr std::size_t common_prefix_size = texture_asset_magic_size + sizeof(std::uint32_t) +
    texture_stable_id_size + sizeof(std::uint64_t) * 2U;
constexpr std::size_t v1_header_size = common_prefix_size + sizeof(std::uint32_t) * 2U + 1U;
constexpr std::size_t v2_header_size = v1_header_size + sizeof(std::uint32_t) * 2U;
constexpr std::size_t mip_descriptor_size = sizeof(std::uint32_t) * 4U + sizeof(std::uint64_t) * 2U;
constexpr std::size_t tile_descriptor_size = sizeof(std::uint32_t) * 5U;
constexpr std::uint32_t max_texture_dimension = 16'384U;
constexpr std::uint64_t max_texture_bytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t max_texture_mip_levels = 32U;
constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

[[nodiscard]] std::uint64_t fingerprint(const std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const std::byte value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
}

[[nodiscard]] bool valid_color_space(const TextureColorSpace value) noexcept {
    return value == TextureColorSpace::linear || value == TextureColorSpace::srgb;
}

[[nodiscard]] bool mip_payload_size(
    const std::uint32_t width,
    const std::uint32_t height,
    std::uint64_t& size
) noexcept {
    if (width == 0U || height == 0U || width > max_texture_dimension ||
        height > max_texture_dimension) {
        return false;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    if (pixels > max_texture_bytes / 4U) return false;
    size = pixels * 4U;
    return true;
}

[[nodiscard]] constexpr std::uint32_t next_mip_extent(const std::uint32_t extent) noexcept {
    return extent > 1U ? (extent + 1U) / 2U : 1U;
}

[[nodiscard]] std::uint32_t expected_mip_count(
    std::uint32_t width,
    std::uint32_t height
) noexcept {
    std::uint32_t count = 0U;
    do {
        ++count;
        width = next_mip_extent(width);
        height = next_mip_extent(height);
    } while ((width != 1U || height != 1U) && count < max_texture_mip_levels);
    if (width == 1U && height == 1U) return count + (count == 0U ? 1U : 0U);
    return 0U;
}

[[nodiscard]] bool expected_virtual_tile_count(
    std::uint32_t width,
    std::uint32_t height,
    const std::uint32_t mip_count,
    std::uint64_t& tile_count
) noexcept {
    tile_count = 0U;
    for (std::uint32_t mip = 0U; mip < mip_count; ++mip) {
        const std::uint64_t columns =
            (static_cast<std::uint64_t>(width) + virtual_texture_tile_extent - 1U) /
            virtual_texture_tile_extent;
        const std::uint64_t rows =
            (static_cast<std::uint64_t>(height) + virtual_texture_tile_extent - 1U) /
            virtual_texture_tile_extent;
        if (columns == 0U || rows == 0U || columns > std::numeric_limits<std::uint32_t>::max() ||
            rows > std::numeric_limits<std::uint32_t>::max() ||
            rows > std::numeric_limits<std::uint64_t>::max() / columns ||
            tile_count > std::numeric_limits<std::uint64_t>::max() - columns * rows) {
            return false;
        }
        tile_count += columns * rows;
        width = next_mip_extent(width);
        height = next_mip_extent(height);
    }
    return tile_count <= std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] bool validate_v2_layout(const TextureAsset& asset) noexcept {
    if (asset.version != texture_asset_format_version_v2 || asset.mip_levels.empty() ||
        asset.mip_levels.size() > max_texture_mip_levels ||
        asset.mip_levels.size() > std::numeric_limits<std::uint32_t>::max() ||
        asset.virtual_tiles.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const std::uint32_t canonical_mips = expected_mip_count(asset.width, asset.height);
    if (canonical_mips == 0U || asset.mip_levels.size() != canonical_mips) return false;
    std::uint64_t canonical_tiles = 0U;
    if (!expected_virtual_tile_count(asset.width, asset.height, canonical_mips, canonical_tiles) ||
        asset.virtual_tiles.size() != canonical_tiles) {
        return false;
    }

    std::uint32_t width = asset.width;
    std::uint32_t height = asset.height;
    std::uint64_t payload_offset = 0U;
    std::uint32_t first_tile = 0U;
    std::size_t tile_cursor = 0U;
    for (std::uint32_t mip = 0U; mip < canonical_mips; ++mip) {
        std::uint64_t payload_size = 0U;
        if (!mip_payload_size(width, height, payload_size)) return false;
        const std::uint32_t columns = (width + virtual_texture_tile_extent - 1U) /
            virtual_texture_tile_extent;
        const std::uint32_t rows = (height + virtual_texture_tile_extent - 1U) /
            virtual_texture_tile_extent;
        const std::uint64_t mip_tile_count_64 = static_cast<std::uint64_t>(columns) * rows;
        if (mip_tile_count_64 > std::numeric_limits<std::uint32_t>::max() ||
            first_tile > std::numeric_limits<std::uint32_t>::max() - mip_tile_count_64) {
            return false;
        }
        const std::uint32_t mip_tile_count = static_cast<std::uint32_t>(mip_tile_count_64);
        const auto& descriptor = asset.mip_levels[mip];
        if (descriptor.width != width || descriptor.height != height ||
            descriptor.payload_offset != payload_offset || descriptor.payload_size != payload_size ||
            descriptor.first_tile != first_tile || descriptor.tile_count != mip_tile_count) {
            return false;
        }

        for (std::uint32_t tile_y = 0U; tile_y < rows; ++tile_y) {
            for (std::uint32_t tile_x = 0U; tile_x < columns; ++tile_x) {
                if (tile_cursor >= asset.virtual_tiles.size()) return false;
                const std::uint32_t pixel_x = tile_x * virtual_texture_tile_extent;
                const std::uint32_t pixel_y = tile_y * virtual_texture_tile_extent;
                const auto& tile = asset.virtual_tiles[tile_cursor++];
                if (tile.mip_level != mip || tile.tile_x != tile_x || tile.tile_y != tile_y ||
                    tile.width != std::min(virtual_texture_tile_extent, width - pixel_x) ||
                    tile.height != std::min(virtual_texture_tile_extent, height - pixel_y)) {
                    return false;
                }
            }
        }

        if (payload_offset > std::numeric_limits<std::uint64_t>::max() - payload_size) return false;
        payload_offset += payload_size;
        first_tile += mip_tile_count;
        width = next_mip_extent(width);
        height = next_mip_extent(height);
    }
    return tile_cursor == asset.virtual_tiles.size() && payload_offset == asset.rgba8.size();
}

void append_u8(std::vector<std::byte>& bytes, const std::uint8_t value) {
    bytes.push_back(static_cast<std::byte>(value));
}

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
    }
}

void append_u64(std::vector<std::byte>& bytes, const std::uint64_t value) {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
    }
}

class Reader final {
public:
    explicit Reader(const std::span<const std::byte> bytes) noexcept : bytes_{bytes} {}

    bool read_u8(std::uint8_t& value) noexcept {
        if (remaining() < 1U) return false;
        value = std::to_integer<std::uint8_t>(bytes_[position_++]);
        return true;
    }

    bool read_u32(std::uint32_t& value) noexcept {
        if (remaining() < sizeof(value)) return false;
        value = 0U;
        for (std::size_t index = 0U; index < sizeof(value); ++index) {
            value |= static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes_[position_ + index])
            ) << (index * 8U);
        }
        position_ += sizeof(value);
        return true;
    }

    bool read_u64(std::uint64_t& value) noexcept {
        if (remaining() < sizeof(value)) return false;
        value = 0U;
        for (std::size_t index = 0U; index < sizeof(value); ++index) {
            value |= static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(bytes_[position_ + index])
            ) << (index * 8U);
        }
        position_ += sizeof(value);
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

    [[nodiscard]] std::size_t position() const noexcept {
        return position_;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0U;
};

} // namespace

std::uint32_t texture_mip_count(const TextureAsset& asset) noexcept {
    if (asset.version == texture_asset_format_version_v1) return 1U;
    if (asset.version == texture_asset_format_version_v2 &&
        asset.mip_levels.size() <= std::numeric_limits<std::uint32_t>::max()) {
        return static_cast<std::uint32_t>(asset.mip_levels.size());
    }
    return 0U;
}

std::span<const std::byte> texture_mip_rgba8(
    const TextureAsset& asset,
    const std::uint32_t mip_level
) noexcept {
    if (asset.version == texture_asset_format_version_v1) {
        if (mip_level != 0U) return {};
        std::uint64_t expected = 0U;
        if (!mip_payload_size(asset.width, asset.height, expected) || expected != asset.rgba8.size()) {
            return {};
        }
        return asset.rgba8;
    }
    if (asset.version != texture_asset_format_version_v2 || mip_level >= asset.mip_levels.size()) {
        return {};
    }
    const auto& mip = asset.mip_levels[mip_level];
    if (mip.payload_offset > asset.rgba8.size() || mip.payload_size > asset.rgba8.size() - mip.payload_offset ||
        mip.payload_offset > std::numeric_limits<std::size_t>::max() ||
        mip.payload_size > std::numeric_limits<std::size_t>::max()) {
        return {};
    }
    return std::span<const std::byte>{asset.rgba8}.subspan(
        static_cast<std::size_t>(mip.payload_offset),
        static_cast<std::size_t>(mip.payload_size)
    );
}

TextureAssetEncodeResult encode_texture_asset(const TextureAsset& asset) {
    TextureAssetEncodeResult result;
    if (!valid_texture_id(asset.stable_id) || asset.source_fingerprint == 0U ||
        !valid_color_space(asset.color_space)) {
        result.diagnostics = "Texture asset contains invalid identity, source fingerprint, or color space.";
        return result;
    }

    if (asset.version == texture_asset_format_version_v1) {
        std::uint64_t expected = 0U;
        if (!mip_payload_size(asset.width, asset.height, expected) || asset.rgba8.size() != expected ||
            !asset.mip_levels.empty() || !asset.virtual_tiles.empty()) {
            result.diagnostics = "Texture v1 asset contains invalid dimensions, pixel data, or virtual metadata.";
            return result;
        }
        result.bytes.reserve(v1_header_size + asset.rgba8.size());
        result.bytes.insert(result.bytes.end(), texture_magic.begin(), texture_magic.end());
        append_u32(result.bytes, texture_asset_format_version_v1);
        for (const std::uint8_t value : asset.stable_id) append_u8(result.bytes, value);
        append_u64(result.bytes, asset.source_fingerprint);
        append_u64(result.bytes, fingerprint(asset.rgba8));
        append_u32(result.bytes, asset.width);
        append_u32(result.bytes, asset.height);
        append_u8(result.bytes, static_cast<std::uint8_t>(asset.color_space));
        result.bytes.insert(result.bytes.end(), asset.rgba8.begin(), asset.rgba8.end());
        return result;
    }

    if (!validate_v2_layout(asset)) {
        result.diagnostics = "Texture v2 asset contains non-canonical mip, tile, or pixel metadata.";
        return result;
    }

    std::vector<std::byte> tail;
    const std::size_t descriptor_bytes = asset.mip_levels.size() * mip_descriptor_size +
        asset.virtual_tiles.size() * tile_descriptor_size;
    tail.reserve(v2_header_size - common_prefix_size + descriptor_bytes + asset.rgba8.size());
    append_u32(tail, asset.width);
    append_u32(tail, asset.height);
    append_u8(tail, static_cast<std::uint8_t>(asset.color_space));
    append_u32(tail, static_cast<std::uint32_t>(asset.mip_levels.size()));
    append_u32(tail, static_cast<std::uint32_t>(asset.virtual_tiles.size()));
    for (const auto& mip : asset.mip_levels) {
        append_u32(tail, mip.width);
        append_u32(tail, mip.height);
        append_u64(tail, mip.payload_offset);
        append_u64(tail, mip.payload_size);
        append_u32(tail, mip.first_tile);
        append_u32(tail, mip.tile_count);
    }
    for (const auto& tile : asset.virtual_tiles) {
        append_u32(tail, tile.mip_level);
        append_u32(tail, tile.tile_x);
        append_u32(tail, tile.tile_y);
        append_u32(tail, tile.width);
        append_u32(tail, tile.height);
    }
    tail.insert(tail.end(), asset.rgba8.begin(), asset.rgba8.end());

    result.bytes.reserve(common_prefix_size + tail.size());
    result.bytes.insert(result.bytes.end(), texture_magic.begin(), texture_magic.end());
    append_u32(result.bytes, texture_asset_format_version_v2);
    for (const std::uint8_t value : asset.stable_id) append_u8(result.bytes, value);
    append_u64(result.bytes, asset.source_fingerprint);
    append_u64(result.bytes, fingerprint(tail));
    result.bytes.insert(result.bytes.end(), tail.begin(), tail.end());
    return result;
}

TextureAssetDecodeResult decode_texture_asset(const std::span<const std::byte> bytes) {
    TextureAssetDecodeResult result;
    if (bytes.size() < common_prefix_size ||
        !std::equal(texture_magic.begin(), texture_magic.end(), bytes.begin())) {
        result.diagnostics = "Texture asset header is invalid.";
        return result;
    }

    Reader reader{bytes.subspan(texture_asset_magic_size)};
    TextureAsset asset;
    std::uint64_t stored_payload_fingerprint = 0U;
    if (!reader.read_u32(asset.version)) {
        result.diagnostics = "Texture asset version is incomplete.";
        return result;
    }
    for (std::uint8_t& value : asset.stable_id) {
        if (!reader.read_u8(value)) {
            result.diagnostics = "Texture asset stable identity is incomplete.";
            return result;
        }
    }
    if (!reader.read_u64(asset.source_fingerprint) || !reader.read_u64(stored_payload_fingerprint)) {
        result.diagnostics = "Texture asset fingerprint metadata is incomplete.";
        return result;
    }
    if (!valid_texture_id(asset.stable_id) || asset.source_fingerprint == 0U) {
        result.diagnostics = "Texture asset identity or source fingerprint is invalid.";
        return result;
    }

    std::uint8_t color_space = 0U;
    if (asset.version == texture_asset_format_version_v1) {
        if (bytes.size() < v1_header_size || !reader.read_u32(asset.width) ||
            !reader.read_u32(asset.height) || !reader.read_u8(color_space)) {
            result.diagnostics = "Texture v1 metadata is incomplete.";
            return result;
        }
        asset.color_space = static_cast<TextureColorSpace>(color_space);
        std::uint64_t expected = 0U;
        if (!valid_color_space(asset.color_space) ||
            !mip_payload_size(asset.width, asset.height, expected) || reader.remaining() != expected) {
            result.diagnostics = "Texture v1 metadata or payload size is invalid.";
            return result;
        }
        const auto pixels = bytes.last(static_cast<std::size_t>(expected));
        if (fingerprint(pixels) != stored_payload_fingerprint) {
            result.diagnostics = "Texture v1 pixel fingerprint does not match.";
            return result;
        }
        asset.rgba8.assign(pixels.begin(), pixels.end());
        result.asset = std::move(asset);
        return result;
    }

    if (asset.version != texture_asset_format_version_v2 || bytes.size() < v2_header_size) {
        result.diagnostics = "Texture asset version is unsupported.";
        return result;
    }

    const std::size_t fingerprint_tail_offset = texture_asset_magic_size + reader.position();
    std::uint32_t mip_count = 0U;
    std::uint32_t tile_count = 0U;
    if (!reader.read_u32(asset.width) || !reader.read_u32(asset.height) ||
        !reader.read_u8(color_space) || !reader.read_u32(mip_count) || !reader.read_u32(tile_count)) {
        result.diagnostics = "Texture v2 metadata is incomplete.";
        return result;
    }
    asset.color_space = static_cast<TextureColorSpace>(color_space);
    const std::uint32_t canonical_mips = expected_mip_count(asset.width, asset.height);
    std::uint64_t canonical_tiles = 0U;
    if (!valid_color_space(asset.color_space) || canonical_mips == 0U || mip_count != canonical_mips ||
        !expected_virtual_tile_count(asset.width, asset.height, canonical_mips, canonical_tiles) ||
        tile_count != canonical_tiles) {
        result.diagnostics = "Texture v2 mip or tile counts are invalid.";
        return result;
    }

    const std::uint64_t descriptor_bytes = static_cast<std::uint64_t>(mip_count) * mip_descriptor_size +
        static_cast<std::uint64_t>(tile_count) * tile_descriptor_size;
    if (descriptor_bytes > reader.remaining()) {
        result.diagnostics = "Texture v2 descriptors are truncated.";
        return result;
    }
    asset.mip_levels.reserve(mip_count);
    for (std::uint32_t mip_index = 0U; mip_index < mip_count; ++mip_index) {
        TextureMipLevel mip;
        if (!reader.read_u32(mip.width) || !reader.read_u32(mip.height) ||
            !reader.read_u64(mip.payload_offset) || !reader.read_u64(mip.payload_size) ||
            !reader.read_u32(mip.first_tile) || !reader.read_u32(mip.tile_count)) {
            result.diagnostics = "Texture v2 mip descriptors are incomplete.";
            return result;
        }
        asset.mip_levels.push_back(mip);
    }
    asset.virtual_tiles.reserve(tile_count);
    for (std::uint32_t tile_index = 0U; tile_index < tile_count; ++tile_index) {
        VirtualTextureTile tile;
        if (!reader.read_u32(tile.mip_level) || !reader.read_u32(tile.tile_x) ||
            !reader.read_u32(tile.tile_y) || !reader.read_u32(tile.width) ||
            !reader.read_u32(tile.height)) {
            result.diagnostics = "Texture v2 tile descriptors are incomplete.";
            return result;
        }
        asset.virtual_tiles.push_back(tile);
    }

    std::uint64_t expected_payload = 0U;
    for (const auto& mip : asset.mip_levels) {
        if (mip.payload_size > std::numeric_limits<std::uint64_t>::max() - expected_payload) {
            result.diagnostics = "Texture v2 payload size overflowed.";
            return result;
        }
        expected_payload += mip.payload_size;
    }
    if (expected_payload > std::numeric_limits<std::size_t>::max() || reader.remaining() != expected_payload) {
        result.diagnostics = "Texture v2 pixel payload size is invalid.";
        return result;
    }
    const std::size_t payload_offset = bytes.size() - reader.remaining();
    const auto pixels = bytes.subspan(payload_offset, static_cast<std::size_t>(expected_payload));
    asset.rgba8.assign(pixels.begin(), pixels.end());

    if (fingerprint(bytes.subspan(fingerprint_tail_offset)) != stored_payload_fingerprint) {
        result.diagnostics = "Texture v2 content fingerprint does not match.";
        return result;
    }
    if (!validate_v2_layout(asset)) {
        result.diagnostics = "Texture v2 mip or tile layout is non-canonical.";
        return result;
    }

    result.asset = std::move(asset);
    return result;
}

TextureAssetDecodeResult load_texture_asset(const std::filesystem::path& path) {
    TextureAssetDecodeResult result;
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.diagnostics = "Could not open cooked texture asset: " + path.string();
        return result;
    }
    const std::vector<char> raw{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    if (!input.eof() && input.fail()) {
        result.diagnostics = "Could not read cooked texture asset: " + path.string();
        return result;
    }
    return decode_texture_asset(std::as_bytes(std::span<const char>{raw}));
}

} // namespace vice::render
