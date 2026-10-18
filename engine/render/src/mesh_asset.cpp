#include <vice/render/mesh_asset.hpp>

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
#include <vector>

namespace vice::render {
namespace {

constexpr std::array<std::byte, mesh_asset_magic_size> mesh_magic{
    std::byte{'V'}, std::byte{'I'}, std::byte{'C'}, std::byte{'E'},
    std::byte{'M'}, std::byte{'E'}, std::byte{'S'}, std::byte{'H'},
};
constexpr std::size_t header_size_v1 = mesh_asset_magic_size + sizeof(std::uint32_t) * 3U +
    sizeof(std::uint64_t);
constexpr std::size_t header_size_v2 = header_size_v1 + mesh_stable_id_size;
constexpr std::size_t legacy_vertex_bytes = sizeof(float) * 6U;
constexpr std::size_t vertex_bytes_v3 = sizeof(MeshVertex);
constexpr std::uint32_t max_vertices = 4'000'000U;
constexpr std::uint32_t max_indices = 12'000'000U;
constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

[[nodiscard]] std::uint64_t fingerprint(std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const std::byte value : bytes) {
        hash ^= static_cast<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
}

class Reader final {
public:
    explicit Reader(std::span<const std::byte> bytes) noexcept : bytes_{bytes} {}

    bool read_u8(std::uint8_t& value) noexcept {
        if (remaining() < 1U) return false;
        value = std::to_integer<std::uint8_t>(bytes_[position_]);
        ++position_;
        return true;
    }

    bool read_u32(std::uint32_t& value) noexcept {
        if (remaining() < sizeof(value)) return false;
        value = 0U;
        for (std::size_t index = 0U; index < sizeof(value); ++index) {
            value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes_[position_ + index]))
                << (index * 8U);
        }
        position_ += sizeof(value);
        return true;
    }

    bool read_u64(std::uint64_t& value) noexcept {
        if (remaining() < sizeof(value)) return false;
        value = 0U;
        for (std::size_t index = 0U; index < sizeof(value); ++index) {
            value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes_[position_ + index]))
                << (index * 8U);
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

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0U;
};

[[nodiscard]] bool finite_vertex(const MeshVertex& vertex, const bool extended) noexcept {
    for (const float value : vertex.position) if (!std::isfinite(value)) return false;
    for (const float value : vertex.color) if (!std::isfinite(value)) return false;
    if (!extended) return true;
    for (const float value : vertex.normal) if (!std::isfinite(value)) return false;
    for (const float value : vertex.uv) if (!std::isfinite(value)) return false;
    const float normal_length_squared =
        vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] +
        vertex.normal[2] * vertex.normal[2];
    return normal_length_squared > 1.0e-8F;
}

} // namespace

MeshAssetDecodeResult decode_mesh_asset(const std::span<const std::byte> bytes) {
    MeshAssetDecodeResult result;
    if (bytes.size() < header_size_v1) {
        result.diagnostics = "Mesh asset is smaller than the VICEMESH header.";
        return result;
    }
    if (!std::equal(mesh_magic.begin(), mesh_magic.end(), bytes.begin())) {
        result.diagnostics = "Mesh asset magic is invalid.";
        return result;
    }

    Reader reader{bytes.subspan(mesh_asset_magic_size)};
    std::uint32_t version = 0U;
    std::uint32_t vertex_count = 0U;
    std::uint32_t index_count = 0U;
    std::uint64_t stored_fingerprint = 0U;
    if (!reader.read_u32(version) || !reader.read_u32(vertex_count) ||
        !reader.read_u32(index_count) || !reader.read_u64(stored_fingerprint)) {
        result.diagnostics = "Mesh asset header is incomplete.";
        return result;
    }
    if (version < mesh_asset_min_supported_version || version > mesh_asset_format_version) {
        result.diagnostics = "Mesh asset format version is not supported.";
        return result;
    }

    MeshStableId stable_id{};
    const std::size_t header_size = version >= mesh_asset_format_version_v2 ? header_size_v2 : header_size_v1;
    if (bytes.size() < header_size) {
        result.diagnostics = "Mesh asset header is incomplete.";
        return result;
    }
    if (version >= mesh_asset_format_version_v2) {
        for (std::uint8_t& value : stable_id) {
            if (!reader.read_u8(value)) {
                result.diagnostics = "Mesh asset stable identity is incomplete.";
                return result;
            }
        }
    }

    if (vertex_count < 3U || vertex_count > max_vertices || index_count < 3U ||
        index_count > max_indices || index_count % 3U != 0U) {
        result.diagnostics = "Mesh asset has invalid vertex or index counts.";
        return result;
    }

    const std::size_t stored_vertex_bytes = version >= mesh_asset_format_version_v3
        ? vertex_bytes_v3
        : legacy_vertex_bytes;
    const std::size_t expected_payload = static_cast<std::size_t>(vertex_count) * stored_vertex_bytes +
        static_cast<std::size_t>(index_count) * sizeof(std::uint32_t);
    if (bytes.size() != header_size + expected_payload) {
        result.diagnostics = "Mesh asset payload size does not match its header.";
        return result;
    }
    if (fingerprint(bytes.subspan(header_size)) != stored_fingerprint) {
        result.diagnostics = "Mesh asset payload fingerprint does not match.";
        return result;
    }

    MeshAsset asset;
    asset.version = version;
    asset.stable_id = stable_id;
    asset.source_fingerprint = stored_fingerprint;
    asset.vertices.resize(vertex_count);
    asset.indices.resize(index_count);

    const bool extended = version >= mesh_asset_format_version_v3;
    for (auto& vertex : asset.vertices) {
        for (float& component : vertex.position) {
            if (!reader.read_f32(component)) {
                result.diagnostics = "Mesh asset contains an incomplete vertex position.";
                return result;
            }
        }
        for (float& component : vertex.color) {
            if (!reader.read_f32(component)) {
                result.diagnostics = "Mesh asset contains an incomplete vertex color.";
                return result;
            }
        }
        if (extended) {
            for (float& component : vertex.normal) {
                if (!reader.read_f32(component)) {
                    result.diagnostics = "Mesh asset contains an incomplete vertex normal.";
                    return result;
                }
            }
            for (float& component : vertex.uv) {
                if (!reader.read_f32(component)) {
                    result.diagnostics = "Mesh asset contains incomplete UV coordinates.";
                    return result;
                }
            }
        }
        if (!finite_vertex(vertex, extended)) {
            result.diagnostics = "Mesh asset contains invalid vertex data.";
            return result;
        }
    }

    for (std::uint32_t& index : asset.indices) {
        if (!reader.read_u32(index) || index >= vertex_count) {
            result.diagnostics = "Mesh asset contains an out-of-range index.";
            return result;
        }
    }
    for (std::size_t index = 0U; index < asset.indices.size(); index += 3U) {
        const auto a = asset.indices[index];
        const auto b = asset.indices[index + 1U];
        const auto c = asset.indices[index + 2U];
        if (a == b || b == c || a == c) {
            result.diagnostics = "Mesh asset contains a degenerate indexed triangle.";
            return result;
        }
    }

    result.asset = std::move(asset);
    return result;
}

MeshAssetDecodeResult load_mesh_asset(const std::filesystem::path& path) {
    MeshAssetDecodeResult result;
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.diagnostics = "Could not open cooked mesh asset: " + path.string();
        return result;
    }
    const std::vector<char> raw{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    if (!input.eof() && input.fail()) {
        result.diagnostics = "Could not read cooked mesh asset: " + path.string();
        return result;
    }
    const auto bytes = std::as_bytes(std::span<const char>{raw});
    return decode_mesh_asset(bytes);
}

} // namespace vice::render
