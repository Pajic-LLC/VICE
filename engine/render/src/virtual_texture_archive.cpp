#include <vice/render/virtual_texture_archive.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace vice::render {
namespace {

constexpr std::array<std::byte, 8U> directory_magic{
    std::byte{'V'}, std::byte{'I'}, std::byte{'C'}, std::byte{'E'},
    std::byte{'V'}, std::byte{'T'}, std::byte{'D'}, std::byte{'R'},
};
constexpr std::array<std::byte, 8U> footer_magic{
    std::byte{'V'}, std::byte{'I'}, std::byte{'C'}, std::byte{'E'},
    std::byte{'V'}, std::byte{'T'}, std::byte{'F'}, std::byte{'T'},
};
constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
constexpr std::uint64_t directory_header_size = 8U + sizeof(std::uint32_t) +
    texture_stable_id_size + sizeof(std::uint64_t) + sizeof(std::uint32_t);
constexpr std::uint64_t directory_tile_size = sizeof(std::uint32_t) * 5U + sizeof(std::uint64_t) * 3U;
constexpr std::uint64_t footer_size = 8U + sizeof(std::uint64_t) * 3U;

[[nodiscard]] std::uint64_t fingerprint(const std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const std::byte value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
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

    bool read_bytes(const std::span<std::byte> destination) noexcept {
        if (remaining() < destination.size()) return false;
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(position_), destination.size(), destination.begin());
        position_ += destination.size();
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0U;
};

[[nodiscard]] bool tile_less(
    const VirtualTextureArchiveTile& left,
    const VirtualTextureArchiveTile& right
) noexcept {
    return std::tie(left.mip_level, left.tile_y, left.tile_x) <
        std::tie(right.mip_level, right.tile_y, right.tile_x);
}

[[nodiscard]] std::optional<std::vector<std::byte>> extract_tile(
    const TextureAsset& asset,
    const VirtualTextureTile& tile
) {
    if (tile.mip_level >= asset.mip_levels.size() || tile.width == 0U || tile.height == 0U ||
        tile.width > virtual_texture_tile_extent || tile.height > virtual_texture_tile_extent) {
        return std::nullopt;
    }
    const auto& mip = asset.mip_levels[tile.mip_level];
    const auto mip_bytes = texture_mip_rgba8(asset, tile.mip_level);
    const std::uint64_t origin_x = static_cast<std::uint64_t>(tile.tile_x) * virtual_texture_tile_extent;
    const std::uint64_t origin_y = static_cast<std::uint64_t>(tile.tile_y) * virtual_texture_tile_extent;
    if (origin_x >= mip.width || origin_y >= mip.height ||
        tile.width > mip.width - origin_x || tile.height > mip.height - origin_y) {
        return std::nullopt;
    }
    const std::uint64_t row_bytes = static_cast<std::uint64_t>(mip.width) * 4U;
    const std::uint64_t tile_row_bytes = static_cast<std::uint64_t>(tile.width) * 4U;
    const std::uint64_t tile_size = tile_row_bytes * tile.height;
    if (row_bytes > std::numeric_limits<std::size_t>::max() ||
        tile_row_bytes > std::numeric_limits<std::size_t>::max() ||
        tile_size > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    std::vector<std::byte> result(static_cast<std::size_t>(tile_size));
    for (std::uint32_t row = 0U; row < tile.height; ++row) {
        const std::uint64_t source_offset =
            (origin_y + row) * row_bytes + origin_x * 4U;
        const std::uint64_t destination_offset = static_cast<std::uint64_t>(row) * tile_row_bytes;
        if (source_offset > mip_bytes.size() || tile_row_bytes > mip_bytes.size() - source_offset) {
            return std::nullopt;
        }
        std::copy_n(
            mip_bytes.begin() + static_cast<std::ptrdiff_t>(source_offset),
            static_cast<std::size_t>(tile_row_bytes),
            result.begin() + static_cast<std::ptrdiff_t>(destination_offset)
        );
    }
    return result;
}

[[nodiscard]] bool canonical_directory(
    const VirtualTextureArchiveDirectory& directory,
    const std::uint64_t payload_end
) noexcept {
    if (!valid_texture_id(directory.stable_id) || directory.source_fingerprint == 0U ||
        directory.tiles.empty()) {
        return false;
    }
    std::uint64_t expected_offset = 0U;
    for (std::size_t index = 0U; index < directory.tiles.size(); ++index) {
        const auto& tile = directory.tiles[index];
        if (tile.width == 0U || tile.height == 0U || tile.width > virtual_texture_tile_extent ||
            tile.height > virtual_texture_tile_extent || tile.payload_fingerprint == 0U ||
            tile.payload_offset != expected_offset) {
            return false;
        }
        if (index > 0U && !tile_less(directory.tiles[index - 1U], tile)) return false;
        const std::uint64_t expected_size = static_cast<std::uint64_t>(tile.width) * tile.height * 4U;
        if (tile.payload_size != expected_size ||
            expected_offset > std::numeric_limits<std::uint64_t>::max() - expected_size) {
            return false;
        }
        expected_offset += expected_size;
    }
    return expected_offset == payload_end;
}

[[nodiscard]] bool read_exact(
    std::ifstream& input,
    const std::uint64_t offset,
    const std::span<std::byte> destination
) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        destination.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) return false;
    input.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
    return input.good() || (input.eof() && static_cast<std::size_t>(input.gcount()) == destination.size());
}

[[nodiscard]] std::optional<std::uint64_t> file_size(std::ifstream& input) {
    input.clear();
    input.seekg(0, std::ios::end);
    if (!input) return std::nullopt;
    const auto end = input.tellg();
    if (end < 0) return std::nullopt;
    return static_cast<std::uint64_t>(end);
}

} // namespace

VirtualTextureArchiveEncodeResult encode_virtual_texture_archive(const TextureAsset& asset) {
    VirtualTextureArchiveEncodeResult result;
    if (asset.version != texture_asset_format_version_v2 || !encode_texture_asset(asset) ||
        asset.virtual_tiles.empty() || asset.virtual_tiles.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Virtual texture archive requires a canonical TextureAsset v2 source.";
        return result;
    }

    VirtualTextureArchiveDirectory directory;
    directory.stable_id = asset.stable_id;
    directory.source_fingerprint = asset.source_fingerprint;
    directory.tiles.reserve(asset.virtual_tiles.size());

    std::vector<std::byte> payloads;
    for (const auto& source_tile : asset.virtual_tiles) {
        const auto tile_bytes = extract_tile(asset, source_tile);
        if (!tile_bytes.has_value() || tile_bytes->empty()) {
            result.diagnostics = "Virtual texture archive could not extract a canonical tile payload.";
            return result;
        }
        if (payloads.size() > std::numeric_limits<std::uint64_t>::max() - tile_bytes->size()) {
            result.diagnostics = "Virtual texture archive payload size overflowed.";
            return result;
        }
        VirtualTextureArchiveTile descriptor;
        descriptor.mip_level = source_tile.mip_level;
        descriptor.tile_x = source_tile.tile_x;
        descriptor.tile_y = source_tile.tile_y;
        descriptor.width = source_tile.width;
        descriptor.height = source_tile.height;
        descriptor.payload_offset = static_cast<std::uint64_t>(payloads.size());
        descriptor.payload_size = static_cast<std::uint64_t>(tile_bytes->size());
        descriptor.payload_fingerprint = fingerprint(*tile_bytes);
        directory.tiles.push_back(descriptor);
        payloads.insert(payloads.end(), tile_bytes->begin(), tile_bytes->end());
    }

    if (!canonical_directory(directory, static_cast<std::uint64_t>(payloads.size()))) {
        result.diagnostics = "Virtual texture archive directory is not canonical.";
        return result;
    }

    std::vector<std::byte> directory_bytes;
    const std::uint64_t descriptor_bytes =
        static_cast<std::uint64_t>(directory.tiles.size()) * directory_tile_size;
    if (descriptor_bytes > std::numeric_limits<std::size_t>::max() - directory_header_size) {
        result.diagnostics = "Virtual texture archive directory is too large.";
        return result;
    }
    directory_bytes.reserve(static_cast<std::size_t>(directory_header_size + descriptor_bytes));
    directory_bytes.insert(directory_bytes.end(), directory_magic.begin(), directory_magic.end());
    append_u32(directory_bytes, virtual_texture_archive_format_version);
    for (const std::uint8_t value : directory.stable_id) directory_bytes.push_back(static_cast<std::byte>(value));
    append_u64(directory_bytes, directory.source_fingerprint);
    append_u32(directory_bytes, static_cast<std::uint32_t>(directory.tiles.size()));
    for (const auto& tile : directory.tiles) {
        append_u32(directory_bytes, tile.mip_level);
        append_u32(directory_bytes, tile.tile_x);
        append_u32(directory_bytes, tile.tile_y);
        append_u32(directory_bytes, tile.width);
        append_u32(directory_bytes, tile.height);
        append_u64(directory_bytes, tile.payload_offset);
        append_u64(directory_bytes, tile.payload_size);
        append_u64(directory_bytes, tile.payload_fingerprint);
    }

    const std::uint64_t directory_offset = static_cast<std::uint64_t>(payloads.size());
    const std::uint64_t directory_size = static_cast<std::uint64_t>(directory_bytes.size());
    const std::uint64_t directory_fingerprint = fingerprint(directory_bytes);
    if (payloads.size() > std::numeric_limits<std::size_t>::max() - directory_bytes.size() -
        static_cast<std::size_t>(footer_size)) {
        result.diagnostics = "Virtual texture archive total size overflowed.";
        return result;
    }

    result.bytes = std::move(payloads);
    result.bytes.insert(result.bytes.end(), directory_bytes.begin(), directory_bytes.end());
    result.bytes.insert(result.bytes.end(), footer_magic.begin(), footer_magic.end());
    append_u64(result.bytes, directory_offset);
    append_u64(result.bytes, directory_size);
    append_u64(result.bytes, directory_fingerprint);
    return result;
}

VirtualTextureArchiveDirectoryResult load_virtual_texture_archive_directory(
    const std::filesystem::path& path
) {
    VirtualTextureArchiveDirectoryResult result;
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.diagnostics = "Could not open virtual texture tile archive: " + path.string();
        return result;
    }
    const auto size = file_size(input);
    if (!size.has_value() || *size < footer_size) {
        result.diagnostics = "Virtual texture tile archive footer is missing.";
        return result;
    }

    std::vector<std::byte> footer(static_cast<std::size_t>(footer_size));
    if (!read_exact(input, *size - footer_size, footer)) {
        result.diagnostics = "Could not read virtual texture tile archive footer.";
        return result;
    }
    if (!std::equal(footer_magic.begin(), footer_magic.end(), footer.begin())) {
        result.diagnostics = "Virtual texture tile archive footer magic is invalid.";
        return result;
    }
    Reader footer_reader{std::span<const std::byte>{footer}.subspan(footer_magic.size())};
    std::uint64_t directory_offset = 0U;
    std::uint64_t directory_size = 0U;
    std::uint64_t directory_fingerprint = 0U;
    if (!footer_reader.read_u64(directory_offset) || !footer_reader.read_u64(directory_size) ||
        !footer_reader.read_u64(directory_fingerprint) || footer_reader.remaining() != 0U ||
        directory_fingerprint == 0U || directory_offset > *size - footer_size ||
        directory_size != *size - footer_size - directory_offset ||
        directory_size < directory_header_size || directory_size > std::numeric_limits<std::size_t>::max()) {
        result.diagnostics = "Virtual texture tile archive footer range is invalid.";
        return result;
    }

    std::vector<std::byte> directory_bytes(static_cast<std::size_t>(directory_size));
    if (!read_exact(input, directory_offset, directory_bytes) ||
        fingerprint(directory_bytes) != directory_fingerprint ||
        !std::equal(directory_magic.begin(), directory_magic.end(), directory_bytes.begin())) {
        result.diagnostics = "Virtual texture tile archive directory fingerprint or magic is invalid.";
        return result;
    }

    Reader reader{std::span<const std::byte>{directory_bytes}.subspan(directory_magic.size())};
    std::uint32_t version = 0U;
    std::uint32_t tile_count = 0U;
    VirtualTextureArchiveDirectory directory;
    std::array<std::byte, texture_stable_id_size> stable_bytes{};
    if (!reader.read_u32(version) || !reader.read_bytes(stable_bytes) ||
        !reader.read_u64(directory.source_fingerprint) || !reader.read_u32(tile_count) ||
        version != virtual_texture_archive_format_version || tile_count == 0U) {
        result.diagnostics = "Virtual texture tile archive directory header is invalid.";
        return result;
    }
    for (std::size_t index = 0U; index < texture_stable_id_size; ++index) {
        directory.stable_id[index] = std::to_integer<std::uint8_t>(stable_bytes[index]);
    }
    const std::uint64_t expected_descriptor_bytes = static_cast<std::uint64_t>(tile_count) * directory_tile_size;
    if (expected_descriptor_bytes != reader.remaining()) {
        result.diagnostics = "Virtual texture tile archive directory size is invalid.";
        return result;
    }

    directory.tiles.resize(tile_count);
    for (auto& tile : directory.tiles) {
        if (!reader.read_u32(tile.mip_level) || !reader.read_u32(tile.tile_x) ||
            !reader.read_u32(tile.tile_y) || !reader.read_u32(tile.width) ||
            !reader.read_u32(tile.height) || !reader.read_u64(tile.payload_offset) ||
            !reader.read_u64(tile.payload_size) || !reader.read_u64(tile.payload_fingerprint)) {
            result.diagnostics = "Virtual texture tile archive directory is truncated.";
            return result;
        }
    }
    if (reader.remaining() != 0U || !canonical_directory(directory, directory_offset)) {
        result.diagnostics = "Virtual texture tile archive directory metadata is non-canonical.";
        return result;
    }
    result.directory = std::move(directory);
    return result;
}

VirtualTextureArchiveTileResult load_virtual_texture_archive_tile(
    const std::filesystem::path& path,
    const VirtualTextureArchiveDirectory& directory,
    const std::uint32_t mip_level,
    const std::uint32_t tile_x,
    const std::uint32_t tile_y
) {
    VirtualTextureArchiveTileResult result;
    const auto found = std::lower_bound(
        directory.tiles.begin(),
        directory.tiles.end(),
        VirtualTextureArchiveTile{.mip_level = mip_level, .tile_x = tile_x, .tile_y = tile_y},
        tile_less
    );
    if (found == directory.tiles.end() || found->mip_level != mip_level ||
        found->tile_x != tile_x || found->tile_y != tile_y) {
        result.diagnostics = "Virtual texture tile archive does not contain the requested tile.";
        return result;
    }
    if (found->payload_size == 0U || found->payload_size > std::numeric_limits<std::size_t>::max()) {
        result.diagnostics = "Virtual texture tile archive requested payload size is invalid.";
        return result;
    }

    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.diagnostics = "Could not open virtual texture tile archive: " + path.string();
        return result;
    }
    std::vector<std::byte> payload(static_cast<std::size_t>(found->payload_size));
    if (!read_exact(input, found->payload_offset, payload) || fingerprint(payload) != found->payload_fingerprint) {
        result.diagnostics = "Virtual texture tile payload fingerprint does not match.";
        return result;
    }
    result.tile = VirtualTextureArchiveTilePayload{
        .descriptor = *found,
        .rgba8 = std::move(payload),
    };
    return result;
}

bool virtual_texture_archive_matches_asset(
    const VirtualTextureArchiveDirectory& directory,
    const TextureAsset& asset
) noexcept {
    if (asset.version != texture_asset_format_version_v2 || directory.stable_id != asset.stable_id ||
        directory.source_fingerprint != asset.source_fingerprint ||
        directory.tiles.size() != asset.virtual_tiles.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < directory.tiles.size(); ++index) {
        const auto& archived = directory.tiles[index];
        const auto& canonical = asset.virtual_tiles[index];
        if (archived.mip_level != canonical.mip_level || archived.tile_x != canonical.tile_x ||
            archived.tile_y != canonical.tile_y || archived.width != canonical.width ||
            archived.height != canonical.height ||
            archived.payload_size != static_cast<std::uint64_t>(canonical.width) * canonical.height * 4U) {
            return false;
        }
    }
    return true;
}

} // namespace vice::render
