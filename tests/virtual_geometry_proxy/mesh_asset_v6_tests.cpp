#include <vice/render/mesh_asset.hpp>
#include <vice/tools/mesh_cooker.hpp>

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
constexpr std::size_t fingerprint_offset = vice::render::mesh_asset_magic_size +
    sizeof(std::uint32_t) * 3U;
constexpr std::size_t v5_header_size = vice::render::mesh_asset_magic_size +
    sizeof(std::uint32_t) * 7U + sizeof(std::uint64_t) + vice::render::mesh_stable_id_size;
constexpr std::size_t v6_header_size = v5_header_size + sizeof(std::uint32_t) * 5U;
constexpr std::size_t meshlet_descriptor_bytes = sizeof(std::uint32_t) * 4U + sizeof(float) * 4U;
constexpr std::size_t page_descriptor_bytes = sizeof(std::uint32_t) * 6U + sizeof(float) * 4U;
constexpr std::size_t hierarchy_descriptor_bytes = sizeof(std::uint32_t) * 6U + sizeof(float) * 5U;
constexpr std::size_t proxy_descriptor_bytes = sizeof(std::uint32_t) * 6U + sizeof(float);

[[nodiscard]] std::uint64_t fingerprint(const std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const auto value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
}

[[nodiscard]] std::uint32_t read_u32(
    const std::span<const std::byte> bytes,
    const std::size_t offset
) {
    assert(offset + sizeof(std::uint32_t) <= bytes.size());
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        value |= static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(bytes[offset + index])
        ) << (index * 8U);
    }
    return value;
}

void overwrite_u32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value
) {
    assert(offset + sizeof(value) <= bytes.size());
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

void overwrite_u64(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint64_t value
) {
    assert(offset + sizeof(value) <= bytes.size());
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
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

[[nodiscard]] vice::tools::mesh_cooker::MeshSource make_source() {
    using namespace vice;
    tools::mesh_cooker::MeshSource source;
    source.version = tools::mesh_cooker::mesh_source_format_version_v2;
    source.name = "virtual_geometry_v6";
    source.stable_id[0] = 0x62U;

    constexpr std::uint32_t triangle_count = 32U * 21U + 1U;
    source.vertices.reserve(static_cast<std::size_t>(triangle_count) * 3U);
    source.indices.reserve(static_cast<std::size_t>(triangle_count) * 3U);
    for (std::uint32_t triangle = 0U; triangle < triangle_count; ++triangle) {
        const auto base_index = static_cast<std::uint32_t>(source.vertices.size());
        const float base_x = static_cast<float>(triangle) * 2.0F;
        for (std::uint32_t corner = 0U; corner < 3U; ++corner) {
            render::MeshVertex vertex;
            vertex.position[0] = base_x + (corner == 1U ? 1.0F : 0.0F);
            vertex.position[1] = 0.0F;
            vertex.position[2] = corner == 2U ? 1.0F : 0.0F;
            vertex.color[0] = 0.2F;
            vertex.color[1] = 0.4F;
            vertex.color[2] = 0.8F;
            vertex.normal[0] = 0.0F;
            vertex.normal[1] = 1.0F;
            vertex.normal[2] = 0.0F;
            source.vertices.push_back(vertex);
        }
        source.indices.push_back(base_index);
        source.indices.push_back(base_index + 2U);
        source.indices.push_back(base_index + 1U);
    }
    return source;
}

[[nodiscard]] std::size_t base_payload_size(const std::span<const std::byte> bytes) {
    return static_cast<std::size_t>(read_u32(bytes, 12U)) * sizeof(vice::render::MeshVertex) +
        static_cast<std::size_t>(read_u32(bytes, 16U)) * sizeof(std::uint32_t) +
        static_cast<std::size_t>(read_u32(bytes, 44U)) * meshlet_descriptor_bytes +
        static_cast<std::size_t>(read_u32(bytes, 48U)) * sizeof(std::uint32_t) +
        read_u32(bytes, 52U) +
        static_cast<std::size_t>(read_u32(bytes, 56U)) * page_descriptor_bytes;
}

void refresh_fingerprint(std::vector<std::byte>& bytes) {
    overwrite_u64(
        bytes,
        fingerprint_offset,
        fingerprint(std::span<const std::byte>{bytes}.subspan(v6_header_size))
    );
}

[[nodiscard]] std::vector<std::byte> downgrade_to_v5(const std::vector<std::byte>& v6) {
    const auto source = std::span<const std::byte>{v6};
    const std::size_t base_size = base_payload_size(source);
    assert(v6.size() >= v6_header_size + base_size);
    const auto base_payload = source.subspan(v6_header_size, base_size);

    std::vector<std::byte> v5;
    v5.reserve(v5_header_size + base_size);
    v5.insert(v5.end(), source.begin(), source.begin() + vice::render::mesh_asset_magic_size);
    append_u32(v5, vice::render::mesh_asset_format_version_v5);
    append_u32(v5, read_u32(source, 12U));
    append_u32(v5, read_u32(source, 16U));
    append_u64(v5, fingerprint(base_payload));
    v5.insert(v5.end(), source.begin() + 28U, source.begin() + 44U);
    append_u32(v5, read_u32(source, 44U));
    append_u32(v5, read_u32(source, 48U));
    append_u32(v5, read_u32(source, 52U));
    append_u32(v5, read_u32(source, 56U));
    v5.insert(v5.end(), base_payload.begin(), base_payload.end());
    return v5;
}

} // namespace

int main() {
    using namespace vice;

    const auto source = make_source();
    const auto cooked = tools::mesh_cooker::cook(source);
    const auto cooked_repeat = tools::mesh_cooker::cook(source);
    assert(cooked.success);
    assert(cooked_repeat.success);
    assert(cooked.bytes == cooked_repeat.bytes);

    const auto decoded = render::decode_mesh_asset(cooked.bytes);
    assert(decoded.asset.has_value());
    assert(decoded.asset->version == render::mesh_asset_format_version_v6);
    assert(decoded.asset->meshlet_geometry.meshlets.size() == 33U);
    assert(decoded.asset->virtual_geometry_pages.pages.size() == 2U);
    assert(decoded.asset->virtual_geometry_proxies.hierarchy.nodes.size() == 3U);
    assert(decoded.asset->virtual_geometry_proxies.hierarchy.root_index == 0U);
    assert(decoded.asset->virtual_geometry_proxies.proxies.size() == 1U);

    const auto& proxy = decoded.asset->virtual_geometry_proxies.proxies.front();
    assert(proxy.node_index == 0U);
    assert(proxy.source_triangle_count == 673U);
    assert(proxy.indices.size() / 3U == proxy.source_triangle_count);
    assert(proxy.direct_geometric_error == 0.0F);
    assert(decoded.asset->virtual_geometry_proxies.hierarchy.nodes[0].geometric_error == 0.0F);
    assert(decoded.asset->virtual_geometry_proxies.hierarchy.nodes[1].geometric_error == 0.0F);
    assert(decoded.asset->virtual_geometry_proxies.hierarchy.nodes[2].geometric_error == 0.0F);

    const auto v5 = downgrade_to_v5(cooked.bytes);
    const auto decoded_v5 = render::decode_mesh_asset(v5);
    assert(decoded_v5.asset.has_value());
    assert(decoded_v5.asset->version == render::mesh_asset_format_version_v5);
    assert(decoded_v5.asset->meshlet_geometry == decoded.asset->meshlet_geometry);
    assert(decoded_v5.asset->virtual_geometry_pages == decoded.asset->virtual_geometry_pages);
    assert(decoded_v5.asset->virtual_geometry_proxies.hierarchy.nodes.empty());
    assert(decoded_v5.asset->virtual_geometry_proxies.proxies.empty());

    const auto cooked_span = std::span<const std::byte>{cooked.bytes};
    const std::size_t base_size = base_payload_size(cooked_span);
    const std::uint32_t hierarchy_count = read_u32(cooked_span, 60U);
    const std::uint32_t proxy_count = read_u32(cooked_span, 68U);
    const std::uint32_t proxy_vertex_count = read_u32(cooked_span, 72U);
    assert(hierarchy_count == 3U);
    assert(proxy_count == 1U);

    const std::size_t hierarchy_offset = v6_header_size + base_size;
    const std::size_t proxy_descriptor_offset = hierarchy_offset +
        static_cast<std::size_t>(hierarchy_count) * hierarchy_descriptor_bytes;
    const std::size_t proxy_vertex_offset = proxy_descriptor_offset +
        static_cast<std::size_t>(proxy_count) * proxy_descriptor_bytes;
    const std::size_t proxy_index_offset = proxy_vertex_offset +
        static_cast<std::size_t>(proxy_vertex_count) * sizeof(render::MeshVertex);

    auto corrupt_hierarchy_parent = cooked.bytes;
    overwrite_u32(corrupt_hierarchy_parent, hierarchy_offset, 0U);
    refresh_fingerprint(corrupt_hierarchy_parent);
    assert(!render::decode_mesh_asset(corrupt_hierarchy_parent).asset.has_value());

    auto corrupt_hierarchy_error = cooked.bytes;
    overwrite_u32(
        corrupt_hierarchy_error,
        hierarchy_offset + sizeof(std::uint32_t) * 6U + sizeof(float) * 4U,
        0x7FC00000U
    );
    refresh_fingerprint(corrupt_hierarchy_error);
    assert(!render::decode_mesh_asset(corrupt_hierarchy_error).asset.has_value());

    auto corrupt_proxy_node = cooked.bytes;
    overwrite_u32(corrupt_proxy_node, proxy_descriptor_offset, 1U);
    refresh_fingerprint(corrupt_proxy_node);
    assert(!render::decode_mesh_asset(corrupt_proxy_node).asset.has_value());

    auto corrupt_direct_error = cooked.bytes;
    overwrite_u32(
        corrupt_direct_error,
        proxy_descriptor_offset + sizeof(std::uint32_t) * 6U,
        std::bit_cast<std::uint32_t>(1.0F)
    );
    refresh_fingerprint(corrupt_direct_error);
    assert(!render::decode_mesh_asset(corrupt_direct_error).asset.has_value());

    auto corrupt_proxy_vertex = cooked.bytes;
    overwrite_u32(
        corrupt_proxy_vertex,
        proxy_vertex_offset,
        std::bit_cast<std::uint32_t>(123456.0F)
    );
    refresh_fingerprint(corrupt_proxy_vertex);
    assert(!render::decode_mesh_asset(corrupt_proxy_vertex).asset.has_value());

    auto corrupt_proxy_index = cooked.bytes;
    const std::uint32_t first_proxy_vertex_count = read_u32(cooked_span, proxy_descriptor_offset + 12U);
    overwrite_u32(corrupt_proxy_index, proxy_index_offset, first_proxy_vertex_count);
    refresh_fingerprint(corrupt_proxy_index);
    assert(!render::decode_mesh_asset(corrupt_proxy_index).asset.has_value());

    auto invalid_proxy_count = cooked.bytes;
    overwrite_u32(invalid_proxy_count, 68U, 0U);
    assert(!render::decode_mesh_asset(invalid_proxy_count).asset.has_value());

    auto truncated = cooked.bytes;
    truncated.pop_back();
    assert(!render::decode_mesh_asset(truncated).asset.has_value());

    auto trailing = cooked.bytes;
    trailing.push_back(std::byte{0U});
    assert(!render::decode_mesh_asset(trailing).asset.has_value());

    return 0;
}
