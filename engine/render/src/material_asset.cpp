#include <vice/render/material_asset.hpp>

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
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const auto& part : path) {
        if (part == ".." || part == ".") return false;
    }
    return path.extension() == ".json" && path.filename().string().ends_with(".vice.json");
}

void append_u8(std::vector<std::byte>& bytes, const std::uint8_t value) {
    bytes.push_back(static_cast<std::byte>(value));
}

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
    }
}

void append_u64(std::vector<std::byte>& bytes, const std::uint64_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
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
        for (std::size_t index = 0; index < sizeof(value); ++index) {
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
        for (std::size_t index = 0; index < sizeof(value); ++index) {
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
        if (!read_u32(size) || size == 0U || size > max_path_bytes || remaining() < size) {
            return false;
        }
        value.clear();
        value.reserve(size);
        for (std::uint32_t index = 0; index < size; ++index) {
            std::uint8_t byte = 0U;
            if (!read_u8(byte)) return false;
            value.push_back(static_cast<char>(byte));
        }
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

    [[nodiscard]] bool finished() const noexcept {
        return position_ == bytes_.size();
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0U;
};

} // namespace

RenderMaterialEncodeResult encode_render_material_asset(const RenderMaterialAsset& asset) {
    RenderMaterialEncodeResult result;
    if (asset.version != render_material_asset_format_version || !valid(asset.stable_id) ||
        asset.source_fingerprint == 0U || !finite_color(asset.base_color) ||
        !safe_relative_path(asset.vertex_shader_manifest) ||
        !safe_relative_path(asset.fragment_shader_manifest)) {
        result.diagnostics = "Render material asset contains invalid identity, color, or shader paths.";
        return result;
    }

    std::vector<std::byte> payload;
    append_f32(payload, asset.base_color.red);
    append_f32(payload, asset.base_color.green);
    append_f32(payload, asset.base_color.blue);
    append_f32(payload, asset.base_color.alpha);
    append_string(payload, asset.vertex_shader_manifest);
    append_string(payload, asset.fragment_shader_manifest);

    result.bytes.reserve(header_size + payload.size());
    result.bytes.insert(result.bytes.end(), material_magic.begin(), material_magic.end());
    append_u32(result.bytes, render_material_asset_format_version);
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
    if (asset.version != render_material_asset_format_version || !valid(asset.stable_id) ||
        asset.source_fingerprint == 0U) {
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
        !reader.read_f32(asset.base_color.blue) || !reader.read_f32(asset.base_color.alpha) ||
        !reader.read_string(asset.vertex_shader_manifest) ||
        !reader.read_string(asset.fragment_shader_manifest) || !reader.finished()) {
        result.diagnostics = "Render material asset payload is incomplete or contains trailing data.";
        return result;
    }
    if (!finite_color(asset.base_color) || !safe_relative_path(asset.vertex_shader_manifest) ||
        !safe_relative_path(asset.fragment_shader_manifest)) {
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
