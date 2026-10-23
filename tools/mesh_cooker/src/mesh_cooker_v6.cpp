#include <vice/tools/mesh_cooker.hpp>

#include <vice/render/meshlet.hpp>
#include <vice/render/virtual_geometry_proxy.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vice::tools::mesh_cooker {
namespace {

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

[[nodiscard]] bool finite_surface_vertex(const render::MeshVertex& vertex) noexcept {
    for (const float component : vertex.position) {
        if (!std::isfinite(component)) return false;
    }
    for (const float component : vertex.color) {
        if (!std::isfinite(component) || component < 0.0F || component > 1.0F) return false;
    }
    for (const float component : vertex.normal) {
        if (!std::isfinite(component)) return false;
    }
    for (const float component : vertex.uv) {
        if (!std::isfinite(component)) return false;
    }
    const float normal_length_squared =
        vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] +
        vertex.normal[2] * vertex.normal[2];
    return normal_length_squared > 1.0e-8F;
}

[[nodiscard]] bool checked_add_u32(
    const std::size_t amount,
    std::uint64_t& total
) noexcept {
    total += amount;
    return total <= std::numeric_limits<std::uint32_t>::max();
}

} // namespace

CookResult cook(const MeshSource& source) {
    CookResult result;
    if (source.name.empty() || source.vertices.size() < 3U || source.indices.size() < 3U ||
        source.indices.size() % 3U != 0U) {
        result.diagnostics = "Mesh source is incomplete.";
        return result;
    }
    if (source.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        source.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Mesh source exceeds the 32-bit cooked geometry contract.";
        return result;
    }
    for (const auto& vertex : source.vertices) {
        if (!finite_surface_vertex(vertex)) {
            result.diagnostics = "Mesh source contains an invalid surface vertex.";
            return result;
        }
    }
    for (std::size_t offset = 0U; offset < source.indices.size(); offset += 3U) {
        const auto a = source.indices[offset];
        const auto b = source.indices[offset + 1U];
        const auto c = source.indices[offset + 2U];
        if (a >= source.vertices.size() || b >= source.vertices.size() ||
            c >= source.vertices.size() || a == b || b == c || a == c) {
            result.diagnostics = "Mesh source contains invalid triangle indices.";
            return result;
        }
    }

    const auto meshlet_build = render::build_meshlets(source.vertices, source.indices);
    if (!meshlet_build) {
        result.diagnostics = "Mesh source could not be partitioned into deterministic clusters: " +
            meshlet_build.diagnostics;
        return result;
    }
    const auto& meshlets = *meshlet_build.geometry;
    if (meshlets.meshlets.size() > std::numeric_limits<std::uint32_t>::max() ||
        meshlets.vertex_indices.size() > std::numeric_limits<std::uint32_t>::max() ||
        meshlets.triangle_indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Meshlet metadata exceeds the 32-bit cooked mesh contract.";
        return result;
    }

    const auto page_build = render::build_virtual_geometry_pages(meshlets);
    if (!page_build) {
        result.diagnostics = "Mesh source could not be partitioned into deterministic virtual geometry pages: " +
            page_build.diagnostics;
        return result;
    }
    const auto& pages = *page_build.page_table;
    if (pages.pages.empty() || pages.pages.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Virtual geometry page metadata exceeds the 32-bit cooked mesh contract.";
        return result;
    }

    const auto hierarchy_build = render::build_virtual_geometry_hierarchy(pages);
    if (!hierarchy_build) {
        result.diagnostics = "Mesh source could not build a deterministic virtual geometry hierarchy: " +
            hierarchy_build.diagnostics;
        return result;
    }
    const auto proxy_build = render::build_virtual_geometry_proxies(
        source.vertices,
        meshlets,
        pages,
        *hierarchy_build.hierarchy
    );
    if (!proxy_build) {
        result.diagnostics = "Mesh source could not build deterministic virtual geometry proxies: " +
            proxy_build.diagnostics;
        return result;
    }
    const auto& proxy_set = *proxy_build.proxy_set;
    if (proxy_set.hierarchy.nodes.empty() ||
        proxy_set.hierarchy.nodes.size() > std::numeric_limits<std::uint32_t>::max() ||
        proxy_set.proxies.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Virtual geometry hierarchy exceeds the 32-bit cooked mesh contract.";
        return result;
    }

    std::uint64_t proxy_vertex_count = 0U;
    std::uint64_t proxy_index_count = 0U;
    for (const auto& proxy : proxy_set.proxies) {
        if (proxy.vertices.empty() || proxy.indices.empty() || proxy.indices.size() % 3U != 0U ||
            !checked_add_u32(proxy.vertices.size(), proxy_vertex_count) ||
            !checked_add_u32(proxy.indices.size(), proxy_index_count)) {
            result.diagnostics = "Virtual geometry proxy streams exceed the 32-bit cooked mesh contract.";
            return result;
        }
    }

    constexpr std::size_t meshlet_descriptor_bytes = sizeof(std::uint32_t) * 4U + sizeof(float) * 4U;
    constexpr std::size_t page_descriptor_bytes = sizeof(std::uint32_t) * 6U + sizeof(float) * 4U;
    constexpr std::size_t hierarchy_descriptor_bytes = sizeof(std::uint32_t) * 6U + sizeof(float) * 5U;
    constexpr std::size_t proxy_descriptor_bytes = sizeof(std::uint32_t) * 6U + sizeof(float);
    std::vector<std::byte> payload;
    payload.reserve(
        source.vertices.size() * sizeof(render::MeshVertex) +
        source.indices.size() * sizeof(std::uint32_t) +
        meshlets.meshlets.size() * meshlet_descriptor_bytes +
        meshlets.vertex_indices.size() * sizeof(std::uint32_t) +
        meshlets.triangle_indices.size() +
        pages.pages.size() * page_descriptor_bytes +
        proxy_set.hierarchy.nodes.size() * hierarchy_descriptor_bytes +
        proxy_set.proxies.size() * proxy_descriptor_bytes +
        static_cast<std::size_t>(proxy_vertex_count) * sizeof(render::MeshVertex) +
        static_cast<std::size_t>(proxy_index_count) * sizeof(std::uint32_t)
    );

    for (const auto& vertex : source.vertices) {
        for (const float component : vertex.position) append_f32(payload, component);
        for (const float component : vertex.color) append_f32(payload, component);
        for (const float component : vertex.normal) append_f32(payload, component);
        for (const float component : vertex.uv) append_f32(payload, component);
    }
    for (const auto index : source.indices) append_u32(payload, index);
    for (const auto& meshlet : meshlets.meshlets) {
        append_u32(payload, meshlet.vertex_offset);
        append_u32(payload, meshlet.vertex_count);
        append_u32(payload, meshlet.triangle_offset);
        append_u32(payload, meshlet.triangle_count);
        append_f32(payload, meshlet.bounds.center[0]);
        append_f32(payload, meshlet.bounds.center[1]);
        append_f32(payload, meshlet.bounds.center[2]);
        append_f32(payload, meshlet.bounds.radius);
    }
    for (const auto index : meshlets.vertex_indices) append_u32(payload, index);
    for (const auto index : meshlets.triangle_indices) append_u8(payload, index);
    for (const auto& page : pages.pages) {
        append_u32(payload, page.meshlet_offset);
        append_u32(payload, page.meshlet_count);
        append_u32(payload, page.vertex_index_offset);
        append_u32(payload, page.vertex_index_count);
        append_u32(payload, page.triangle_index_offset);
        append_u32(payload, page.triangle_index_count);
        append_f32(payload, page.bounds.center[0]);
        append_f32(payload, page.bounds.center[1]);
        append_f32(payload, page.bounds.center[2]);
        append_f32(payload, page.bounds.radius);
    }
    for (const auto& node : proxy_set.hierarchy.nodes) {
        append_u32(payload, node.parent_index);
        append_u32(payload, node.first_child);
        append_u32(payload, node.child_count);
        append_u32(payload, node.page_offset);
        append_u32(payload, node.page_count);
        append_u32(payload, node.depth);
        append_f32(payload, node.bounds.center[0]);
        append_f32(payload, node.bounds.center[1]);
        append_f32(payload, node.bounds.center[2]);
        append_f32(payload, node.bounds.radius);
        append_f32(payload, node.geometric_error);
    }

    std::uint32_t vertex_offset = 0U;
    std::uint32_t index_offset = 0U;
    for (const auto& proxy : proxy_set.proxies) {
        append_u32(payload, proxy.node_index);
        append_u32(payload, proxy.source_triangle_count);
        append_u32(payload, vertex_offset);
        append_u32(payload, static_cast<std::uint32_t>(proxy.vertices.size()));
        append_u32(payload, index_offset);
        append_u32(payload, static_cast<std::uint32_t>(proxy.indices.size()));
        append_f32(payload, proxy.direct_geometric_error);
        vertex_offset += static_cast<std::uint32_t>(proxy.vertices.size());
        index_offset += static_cast<std::uint32_t>(proxy.indices.size());
    }
    for (const auto& proxy : proxy_set.proxies) {
        for (const auto& vertex : proxy.vertices) {
            for (const float component : vertex.position) append_f32(payload, component);
            for (const float component : vertex.color) append_f32(payload, component);
            for (const float component : vertex.normal) append_f32(payload, component);
            for (const float component : vertex.uv) append_f32(payload, component);
        }
    }
    for (const auto& proxy : proxy_set.proxies) {
        for (const auto index : proxy.indices) append_u32(payload, index);
    }

    result.source_fingerprint = fingerprint(payload);
    constexpr std::size_t v6_header_bytes = render::mesh_asset_magic_size +
        sizeof(std::uint32_t) * 12U + sizeof(std::uint64_t) + render::mesh_stable_id_size;
    result.bytes.reserve(v6_header_bytes + payload.size());
    constexpr std::string_view magic = "VICEMESH";
    for (const char value : magic) result.bytes.push_back(static_cast<std::byte>(value));
    append_u32(result.bytes, render::mesh_asset_format_version_v6);
    append_u32(result.bytes, static_cast<std::uint32_t>(source.vertices.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(source.indices.size()));
    append_u64(result.bytes, result.source_fingerprint);
    for (const auto value : source.stable_id) append_u8(result.bytes, value);
    append_u32(result.bytes, static_cast<std::uint32_t>(meshlets.meshlets.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(meshlets.vertex_indices.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(meshlets.triangle_indices.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(pages.pages.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(proxy_set.hierarchy.nodes.size()));
    append_u32(result.bytes, proxy_set.hierarchy.root_index);
    append_u32(result.bytes, static_cast<std::uint32_t>(proxy_set.proxies.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(proxy_vertex_count));
    append_u32(result.bytes, static_cast<std::uint32_t>(proxy_index_count));
    result.bytes.insert(result.bytes.end(), payload.begin(), payload.end());

    const auto decoded = render::decode_mesh_asset(result.bytes);
    if (!decoded.asset.has_value()) {
        result.bytes.clear();
        result.diagnostics = "Cooked mesh failed runtime validation: " + decoded.diagnostics;
        return result;
    }
    if (decoded.asset->version != render::mesh_asset_format_version_v6 ||
        decoded.asset->meshlet_geometry != meshlets ||
        decoded.asset->virtual_geometry_pages != pages ||
        decoded.asset->virtual_geometry_proxies != proxy_set) {
        result.bytes.clear();
        result.diagnostics = "Cooked v6 virtual geometry metadata did not round-trip deterministically.";
        return result;
    }

    result.success = true;
    return result;
}

} // namespace vice::tools::mesh_cooker
