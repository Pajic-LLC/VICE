#include <vice/render/material_asset.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vice::render {
namespace {

constexpr std::array<std::byte, render_material_asset_magic_size> material_magic{
    std::byte{'V'}, std::byte{'I'}, std::byte{'C'}, std::byte{'E'},
    std::byte{'M'}, std::byte{'A'}, std::byte{'T'}, std::byte{'L'},
};
constexpr std::size_t header_size = render_material_asset_magic_size + sizeof(std::uint32_t) +
    render_material_stable_id_size + sizeof(std::uint64_t) * 2U;
constexpr std::uint32_t max_path_bytes = 4096U;
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

[[nodiscard]] bool finite_color(const Color color) noexcept {
    return std::isfinite(color.red) && std::isfinite(color.green) &&
        std::isfinite(color.blue) && std::isfinite(color.alpha);
}

[[nodiscard]] bool safe_relative_path(const std::string_view value) {
    if (value.empty() || value.size() > max_path_bytes || value.find('\\') != std::string_view::npos) {
        return false;
    }
    const std::filesystem::path path{value};
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& part : path) {
        if (part == ".." || part == ".") return false;
    }
    return path.extension() == ".json" && path.filename().string().ends_with(".vice.json");
}

[[nodiscard]] bool valid_filter(const Filter value) noexcept {
    return value == Filter::nearest || value == Filter::linear;
}

[[nodiscard]] bool valid_address(const AddressMode value) noexcept {
    return value == AddressMode::clamp_to_edge || value == AddressMode::repeat ||
        value == AddressMode::mirrored_repeat;
}

[[nodiscard]] bool valid_v2_surface(const RenderMaterialAsset& asset) noexcept {
    if (!std::isfinite(asset.metallic) || !std::isfinite(asset.roughness) ||
        asset.metallic < 0.0F || asset.metallic > 1.0F ||
        asset.roughness < 0.0F || asset.roughness > 1.0F ||
        !valid_texture_id(asset.base_color_texture) ||
        !valid_filter(asset.min_filter) || !valid_filter(asset.mag_filter) ||
        !valid_filter(asset.mip_filter) || !valid_address(asset.address_u) ||
        !valid_address(asset.address_v) || !valid_address(asset.address_w)) {
        return false;
    }
    SamplerDesc sampler;
    sampler.min_filter = asset.min_filter;
    sampler.mag_filter = asset.mag_filter;
    sampler.mip_filter = asset.mip_filter;
    sampler.address_u = asset.address_u;
    sampler.address_v = asset.address_v;
    sampler.address_w = asset.address_w;
    sampler.max_anisotropy = asset.max_anisotropy;
    return is_valid(sampler);
}

[[nodiscard]] bool valid_v3_surface(const RenderMaterialAsset& asset) noexcept {
    return valid_v2_surface(asset) && valid_texture_id(asset.normal_texture) &&
        valid_texture_id(asset.orm_texture) && valid_texture_id(asset.emissive_texture) &&
        finite_color(asset.emissive_color) && asset.emissive_color.red >= 0.0F &&
        asset.emissive_color.green >= 0.0F && asset.emissive_color.blue >= 0.0F &&
        std::isfinite(asset.emissive_intensity) && asset.emissive_intensity >= 0.0F &&
        std::isfinite(asset.normal_scale) && asset.normal_scale >= 0.0F;
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

void append_string(std::vector<std::byte>& bytes, const std::string& value) {
    append_u32(bytes, static_cast<std::uint32_t>(value.size()));
    for (const char character : value) {
        append_u8(bytes, static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
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

    bool read_f32(float& value) noexcept {
        std::uint32_t bits = 0U;
        if (!read_u32(bits)) return false;
        value = std::bit_cast<float>(bits);
        return true;
    }

    bool read_string(std::string& value) {
        std::uint32_t size = 0U;
        if (!read_u32(size) || size == 0U || size > max_path_bytes || remaining() < size) return false;
        value.clear();
        value.reserve(size);
        for (std::uint32_t index = 0U; index < size; ++index) {
            std::uint8_t byte = 0U;
            if (!read_u8(byte)) return false;
            value.push_back(static_cast<char>(byte));
        }
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - position_; }
    [[nodiscard]] bool finished() const noexcept { return position_ == bytes_.size(); }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0U;
};

} // namespace

RenderMaterialEncodeResult encode_render_material_asset(const RenderMaterialAsset& asset) {
    RenderMaterialEncodeResult result;
    if (asset.version < render_material_asset_min_supported_version ||
        asset.version > render_material_asset_format_version ||
        !valid_render_material_id(asset.stable_id) || asset.source_fingerprint == 0U ||
        !finite_color(asset.base_color) || !safe_relative_path(asset.vertex_shader_manifest) ||
        !safe_relative_path(asset.fragment_shader_manifest) ||
        (asset.version >= render_material_asset_format_version_v2 && !valid_v2_surface(asset)) ||
        (asset.version >= render_material_asset_format_version_v3 && !valid_v3_surface(asset))) {
        result.diagnostics = "Render material asset contains invalid identity, surface data, or shader paths.";
        return result;
    }

    std::vector<std::byte> payload;
    append_f32(payload, asset.base_color.red);
    append_f32(payload, asset.base_color.green);
    append_f32(payload, asset.base_color.blue);
    append_f32(payload, asset.base_color.alpha);
    if (asset.version >= render_material_asset_format_version_v2) {
        append_f32(payload, asset.metallic);
        append_f32(payload, asset.roughness);
        for (const std::uint8_t value : asset.base_color_texture) append_u8(payload, value);
        append_u8(payload, static_cast<std::uint8_t>(asset.min_filter));
        append_u8(payload, static_cast<std::uint8_t>(asset.mag_filter));
        append_u8(payload, static_cast<std::uint8_t>(asset.mip_filter));
        append_u8(payload, static_cast<std::uint8_t>(asset.address_u));
        append_u8(payload, static_cast<std::uint8_t>(asset.address_v));
        append_u8(payload, static_cast<std::uint8_t>(asset.address_w));
        append_u32(payload, asset.max_anisotropy);
    }
    if (asset.version >= render_material_asset_format_version_v3) {
        for (const std::uint8_t value : asset.normal_texture) append_u8(payload, value);
        for (const std::uint8_t value : asset.orm_texture) append_u8(payload, value);
        for (const std::uint8_t value : asset.emissive_texture) append_u8(payload, value);
        append_f32(payload, asset.normal_scale);
        append_f32(payload, asset.emissive_color.red);
        append_f32(payload, asset.emissive_color.green);
        append_f32(payload, asset.emissive_color.blue);
        append_f32(payload, asset.emissive_color.alpha);
        append_f32(payload, asset.emissive_intensity);
    }
    append_string(payload, asset.vertex_shader_manifest);
    append_string(payload, asset.fragment_shader_manifest);

    result.bytes.reserve(header_size + payload.size());
    result.bytes.insert(result.bytes.end(), material_magic.begin(), material_magic.end());
    append_u32(result.bytes, asset.version);
    for (const std::uint8_t value : asset.stable_id) append_u8(result.bytes, value);
    append_u64(result.bytes, asset.source_fingerprint);
    append_u64(result.bytes, fingerprint(payload));
    result.bytes.insert(result.bytes.end(), payload.begin(), payload.end());
    return result;
}

RenderMaterialDecodeResult decode_render_material_asset(const std::span<const std::byte> bytes) {
    RenderMaterialDecodeResult result;
    if (bytes.size() < header_size || !std::equal(material_magic.begin(), material_magic.end(), bytes.begin())) {
        result.diagnostics = "Render material asset header is invalid.";
        return result;
    }

    Reader header{bytes.subspan(render_material_asset_magic_size)};
    RenderMaterialAsset asset;
    std::uint64_t stored_payload_fingerprint = 0U;
    if (!header.read_u32(asset.version)) {
        result.diagnostics = "Render material asset version is incomplete.";
        return result;
    }
    for (std::uint8_t& value : asset.stable_id) {
        if (!header.read_u8(value)) {
            result.diagnostics = "Render material stable identity is incomplete.";
            return result;
        }
    }
    if (!header.read_u64(asset.source_fingerprint) || !header.read_u64(stored_payload_fingerprint)) {
        result.diagnostics = "Render material asset metadata is incomplete.";
        return result;
    }
    if (asset.version < render_material_asset_min_supported_version ||
        asset.version > render_material_asset_format_version ||
        !valid_render_material_id(asset.stable_id) || asset.source_fingerprint == 0U) {
        result.diagnostics = "Render material asset identity is invalid or unsupported.";
        return result;
    }

    const auto payload = bytes.subspan(header_size);
    if (fingerprint(payload) != stored_payload_fingerprint) {
        result.diagnostics = "Render material asset payload fingerprint does not match.";
        return result;
    }
    Reader reader{payload};
    if (!reader.read_f32(asset.base_color.red) || !reader.read_f32(asset.base_color.green) ||
        !reader.read_f32(asset.base_color.blue) || !reader.read_f32(asset.base_color.alpha)) {
        result.diagnostics = "Render material asset base color is incomplete.";
        return result;
    }

    if (asset.version >= render_material_asset_format_version_v2) {
        std::uint8_t min_filter = 0U;
        std::uint8_t mag_filter = 0U;
        std::uint8_t mip_filter = 0U;
        std::uint8_t address_u = 0U;
        std::uint8_t address_v = 0U;
        std::uint8_t address_w = 0U;
        if (!reader.read_f32(asset.metallic) || !reader.read_f32(asset.roughness)) {
            result.diagnostics = "Render material PBR parameters are incomplete.";
            return result;
        }
        for (std::uint8_t& value : asset.base_color_texture) {
            if (!reader.read_u8(value)) {
                result.diagnostics = "Render material base-color texture identity is incomplete.";
                return result;
            }
        }
        if (!reader.read_u8(min_filter) || !reader.read_u8(mag_filter) || !reader.read_u8(mip_filter) ||
            !reader.read_u8(address_u) || !reader.read_u8(address_v) || !reader.read_u8(address_w) ||
            !reader.read_u32(asset.max_anisotropy)) {
            result.diagnostics = "Render material sampler data is incomplete.";
            return result;
        }
        asset.min_filter = static_cast<Filter>(min_filter);
        asset.mag_filter = static_cast<Filter>(mag_filter);
        asset.mip_filter = static_cast<Filter>(mip_filter);
        asset.address_u = static_cast<AddressMode>(address_u);
        asset.address_v = static_cast<AddressMode>(address_v);
        asset.address_w = static_cast<AddressMode>(address_w);
    }

    if (asset.version >= render_material_asset_format_version_v3) {
        for (std::uint8_t& value : asset.normal_texture) {
            if (!reader.read_u8(value)) {
                result.diagnostics = "Render material normal texture identity is incomplete.";
                return result;
            }
        }
        for (std::uint8_t& value : asset.orm_texture) {
            if (!reader.read_u8(value)) {
                result.diagnostics = "Render material ORM texture identity is incomplete.";
                return result;
            }
        }
        for (std::uint8_t& value : asset.emissive_texture) {
            if (!reader.read_u8(value)) {
                result.diagnostics = "Render material emissive texture identity is incomplete.";
                return result;
            }
        }
        if (!reader.read_f32(asset.normal_scale) || !reader.read_f32(asset.emissive_color.red) ||
            !reader.read_f32(asset.emissive_color.green) || !reader.read_f32(asset.emissive_color.blue) ||
            !reader.read_f32(asset.emissive_color.alpha) || !reader.read_f32(asset.emissive_intensity)) {
            result.diagnostics = "Render material V3 surface parameters are incomplete.";
            return result;
        }
    }

    if (!reader.read_string(asset.vertex_shader_manifest) ||
        !reader.read_string(asset.fragment_shader_manifest) || !reader.finished()) {
        result.diagnostics = "Render material shader paths are incomplete or contain trailing data.";
        return result;
    }
    if (!finite_color(asset.base_color) || !safe_relative_path(asset.vertex_shader_manifest) ||
        !safe_relative_path(asset.fragment_shader_manifest) ||
        (asset.version >= render_material_asset_format_version_v2 && !valid_v2_surface(asset)) ||
        (asset.version >= render_material_asset_format_version_v3 && !valid_v3_surface(asset))) {
        result.diagnostics = "Render material asset payload contains invalid values.";
        return result;
    }

    result.asset = std::move(asset);
    return result;
}

RenderMaterialDecodeResult load_render_material_asset(const std::filesystem::path& path) {
    RenderMaterialDecodeResult result;
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.diagnostics = "Could not open cooked render material: " + path.string();
        return result;
    }
    const std::vector<char> raw{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    if (!input.eof() && input.fail()) {
        result.diagnostics = "Could not read cooked render material: " + path.string();
        return result;
    }
    return decode_render_material_asset(std::as_bytes(std::span<const char>{raw}));
}

} // namespace vice::render
