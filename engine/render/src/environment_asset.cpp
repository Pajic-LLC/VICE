#include <vice/render/environment_asset.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
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

constexpr std::array<std::byte, environment_asset_magic_size> environment_magic{
    std::byte{'V'}, std::byte{'I'}, std::byte{'C'}, std::byte{'E'},
    std::byte{'E'}, std::byte{'N'}, std::byte{'V'}, std::byte{'R'},
};
constexpr std::size_t header_size = environment_asset_magic_size + sizeof(std::uint32_t) +
    environment_stable_id_size + sizeof(std::uint64_t) * 2U + sizeof(float) * 2U +
    sizeof(std::uint32_t) * 6U;
constexpr std::uint32_t max_dimension = 16'384U;
constexpr std::uint32_t max_mip_levels = 16U;
constexpr std::uint64_t max_payload_bytes = 1024ULL * 1024ULL * 1024ULL;
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

[[nodiscard]] constexpr std::uint32_t mip_extent(
    std::uint32_t extent,
    std::uint32_t level
) noexcept {
    while (level-- > 0U && extent > 1U) extent /= 2U;
    return extent == 0U ? 1U : extent;
}

[[nodiscard]] bool image_bytes(
    const std::uint32_t width,
    const std::uint32_t height,
    std::uint64_t& bytes
) noexcept {
    if (width == 0U || height == 0U || width > max_dimension || height > max_dimension) return false;
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    if (pixels > max_payload_bytes / 8U) return false;
    bytes = pixels * 8U;
    return true;
}

[[nodiscard]] bool expected_sizes(
    const EnvironmentAsset& asset,
    std::uint64_t& irradiance,
    std::uint64_t& specular,
    std::uint64_t& brdf
) noexcept {
    if (!image_bytes(asset.irradiance_width, asset.irradiance_height, irradiance) ||
        !image_bytes(asset.specular_width, asset.specular_height, specular) ||
        asset.specular_mip_levels == 0U || asset.specular_mip_levels > max_mip_levels ||
        asset.brdf_lut_size == 0U || asset.brdf_lut_size > max_dimension) {
        return false;
    }

    specular = 0U;
    for (std::uint32_t level = 0U; level < asset.specular_mip_levels; ++level) {
        std::uint64_t level_bytes = 0U;
        if (!image_bytes(
                mip_extent(asset.specular_width, level),
                mip_extent(asset.specular_height, level),
                level_bytes
            ) || specular > max_payload_bytes - level_bytes) {
            return false;
        }
        specular += level_bytes;
    }
    if (!image_bytes(asset.brdf_lut_size, asset.brdf_lut_size, brdf)) return false;
    return irradiance <= max_payload_bytes && specular <= max_payload_bytes && brdf <= max_payload_bytes &&
        irradiance + specular <= max_payload_bytes && irradiance + specular + brdf <= max_payload_bytes;
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

void append_f32(std::vector<std::byte>& bytes, const float value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
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

    bool read_f32(float& value) noexcept {
        std::uint32_t bits = 0U;
        if (!read_u32(bits)) return false;
        value = std::bit_cast<float>(bits);
        return true;
    }

    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - position_; }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0U;
};

} // namespace

EnvironmentAssetEncodeResult encode_environment_asset(const EnvironmentAsset& asset) {
    EnvironmentAssetEncodeResult result;
    std::uint64_t irradiance_bytes = 0U;
    std::uint64_t specular_bytes = 0U;
    std::uint64_t brdf_bytes = 0U;
    if (asset.version != environment_asset_format_version || !valid_environment_id(asset.stable_id) ||
        asset.source_fingerprint == 0U || !std::isfinite(asset.intensity) || asset.intensity < 0.0F ||
        !std::isfinite(asset.rotation_radians) ||
        !expected_sizes(asset, irradiance_bytes, specular_bytes, brdf_bytes) ||
        asset.irradiance_rgba16f.size() != irradiance_bytes ||
        asset.specular_rgba16f.size() != specular_bytes ||
        asset.brdf_lut_rgba16f.size() != brdf_bytes) {
        result.diagnostics = "Environment asset contains invalid identity, dimensions, metadata, or lighting data.";
        return result;
    }

    std::vector<std::byte> payload;
    payload.reserve(static_cast<std::size_t>(irradiance_bytes + specular_bytes + brdf_bytes));
    payload.insert(payload.end(), asset.irradiance_rgba16f.begin(), asset.irradiance_rgba16f.end());
    payload.insert(payload.end(), asset.specular_rgba16f.begin(), asset.specular_rgba16f.end());
    payload.insert(payload.end(), asset.brdf_lut_rgba16f.begin(), asset.brdf_lut_rgba16f.end());

    result.bytes.reserve(header_size + payload.size());
    result.bytes.insert(result.bytes.end(), environment_magic.begin(), environment_magic.end());
    append_u32(result.bytes, asset.version);
    for (const std::uint8_t value : asset.stable_id) append_u8(result.bytes, value);
    append_u64(result.bytes, asset.source_fingerprint);
    append_u64(result.bytes, fingerprint(payload));
    append_f32(result.bytes, asset.intensity);
    append_f32(result.bytes, asset.rotation_radians);
    append_u32(result.bytes, asset.irradiance_width);
    append_u32(result.bytes, asset.irradiance_height);
    append_u32(result.bytes, asset.specular_width);
    append_u32(result.bytes, asset.specular_height);
    append_u32(result.bytes, asset.specular_mip_levels);
    append_u32(result.bytes, asset.brdf_lut_size);
    result.bytes.insert(result.bytes.end(), payload.begin(), payload.end());
    return result;
}

EnvironmentAssetDecodeResult decode_environment_asset(const std::span<const std::byte> bytes) {
    EnvironmentAssetDecodeResult result;
    if (bytes.size() < header_size ||
        !std::equal(environment_magic.begin(), environment_magic.end(), bytes.begin())) {
        result.diagnostics = "Environment asset header is invalid.";
        return result;
    }

    Reader reader{bytes.subspan(environment_asset_magic_size)};
    EnvironmentAsset asset;
    std::uint64_t stored_payload_fingerprint = 0U;
    if (!reader.read_u32(asset.version)) {
        result.diagnostics = "Environment asset version is incomplete.";
        return result;
    }
    for (std::uint8_t& value : asset.stable_id) {
        if (!reader.read_u8(value)) {
            result.diagnostics = "Environment asset stable identity is incomplete.";
            return result;
        }
    }
    if (!reader.read_u64(asset.source_fingerprint) ||
        !reader.read_u64(stored_payload_fingerprint) ||
        !reader.read_f32(asset.intensity) || !reader.read_f32(asset.rotation_radians) ||
        !reader.read_u32(asset.irradiance_width) || !reader.read_u32(asset.irradiance_height) ||
        !reader.read_u32(asset.specular_width) || !reader.read_u32(asset.specular_height) ||
        !reader.read_u32(asset.specular_mip_levels) || !reader.read_u32(asset.brdf_lut_size)) {
        result.diagnostics = "Environment asset metadata is incomplete.";
        return result;
    }

    std::uint64_t irradiance_bytes = 0U;
    std::uint64_t specular_bytes = 0U;
    std::uint64_t brdf_bytes = 0U;
    if (asset.version != environment_asset_format_version || !valid_environment_id(asset.stable_id) ||
        asset.source_fingerprint == 0U || !std::isfinite(asset.intensity) || asset.intensity < 0.0F ||
        !std::isfinite(asset.rotation_radians) ||
        !expected_sizes(asset, irradiance_bytes, specular_bytes, brdf_bytes)) {
        result.diagnostics = "Environment asset metadata is invalid or unsupported.";
        return result;
    }

    const std::uint64_t payload_size = irradiance_bytes + specular_bytes + brdf_bytes;
    if (payload_size > std::numeric_limits<std::size_t>::max() || reader.remaining() != payload_size) {
        result.diagnostics = "Environment asset payload size is invalid.";
        return result;
    }
    const auto payload = bytes.subspan(header_size);
    if (fingerprint(payload) != stored_payload_fingerprint) {
        result.diagnostics = "Environment asset payload fingerprint does not match.";
        return result;
    }

    std::size_t offset = 0U;
    const auto copy_part = [&](std::vector<std::byte>& destination, const std::uint64_t size) {
        const auto part = payload.subspan(offset, static_cast<std::size_t>(size));
        destination.assign(part.begin(), part.end());
        offset += static_cast<std::size_t>(size);
    };
    copy_part(asset.irradiance_rgba16f, irradiance_bytes);
    copy_part(asset.specular_rgba16f, specular_bytes);
    copy_part(asset.brdf_lut_rgba16f, brdf_bytes);
    result.asset = std::move(asset);
    return result;
}

EnvironmentAssetDecodeResult load_environment_asset(const std::filesystem::path& path) {
    EnvironmentAssetDecodeResult result;
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.diagnostics = "Could not open cooked environment asset: " + path.string();
        return result;
    }
    const std::vector<char> raw{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    if (!input.eof() && input.fail()) {
        result.diagnostics = "Could not read cooked environment asset: " + path.string();
        return result;
    }
    return decode_environment_asset(std::as_bytes(std::span<const char>{raw}));
}

} // namespace vice::render
