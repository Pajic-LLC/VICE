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
constexpr std::size_t header_size = texture_asset_magic_size + sizeof(std::uint32_t) +
    texture_stable_id_size + sizeof(std::uint64_t) * 2U + sizeof(std::uint32_t) * 2U + 1U;
constexpr std::uint32_t max_texture_dimension = 16'384U;
constexpr std::uint64_t max_texture_bytes = 512ULL * 1024ULL * 1024ULL;
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

[[nodiscard]] bool expected_payload_size(
    const std::uint32_t width,
    const std::uint32_t height,
    std::size_t& size
) noexcept {
    if (width == 0U || height == 0U || width > max_texture_dimension ||
        height > max_texture_dimension) {
        return false;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    if (pixels > max_texture_bytes / 4U) return false;
    const std::uint64_t bytes = pixels * 4U;
    if (bytes > std::numeric_limits<std::size_t>::max()) return false;
    size = static_cast<std::size_t>(bytes);
    return true;
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

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0U;
};

} // namespace

TextureAssetEncodeResult encode_texture_asset(const TextureAsset& asset) {
    TextureAssetEncodeResult result;
    std::size_t expected = 0U;
    if (asset.version != texture_asset_format_version || !valid_texture_id(asset.stable_id) ||
        asset.source_fingerprint == 0U || !valid_color_space(asset.color_space) ||
        !expected_payload_size(asset.width, asset.height, expected) || asset.rgba8.size() != expected) {
        result.diagnostics = "Texture asset contains invalid identity, dimensions, color space, or pixel data.";
        return result;
    }

    result.bytes.reserve(header_size + asset.rgba8.size());
    result.bytes.insert(result.bytes.end(), texture_magic.begin(), texture_magic.end());
    append_u32(result.bytes, texture_asset_format_version);
    for (const std::uint8_t value : asset.stable_id) append_u8(result.bytes, value);
    append_u64(result.bytes, asset.source_fingerprint);
    append_u64(result.bytes, fingerprint(asset.rgba8));
    append_u32(result.bytes, asset.width);
    append_u32(result.bytes, asset.height);
    append_u8(result.bytes, static_cast<std::uint8_t>(asset.color_space));
    result.bytes.insert(result.bytes.end(), asset.rgba8.begin(), asset.rgba8.end());
    return result;
}

TextureAssetDecodeResult decode_texture_asset(const std::span<const std::byte> bytes) {
    TextureAssetDecodeResult result;
    if (bytes.size() < header_size || !std::equal(texture_magic.begin(), texture_magic.end(), bytes.begin())) {
        result.diagnostics = "Texture asset header is invalid.";
        return result;
    }

    Reader reader{bytes.subspan(texture_asset_magic_size)};
    TextureAsset asset;
    std::uint64_t stored_payload_fingerprint = 0U;
    std::uint8_t color_space = 0U;
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
    if (!reader.read_u64(asset.source_fingerprint) ||
        !reader.read_u64(stored_payload_fingerprint) ||
        !reader.read_u32(asset.width) || !reader.read_u32(asset.height) ||
        !reader.read_u8(color_space)) {
        result.diagnostics = "Texture asset metadata is incomplete.";
        return result;
    }
    asset.color_space = static_cast<TextureColorSpace>(color_space);

    std::size_t expected = 0U;
    if (asset.version != texture_asset_format_version || !valid_texture_id(asset.stable_id) ||
        asset.source_fingerprint == 0U || !valid_color_space(asset.color_space) ||
        !expected_payload_size(asset.width, asset.height, expected) || reader.remaining() != expected) {
        result.diagnostics = "Texture asset metadata or payload size is invalid.";
        return result;
    }

    const auto pixels = bytes.last(expected);
    if (fingerprint(pixels) != stored_payload_fingerprint) {
        result.diagnostics = "Texture asset pixel fingerprint does not match.";
        return result;
    }
    asset.rgba8.assign(pixels.begin(), pixels.end());
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
