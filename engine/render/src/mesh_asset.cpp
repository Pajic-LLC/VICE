#include <vice/render/mesh_asset.hpp>

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
constexpr std::size_t header_size = mesh_asset_magic_size + sizeof(std::uint32_t) * 3U +
    sizeof(std::uint64_t);
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

    bool read_u32(std::uint32_t& value) noexcept {
        if (remaining() < sizeof(value)) return false;
        value = 0;
        for (std::size_t index = 0; index < sizeof(value); ++index) {
            value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes_[position_ + index]))
                << (index * 8U);
        }
        position_ += sizeof(value);
        return true;
    }

    bool read_u64(std::uint64_t& value) noexcept {
        if (remaining() < sizeof(value)) return false;
        value = 0;
        for (std::size_t index = 0; index < sizeof(value); ++index) {
            value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes_[position_ + index]))
                << (index * 8U);
        }
        position_ += sizeof(value);
        return true;
    }

    bool read_f32(float& value) noexcept {
        std::uint32_t bits = 0;
        if (!read_u32(bits)) return false;
        value = std::bit_cast<float>(bits);
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

} // namespace

MeshAssetDecodeResult decode_mesh_asset(const std::span<const std::byte> bytes) {
    MeshAssetDecodeResult result;
    if (bytes.size() < header_size) {
        result.diagnostics = "Mesh asset is smaller than the VICEMESH header.";
        return result;
    }
    if (!std::equal(mesh_magic.begin(), mesh_magic.end(), bytes.begin())) {
        result.diagnostics = "Mesh asset magic is invalid.";
        return result;
    }

    Reader reader{bytes.subspan(mesh_asset_magic_size)};
    std::uint32_t version = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    std::uint64_t stored_fingerprint = 0;
    if (!reader.read_u32(version) || !reader.read_u32(vertex_count) ||
        !reader.read_u32(index_count) || !reader.read_u64(stored_fingerprint)) {
        result.diagnostics = "Mesh asset header is incomplete.";
        return result;
    }
    if (version != mesh_asset_format_version) {
        result.diagnostics = "Mesh asset format version is not supported.";
        return result;
    }
    if (vertex_count < 3U || vertex_count > max_vertices || index_count < 3U ||
        index_count > max_indices || index_count % 3U != 0U) {
        result.diagnostics = "Mesh asset has invalid vertex or index counts.";
        return result;
    }

    constexpr std::size_t vertex_bytes = sizeof(MeshVertex);
    const std::size_t expected_payload = static_cast<std::size_t>(vertex_count) * vertex_bytes +
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
    asset.source_fingerprint = stored_fingerprint;
    asset.vertices.resize(vertex_count);
    asset.indices.resize(index_count);

    for (auto& vertex : asset.vertices) {
        for (float& component : vertex.position) {
            if (!reader.read_f32(component) || !std::isfinite(component)) {
                result.diagnostics = "Mesh asset contains an invalid vertex position.";
                return result;
            }
        }
        for (float& component : vertex.color) {
            if (!reader.read_f32(component) || !std::isfinite(component)) {
                result.diagnostics = "Mesh asset contains an invalid vertex color.";
                return result;
            }
        }
    }

    for (std::uint32_t& index : asset.indices) {
        if (!reader.read_u32(index) || index >= vertex_count) {
            result.diagnostics = "Mesh asset contains an out-of-range index.";
            return result;
        }
    }
    for (std::size_t index = 0; index < asset.indices.size(); index += 3U) {
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
