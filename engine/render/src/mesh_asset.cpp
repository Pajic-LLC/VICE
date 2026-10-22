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
#include <utility>
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
constexpr std::size_t header_size_v4 = header_size_v2 + sizeof(std::uint32_t) * 3U;
constexpr std::size_t legacy_vertex_bytes = sizeof(float) * 6U;
constexpr std::size_t vertex_bytes_v3 = sizeof(MeshVertex);
constexpr std::size_t meshlet_descriptor_bytes = sizeof(std::uint32_t) * 4U + sizeof(float) * 4U;
constexpr std::uint32_t max_vertices = 4'000'000U;
constexpr std::uint32_t max_indices = 12'000'000U;
constexpr std::uint32_t max_meshlets = max_indices / 3U;
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

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result
) noexcept {
    if (left > std::numeric_limits<std::size_t>::max() - right) return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result
) noexcept {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) return false;
    result = left * right;
    return true;
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

[[nodiscard]] bool finite_bounds(const MeshletBounds& bounds) noexcept {
    return std::isfinite(bounds.center[0]) &&
        std::isfinite(bounds.center[1]) &&
        std::isfinite(bounds.center[2]) &&
        std::isfinite(bounds.radius) && bounds.radius >= 0.0F;
}

[[nodiscard]] bool bound_contains_vertex(
    const MeshletBounds& bounds,
    const MeshVertex& vertex
) noexcept {
    const double dx = static_cast<double>(vertex.position[0]) - bounds.center[0];
    const double dy = static_cast<double>(vertex.position[1]) - bounds.center[1];
    const double dz = static_cast<double>(vertex.position[2]) - bounds.center[2];
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double tolerance = std::max(
        1.0e-4,
        static_cast<double>(bounds.radius) * 1.0e-5
    );
    return std::isfinite(distance) &&
        distance <= static_cast<double>(bounds.radius) + tolerance;
}

[[nodiscard]] bool validate_meshlets(MeshAsset& asset, std::string& diagnostics) {
    const auto& geometry = asset.meshlet_geometry;
    if (geometry.meshlets.empty() || geometry.vertex_indices.empty() ||
        geometry.triangle_indices.empty()) {
        diagnostics = "Mesh asset v4 cluster payload is empty.";
        return false;
    }
    if (geometry.triangle_indices.size() != asset.indices.size()) {
        diagnostics = "Mesh asset v4 cluster triangle stream does not match the mesh index count.";
        return false;
    }

    std::size_t expected_vertex_offset = 0U;
    std::size_t expected_triangle_offset = 0U;
    std::size_t reconstructed_offset = 0U;
    for (const auto& meshlet : geometry.meshlets) {
        if (meshlet.vertex_count == 0U || meshlet.vertex_count > meshlet_max_vertices ||
            meshlet.triangle_count == 0U || meshlet.triangle_count > meshlet_max_triangles) {
            diagnostics = "Mesh asset v4 contains an invalid cluster vertex or triangle count.";
            return false;
        }
        if (meshlet.vertex_offset != expected_vertex_offset ||
            meshlet.triangle_offset != expected_triangle_offset) {
            diagnostics = "Mesh asset v4 cluster offsets are not canonical and contiguous.";
            return false;
        }
        if (!finite_bounds(meshlet.bounds)) {
            diagnostics = "Mesh asset v4 contains invalid cluster bounds.";
            return false;
        }

        const std::size_t vertex_end = expected_vertex_offset + meshlet.vertex_count;
        const std::size_t triangle_bytes = static_cast<std::size_t>(meshlet.triangle_count) * 3U;
        const std::size_t triangle_end = expected_triangle_offset + triangle_bytes;
        if (vertex_end > geometry.vertex_indices.size() ||
            triangle_end > geometry.triangle_indices.size()) {
            diagnostics = "Mesh asset v4 cluster ranges exceed their metadata streams.";
            return false;
        }

        std::array<bool, meshlet_max_vertices> used_vertices{};
        for (std::uint32_t triangle = 0U; triangle < meshlet.triangle_count; ++triangle) {
            for (std::uint32_t corner = 0U; corner < 3U; ++corner) {
                const std::size_t local_triangle_offset = expected_triangle_offset +
                    static_cast<std::size_t>(triangle) * 3U + corner;
                const std::uint8_t local_index = geometry.triangle_indices[local_triangle_offset];
                if (local_index >= meshlet.vertex_count) {
                    diagnostics = "Mesh asset v4 cluster contains an out-of-range local vertex index.";
                    return false;
                }
                used_vertices[local_index] = true;
                const std::uint32_t global_index = geometry.vertex_indices[
                    expected_vertex_offset + local_index
                ];
                if (global_index >= asset.vertices.size()) {
                    diagnostics = "Mesh asset v4 cluster remap contains an out-of-range global vertex index.";
                    return false;
                }
                if (reconstructed_offset >= asset.indices.size() ||
                    global_index != asset.indices[reconstructed_offset]) {
                    diagnostics = "Mesh asset v4 cluster geometry does not reconstruct the authoritative index stream.";
                    return false;
                }
                ++reconstructed_offset;
            }
        }

        for (std::uint32_t local_index = 0U; local_index < meshlet.vertex_count; ++local_index) {
            if (!used_vertices[local_index]) {
                diagnostics = "Mesh asset v4 cluster contains an unused vertex remap entry.";
                return false;
            }
            const std::uint32_t global_index = geometry.vertex_indices[
                expected_vertex_offset + local_index
            ];
            if (!bound_contains_vertex(meshlet.bounds, asset.vertices[global_index])) {
                diagnostics = "Mesh asset v4 cluster bounds do not conservatively contain referenced vertices.";
                return false;
            }
        }

        expected_vertex_offset = vertex_end;
        expected_triangle_offset = triangle_end;
    }

    if (expected_vertex_offset != geometry.vertex_indices.size() ||
        expected_triangle_offset != geometry.triangle_indices.size() ||
        reconstructed_offset != asset.indices.size()) {
        diagnostics = "Mesh asset v4 cluster streams contain trailing or missing geometry.";
        return false;
    }
    return true;
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
    if (version >= mesh_asset_format_version_v2) {
        for (std::uint8_t& value : stable_id) {
            if (!reader.read_u8(value)) {
                result.diagnostics = "Mesh asset stable identity is incomplete.";
                return result;
            }
        }
    }

    std::uint32_t meshlet_count = 0U;
    std::uint32_t meshlet_vertex_index_count = 0U;
    std::uint32_t meshlet_triangle_index_count = 0U;
    if (version >= mesh_asset_format_version_v4 &&
        (!reader.read_u32(meshlet_count) ||
         !reader.read_u32(meshlet_vertex_index_count) ||
         !reader.read_u32(meshlet_triangle_index_count))) {
        result.diagnostics = "Mesh asset v4 cluster header is incomplete.";
        return result;
    }

    const std::size_t header_size = version >= mesh_asset_format_version_v4
        ? header_size_v4
        : (version >= mesh_asset_format_version_v2 ? header_size_v2 : header_size_v1);
    if (bytes.size() < header_size) {
        result.diagnostics = "Mesh asset header is incomplete.";
        return result;
    }

    if (vertex_count < 3U || vertex_count > max_vertices || index_count < 3U ||
        index_count > max_indices || index_count % 3U != 0U) {
        result.diagnostics = "Mesh asset has invalid vertex or index counts.";
        return result;
    }
    if (version >= mesh_asset_format_version_v4 &&
        (meshlet_count == 0U || meshlet_count > max_meshlets ||
         meshlet_vertex_index_count < 3U || meshlet_vertex_index_count > index_count ||
         meshlet_triangle_index_count != index_count)) {
        result.diagnostics = "Mesh asset v4 has invalid cluster stream counts.";
        return result;
    }

    const std::size_t stored_vertex_bytes = version >= mesh_asset_format_version_v3
        ? vertex_bytes_v3
        : legacy_vertex_bytes;
    std::size_t expected_payload = 0U;
    std::size_t section_bytes = 0U;
    if (!checked_multiply(vertex_count, stored_vertex_bytes, expected_payload) ||
        !checked_multiply(index_count, sizeof(std::uint32_t), section_bytes) ||
        !checked_add(expected_payload, section_bytes, expected_payload)) {
        result.diagnostics = "Mesh asset payload size overflows the host address space.";
        return result;
    }
    if (version >= mesh_asset_format_version_v4) {
        if (!checked_multiply(meshlet_count, meshlet_descriptor_bytes, section_bytes) ||
            !checked_add(expected_payload, section_bytes, expected_payload) ||
            !checked_multiply(meshlet_vertex_index_count, sizeof(std::uint32_t), section_bytes) ||
            !checked_add(expected_payload, section_bytes, expected_payload) ||
            !checked_add(expected_payload, meshlet_triangle_index_count, expected_payload)) {
            result.diagnostics = "Mesh asset v4 cluster payload size overflows the host address space.";
            return result;
        }
    }
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

    if (version >= mesh_asset_format_version_v4) {
        auto& geometry = asset.meshlet_geometry;
        geometry.meshlets.resize(meshlet_count);
        geometry.vertex_indices.resize(meshlet_vertex_index_count);
        geometry.triangle_indices.resize(meshlet_triangle_index_count);

        for (auto& meshlet : geometry.meshlets) {
            if (!reader.read_u32(meshlet.vertex_offset) ||
                !reader.read_u32(meshlet.vertex_count) ||
                !reader.read_u32(meshlet.triangle_offset) ||
                !reader.read_u32(meshlet.triangle_count) ||
                !reader.read_f32(meshlet.bounds.center[0]) ||
                !reader.read_f32(meshlet.bounds.center[1]) ||
                !reader.read_f32(meshlet.bounds.center[2]) ||
                !reader.read_f32(meshlet.bounds.radius)) {
                result.diagnostics = "Mesh asset v4 contains an incomplete cluster descriptor.";
                return result;
            }
        }
        for (auto& global_index : geometry.vertex_indices) {
            if (!reader.read_u32(global_index)) {
                result.diagnostics = "Mesh asset v4 contains an incomplete cluster vertex remap.";
                return result;
            }
        }
        for (auto& local_index : geometry.triangle_indices) {
            if (!reader.read_u8(local_index)) {
                result.diagnostics = "Mesh asset v4 contains an incomplete cluster triangle stream.";
                return result;
            }
        }
        if (reader.remaining() != 0U) {
            result.diagnostics = "Mesh asset v4 contains trailing cluster payload data.";
            return result;
        }
        if (!validate_meshlets(asset, result.diagnostics)) return result;
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
