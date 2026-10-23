#include <vice/render/mesh_asset.hpp>

#include <vice/render/meshlet.hpp>

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

MeshAssetDecodeResult decode_mesh_asset_v1_v5(std::span<const std::byte> bytes);

namespace {

constexpr std::array<std::byte, mesh_asset_magic_size> mesh_magic{
    std::byte{'V'}, std::byte{'I'}, std::byte{'C'}, std::byte{'E'},
    std::byte{'M'}, std::byte{'E'}, std::byte{'S'}, std::byte{'H'},
};
constexpr std::size_t header_size_v5 = mesh_asset_magic_size + sizeof(std::uint32_t) * 7U +
    sizeof(std::uint64_t) + mesh_stable_id_size;
constexpr std::size_t header_size_v6 = header_size_v5 + sizeof(std::uint32_t) * 5U;
constexpr std::size_t vertex_bytes = sizeof(MeshVertex);
constexpr std::size_t meshlet_descriptor_bytes = sizeof(std::uint32_t) * 4U + sizeof(float) * 4U;
constexpr std::size_t page_descriptor_bytes = sizeof(std::uint32_t) * 6U + sizeof(float) * 4U;
constexpr std::size_t hierarchy_descriptor_bytes = sizeof(std::uint32_t) * 6U + sizeof(float) * 5U;
constexpr std::size_t proxy_descriptor_bytes = sizeof(std::uint32_t) * 6U + sizeof(float);
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

struct ProxyDescriptor {
    std::uint32_t node_index = virtual_geometry_invalid_node;
    std::uint32_t source_triangle_count = 0U;
    std::uint32_t vertex_offset = 0U;
    std::uint32_t vertex_count = 0U;
    std::uint32_t index_offset = 0U;
    std::uint32_t index_count = 0U;
    float direct_geometric_error = 0.0F;
};

[[nodiscard]] bool finite_vertex(const MeshVertex& vertex) noexcept {
    for (const float value : vertex.position) if (!std::isfinite(value)) return false;
    for (const float value : vertex.color) if (!std::isfinite(value)) return false;
    for (const float value : vertex.normal) if (!std::isfinite(value)) return false;
    for (const float value : vertex.uv) if (!std::isfinite(value)) return false;
    const float normal_length_squared =
        vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] +
        vertex.normal[2] * vertex.normal[2];
    return normal_length_squared > 1.0e-8F;
}

[[nodiscard]] bool close_float(const float lhs, const float rhs) noexcept {
    if (!std::isfinite(lhs) || !std::isfinite(rhs)) return false;
    const float scale = std::max({1.0F, std::abs(lhs), std::abs(rhs)});
    return std::abs(lhs - rhs) <= scale * 1.0e-5F;
}

[[nodiscard]] bool equivalent_bounds(
    const MeshletBounds& lhs,
    const MeshletBounds& rhs
) noexcept {
    return close_float(lhs.center[0], rhs.center[0]) &&
        close_float(lhs.center[1], rhs.center[1]) &&
        close_float(lhs.center[2], rhs.center[2]) && close_float(lhs.radius, rhs.radius);
}

[[nodiscard]] bool base_payload_size(
    const std::uint32_t vertex_count,
    const std::uint32_t index_count,
    const std::uint32_t meshlet_count,
    const std::uint32_t meshlet_vertex_index_count,
    const std::uint32_t meshlet_triangle_index_count,
    const std::uint32_t page_count,
    std::size_t& size
) noexcept {
    size = 0U;
    std::size_t section = 0U;
    if (!checked_multiply(vertex_count, vertex_bytes, size) ||
        !checked_multiply(index_count, sizeof(std::uint32_t), section) ||
        !checked_add(size, section, size) ||
        !checked_multiply(meshlet_count, meshlet_descriptor_bytes, section) ||
        !checked_add(size, section, size) ||
        !checked_multiply(meshlet_vertex_index_count, sizeof(std::uint32_t), section) ||
        !checked_add(size, section, size) ||
        !checked_add(size, meshlet_triangle_index_count, size) ||
        !checked_multiply(page_count, page_descriptor_bytes, section) ||
        !checked_add(size, section, size)) {
        return false;
    }
    return true;
}

[[nodiscard]] std::pair<std::size_t, std::size_t> canonical_hierarchy_counts(
    const std::uint32_t page_count
) noexcept {
    std::size_t level_count = page_count;
    std::size_t node_count = level_count;
    while (level_count > 1U) {
        level_count = (level_count + virtual_geometry_hierarchy_fanout - 1U) /
            virtual_geometry_hierarchy_fanout;
        node_count += level_count;
    }
    return {node_count, node_count - page_count};
}

[[nodiscard]] bool proxy_vertex_is_from_node(
    const MeshAsset& asset,
    const VirtualGeometryHierarchyNode& node,
    const MeshVertex& candidate
) {
    for (std::uint32_t page_local = 0U; page_local < node.page_count; ++page_local) {
        const auto& page = asset.virtual_geometry_pages.pages[node.page_offset + page_local];
        for (std::uint32_t meshlet_local = 0U; meshlet_local < page.meshlet_count; ++meshlet_local) {
            const auto& meshlet = asset.meshlet_geometry.meshlets[
                page.meshlet_offset + meshlet_local
            ];
            for (std::uint32_t local = 0U; local < meshlet.vertex_count; ++local) {
                const auto global_index = asset.meshlet_geometry.vertex_indices[
                    static_cast<std::size_t>(meshlet.vertex_offset) + local
                ];
                if (asset.vertices[global_index] == candidate) return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::uint64_t source_triangle_count(
    const MeshAsset& asset,
    const VirtualGeometryHierarchyNode& node
) noexcept {
    std::uint64_t count = 0U;
    for (std::uint32_t local = 0U; local < node.page_count; ++local) {
        count += asset.virtual_geometry_pages.pages[node.page_offset + local].triangle_index_count / 3U;
    }
    return count;
}

[[nodiscard]] bool validate_v6_virtual_geometry(
    MeshAsset& asset,
    std::string& diagnostics
) {
    auto& proxy_set = asset.virtual_geometry_proxies;
    const auto expected_build = build_virtual_geometry_hierarchy(asset.virtual_geometry_pages);
    if (!expected_build) {
        diagnostics = "Mesh asset v6 virtual geometry hierarchy could not be reconstructed from pages.";
        return false;
    }
    const auto& expected = *expected_build.hierarchy;
    auto& hierarchy = proxy_set.hierarchy;
    if (hierarchy.root_index != expected.root_index ||
        hierarchy.nodes.size() != expected.nodes.size()) {
        diagnostics = "Mesh asset v6 virtual geometry hierarchy shape is not canonical.";
        return false;
    }

    std::size_t internal_count = 0U;
    for (std::size_t index = 0U; index < hierarchy.nodes.size(); ++index) {
        const auto& node = hierarchy.nodes[index];
        const auto& canonical = expected.nodes[index];
        if (node.parent_index != canonical.parent_index ||
            node.first_child != canonical.first_child ||
            node.child_count != canonical.child_count ||
            node.page_offset != canonical.page_offset ||
            node.page_count != canonical.page_count || node.depth != canonical.depth ||
            !equivalent_bounds(node.bounds, canonical.bounds) ||
            !std::isfinite(node.geometric_error) || node.geometric_error < 0.0F) {
            diagnostics = "Mesh asset v6 contains non-canonical hierarchy metadata.";
            return false;
        }
        if (node.child_count == 0U) {
            if (node.geometric_error != 0.0F) {
                diagnostics = "Mesh asset v6 hierarchy leaves must have zero geometric error.";
                return false;
            }
            continue;
        }
        ++internal_count;
        for (std::uint32_t local = 0U; local < node.child_count; ++local) {
            if (hierarchy.nodes[node.first_child + local].geometric_error > node.geometric_error) {
                diagnostics = "Mesh asset v6 hierarchy geometric error is not monotonic.";
                return false;
            }
        }
    }

    if (proxy_set.proxies.size() != internal_count) {
        diagnostics = "Mesh asset v6 proxy count does not cover every internal hierarchy node.";
        return false;
    }

    std::size_t proxy_offset = 0U;
    for (std::size_t node_index = 0U; node_index < hierarchy.nodes.size(); ++node_index) {
        const auto& node = hierarchy.nodes[node_index];
        if (node.child_count == 0U) continue;
        if (proxy_offset >= proxy_set.proxies.size()) {
            diagnostics = "Mesh asset v6 proxy stream ended before the hierarchy.";
            return false;
        }
        const auto& proxy = proxy_set.proxies[proxy_offset++];
        const auto expected_triangles = source_triangle_count(asset, node);
        if (proxy.node_index != node_index || expected_triangles == 0U ||
            expected_triangles > std::numeric_limits<std::uint32_t>::max() ||
            proxy.source_triangle_count != expected_triangles || proxy.vertices.empty() ||
            proxy.indices.empty() || proxy.indices.size() % 3U != 0U ||
            proxy.indices.size() / 3U > proxy.source_triangle_count ||
            !std::isfinite(proxy.direct_geometric_error) || proxy.direct_geometric_error < 0.0F ||
            proxy.direct_geometric_error > node.geometric_error) {
            diagnostics = "Mesh asset v6 contains invalid proxy descriptor metadata.";
            return false;
        }
        if (proxy.indices.size() / 3U == proxy.source_triangle_count &&
            proxy.direct_geometric_error != 0.0F) {
            diagnostics = "Mesh asset v6 exact proxy fallback must have zero direct error.";
            return false;
        }

        std::vector<bool> seen(proxy.vertices.size(), false);
        std::uint32_t next_first_use = 0U;
        for (std::size_t index = 0U; index < proxy.indices.size(); ++index) {
            const auto local_index = proxy.indices[index];
            if (local_index >= proxy.vertices.size()) {
                diagnostics = "Mesh asset v6 proxy contains an out-of-range local index.";
                return false;
            }
            if (!seen[local_index]) {
                if (local_index != next_first_use) {
                    diagnostics = "Mesh asset v6 proxy vertex compaction is not canonical.";
                    return false;
                }
                seen[local_index] = true;
                ++next_first_use;
            }
        }
        if (next_first_use != proxy.vertices.size()) {
            diagnostics = "Mesh asset v6 proxy contains unused compact vertices.";
            return false;
        }
        for (std::size_t index = 0U; index < proxy.indices.size(); index += 3U) {
            const auto a = proxy.indices[index];
            const auto b = proxy.indices[index + 1U];
            const auto c = proxy.indices[index + 2U];
            if (a == b || b == c || a == c) {
                diagnostics = "Mesh asset v6 proxy contains a degenerate triangle.";
                return false;
            }
        }
        for (const auto& vertex : proxy.vertices) {
            if (!finite_vertex(vertex) || !proxy_vertex_is_from_node(asset, node, vertex)) {
                diagnostics = "Mesh asset v6 proxy vertex is invalid or not sourced from its node.";
                return false;
            }
        }
    }
    return proxy_offset == proxy_set.proxies.size();
}

[[nodiscard]] MeshAssetDecodeResult decode_v6(const std::span<const std::byte> bytes) {
    MeshAssetDecodeResult result;
    if (bytes.size() < header_size_v6) {
        result.diagnostics = "Mesh asset v6 header is incomplete.";
        return result;
    }

    Reader header{bytes.subspan(mesh_asset_magic_size)};
    std::uint32_t version = 0U;
    std::uint32_t vertex_count = 0U;
    std::uint32_t index_count = 0U;
    std::uint64_t stored_fingerprint = 0U;
    MeshStableId stable_id{};
    std::uint32_t meshlet_count = 0U;
    std::uint32_t meshlet_vertex_index_count = 0U;
    std::uint32_t meshlet_triangle_index_count = 0U;
    std::uint32_t page_count = 0U;
    std::uint32_t hierarchy_node_count = 0U;
    std::uint32_t hierarchy_root_index = virtual_geometry_invalid_node;
    std::uint32_t proxy_count = 0U;
    std::uint32_t proxy_vertex_count = 0U;
    std::uint32_t proxy_index_count = 0U;

    if (!header.read_u32(version) || !header.read_u32(vertex_count) ||
        !header.read_u32(index_count) || !header.read_u64(stored_fingerprint)) {
        result.diagnostics = "Mesh asset v6 base header is incomplete.";
        return result;
    }
    for (auto& value : stable_id) {
        if (!header.read_u8(value)) {
            result.diagnostics = "Mesh asset v6 stable identity is incomplete.";
            return result;
        }
    }
    if (!header.read_u32(meshlet_count) || !header.read_u32(meshlet_vertex_index_count) ||
        !header.read_u32(meshlet_triangle_index_count) || !header.read_u32(page_count) ||
        !header.read_u32(hierarchy_node_count) || !header.read_u32(hierarchy_root_index) ||
        !header.read_u32(proxy_count) || !header.read_u32(proxy_vertex_count) ||
        !header.read_u32(proxy_index_count)) {
        result.diagnostics = "Mesh asset v6 virtual geometry header is incomplete.";
        return result;
    }
    if (version != mesh_asset_format_version_v6 || page_count == 0U || hierarchy_root_index != 0U) {
        result.diagnostics = "Mesh asset v6 header contains invalid virtual geometry metadata.";
        return result;
    }

    const auto [canonical_nodes, canonical_proxies] = canonical_hierarchy_counts(page_count);
    if (hierarchy_node_count != canonical_nodes || proxy_count != canonical_proxies ||
        (proxy_count == 0U && (proxy_vertex_count != 0U || proxy_index_count != 0U)) ||
        (proxy_count != 0U && (proxy_vertex_count == 0U || proxy_index_count == 0U))) {
        result.diagnostics = "Mesh asset v6 hierarchy or proxy stream counts are not canonical.";
        return result;
    }

    std::size_t base_size = 0U;
    if (!base_payload_size(
            vertex_count,
            index_count,
            meshlet_count,
            meshlet_vertex_index_count,
            meshlet_triangle_index_count,
            page_count,
            base_size
        )) {
        result.diagnostics = "Mesh asset v6 base payload size overflows the host address space.";
        return result;
    }

    std::size_t expected_payload = base_size;
    std::size_t section = 0U;
    if (!checked_multiply(hierarchy_node_count, hierarchy_descriptor_bytes, section) ||
        !checked_add(expected_payload, section, expected_payload) ||
        !checked_multiply(proxy_count, proxy_descriptor_bytes, section) ||
        !checked_add(expected_payload, section, expected_payload) ||
        !checked_multiply(proxy_vertex_count, vertex_bytes, section) ||
        !checked_add(expected_payload, section, expected_payload) ||
        !checked_multiply(proxy_index_count, sizeof(std::uint32_t), section) ||
        !checked_add(expected_payload, section, expected_payload)) {
        result.diagnostics = "Mesh asset v6 proxy payload size overflows the host address space.";
        return result;
    }
    if (bytes.size() != header_size_v6 + expected_payload) {
        result.diagnostics = "Mesh asset v6 payload size does not match its header.";
        return result;
    }
    const auto payload = bytes.subspan(header_size_v6);
    if (fingerprint(payload) != stored_fingerprint) {
        result.diagnostics = "Mesh asset v6 payload fingerprint does not match.";
        return result;
    }

    const auto base_payload = payload.first(base_size);
    std::vector<std::byte> legacy;
    legacy.reserve(header_size_v5 + base_size);
    legacy.insert(legacy.end(), mesh_magic.begin(), mesh_magic.end());
    append_u32(legacy, mesh_asset_format_version_v5);
    append_u32(legacy, vertex_count);
    append_u32(legacy, index_count);
    append_u64(legacy, fingerprint(base_payload));
    for (const auto value : stable_id) append_u8(legacy, value);
    append_u32(legacy, meshlet_count);
    append_u32(legacy, meshlet_vertex_index_count);
    append_u32(legacy, meshlet_triangle_index_count);
    append_u32(legacy, page_count);
    legacy.insert(legacy.end(), base_payload.begin(), base_payload.end());

    auto base = decode_mesh_asset_v1_v5(legacy);
    if (!base.asset.has_value()) {
        result.diagnostics = "Mesh asset v6 base geometry is invalid: " + base.diagnostics;
        return result;
    }
    MeshAsset asset = std::move(*base.asset);
    asset.version = mesh_asset_format_version_v6;
    asset.source_fingerprint = stored_fingerprint;

    Reader extension{payload.subspan(base_size)};
    auto& hierarchy = asset.virtual_geometry_proxies.hierarchy;
    hierarchy.root_index = hierarchy_root_index;
    hierarchy.nodes.resize(hierarchy_node_count);
    for (auto& node : hierarchy.nodes) {
        if (!extension.read_u32(node.parent_index) || !extension.read_u32(node.first_child) ||
            !extension.read_u32(node.child_count) || !extension.read_u32(node.page_offset) ||
            !extension.read_u32(node.page_count) || !extension.read_u32(node.depth) ||
            !extension.read_f32(node.bounds.center[0]) ||
            !extension.read_f32(node.bounds.center[1]) ||
            !extension.read_f32(node.bounds.center[2]) ||
            !extension.read_f32(node.bounds.radius) ||
            !extension.read_f32(node.geometric_error)) {
            result.diagnostics = "Mesh asset v6 contains an incomplete hierarchy descriptor.";
            return result;
        }
    }

    std::vector<ProxyDescriptor> descriptors(proxy_count);
    std::uint64_t expected_vertex_offset = 0U;
    std::uint64_t expected_index_offset = 0U;
    for (auto& descriptor : descriptors) {
        if (!extension.read_u32(descriptor.node_index) ||
            !extension.read_u32(descriptor.source_triangle_count) ||
            !extension.read_u32(descriptor.vertex_offset) ||
            !extension.read_u32(descriptor.vertex_count) ||
            !extension.read_u32(descriptor.index_offset) ||
            !extension.read_u32(descriptor.index_count) ||
            !extension.read_f32(descriptor.direct_geometric_error)) {
            result.diagnostics = "Mesh asset v6 contains an incomplete proxy descriptor.";
            return result;
        }
        if (descriptor.vertex_count == 0U || descriptor.index_count == 0U ||
            descriptor.index_count % 3U != 0U ||
            descriptor.vertex_offset != expected_vertex_offset ||
            descriptor.index_offset != expected_index_offset) {
            result.diagnostics = "Mesh asset v6 proxy stream ranges are not canonical.";
            return result;
        }
        expected_vertex_offset += descriptor.vertex_count;
        expected_index_offset += descriptor.index_count;
        if (expected_vertex_offset > proxy_vertex_count || expected_index_offset > proxy_index_count) {
            result.diagnostics = "Mesh asset v6 proxy stream ranges exceed their backing payloads.";
            return result;
        }
    }
    if (expected_vertex_offset != proxy_vertex_count || expected_index_offset != proxy_index_count) {
        result.diagnostics = "Mesh asset v6 proxy descriptors do not exactly cover their streams.";
        return result;
    }

    std::vector<MeshVertex> proxy_vertices(proxy_vertex_count);
    for (auto& vertex : proxy_vertices) {
        for (float& value : vertex.position) if (!extension.read_f32(value)) {
            result.diagnostics = "Mesh asset v6 contains an incomplete proxy vertex position.";
            return result;
        }
        for (float& value : vertex.color) if (!extension.read_f32(value)) {
            result.diagnostics = "Mesh asset v6 contains an incomplete proxy vertex color.";
            return result;
        }
        for (float& value : vertex.normal) if (!extension.read_f32(value)) {
            result.diagnostics = "Mesh asset v6 contains an incomplete proxy vertex normal.";
            return result;
        }
        for (float& value : vertex.uv) if (!extension.read_f32(value)) {
            result.diagnostics = "Mesh asset v6 contains incomplete proxy UV coordinates.";
            return result;
        }
    }

    std::vector<std::uint32_t> proxy_indices(proxy_index_count);
    for (auto& index : proxy_indices) {
        if (!extension.read_u32(index)) {
            result.diagnostics = "Mesh asset v6 contains an incomplete proxy index stream.";
            return result;
        }
    }
    if (extension.remaining() != 0U) {
        result.diagnostics = "Mesh asset v6 contains trailing virtual geometry payload data.";
        return result;
    }

    auto& proxies = asset.virtual_geometry_proxies.proxies;
    proxies.reserve(proxy_count);
    for (const auto& descriptor : descriptors) {
        VirtualGeometryProxyMesh proxy;
        proxy.node_index = descriptor.node_index;
        proxy.source_triangle_count = descriptor.source_triangle_count;
        proxy.direct_geometric_error = descriptor.direct_geometric_error;
        proxy.vertices.assign(
            proxy_vertices.begin() + descriptor.vertex_offset,
            proxy_vertices.begin() + descriptor.vertex_offset + descriptor.vertex_count
        );
        proxy.indices.assign(
            proxy_indices.begin() + descriptor.index_offset,
            proxy_indices.begin() + descriptor.index_offset + descriptor.index_count
        );
        proxies.push_back(std::move(proxy));
    }

    if (!validate_v6_virtual_geometry(asset, result.diagnostics)) return result;
    result.asset = std::move(asset);
    return result;
}

} // namespace

MeshAssetDecodeResult decode_mesh_asset(const std::span<const std::byte> bytes) {
    if (bytes.size() < mesh_asset_magic_size + sizeof(std::uint32_t) ||
        !std::equal(mesh_magic.begin(), mesh_magic.end(), bytes.begin())) {
        return decode_mesh_asset_v1_v5(bytes);
    }
    Reader reader{bytes.subspan(mesh_asset_magic_size)};
    std::uint32_t version = 0U;
    if (!reader.read_u32(version) || version <= mesh_asset_format_version_v5) {
        return decode_mesh_asset_v1_v5(bytes);
    }
    if (version != mesh_asset_format_version_v6) {
        MeshAssetDecodeResult result;
        result.diagnostics = "Mesh asset format version is not supported.";
        return result;
    }
    return decode_v6(bytes);
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
    return decode_mesh_asset(std::as_bytes(std::span<const char>{raw}));
}

} // namespace vice::render
