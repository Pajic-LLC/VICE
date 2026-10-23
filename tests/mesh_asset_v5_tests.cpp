#include <vice/render/mesh_asset.hpp>
#include <vice/tools/mesh_cooker.hpp>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
constexpr std::size_t mesh_fingerprint_offset = vice::render::mesh_asset_magic_size +
    sizeof(std::uint32_t) * 3U;
constexpr std::size_t mesh_v4_header_size = vice::render::mesh_asset_magic_size +
    sizeof(std::uint32_t) * 6U + sizeof(std::uint64_t) + vice::render::mesh_stable_id_size;
constexpr std::size_t mesh_v5_header_size = vice::render::mesh_asset_magic_size +
    sizeof(std::uint32_t) * 7U + sizeof(std::uint64_t) + vice::render::mesh_stable_id_size;
constexpr std::size_t meshlet_descriptor_bytes = sizeof(std::uint32_t) * 4U + sizeof(float) * 4U;
constexpr std::size_t page_descriptor_bytes = sizeof(std::uint32_t) * 6U + sizeof(float) * 4U;

std::uint64_t fingerprint(const std::span<const std::byte> bytes) {
    std::uint64_t hash = fnv_offset_basis;
    for (const std::byte value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
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

vice::tools::mesh_cooker::MeshSource make_multi_page_source() {
    using namespace vice;
    tools::mesh_cooker::MeshSource source;
    source.version = tools::mesh_cooker::mesh_source_format_version_v2;
    source.name = "virtual_geometry_pages";

    constexpr std::uint32_t triangle_count = 32U * 21U + 1U;
    source.vertices.reserve(static_cast<std::size_t>(triangle_count) * 3U);
    source.indices.reserve(static_cast<std::size_t>(triangle_count) * 3U);
    for (std::uint32_t triangle = 0U; triangle < triangle_count; ++triangle) {
        const std::uint32_t base_index = static_cast<std::uint32_t>(source.vertices.size());
        const float base_x = static_cast<float>(triangle) * 2.0F;

        render::MeshVertex a;
        a.position[0] = base_x;
        a.position[1] = 0.0F;
        a.position[2] = 0.0F;

        render::MeshVertex b;
        b.position[0] = base_x + 1.0F;
        b.position[1] = 0.0F;
        b.position[2] = 0.0F;

        render::MeshVertex c;
        c.position[0] = base_x;
        c.position[1] = 0.0F;
        c.position[2] = 1.0F;

        source.vertices.push_back(a);
        source.vertices.push_back(b);
        source.vertices.push_back(c);
        source.indices.push_back(base_index);
        source.indices.push_back(base_index + 1U);
        source.indices.push_back(base_index + 2U);
    }
    return source;
}

bool contains(
    const vice::render::MeshletBounds& outer,
    const vice::render::MeshletBounds& inner
) {
    const double dx = static_cast<double>(inner.center[0]) - outer.center[0];
    const double dy = static_cast<double>(inner.center[1]) - outer.center[1];
    const double dz = static_cast<double>(inner.center[2]) - outer.center[2];
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double tolerance = std::max(
        1.0e-4,
        static_cast<double>(outer.radius) * 1.0e-5
    );
    return distance + static_cast<double>(inner.radius) <=
        static_cast<double>(outer.radius) + tolerance;
}

std::vector<std::byte> downgrade_to_v4(
    const std::vector<std::byte>& v5,
    const std::size_t page_count
) {
    assert(v5.size() >= mesh_v5_header_size + page_count * page_descriptor_bytes);
    std::vector<std::byte> v4 = v5;
    v4.resize(v4.size() - page_count * page_descriptor_bytes);
    v4.erase(
        v4.begin() + static_cast<std::ptrdiff_t>(mesh_v4_header_size),
        v4.begin() + static_cast<std::ptrdiff_t>(mesh_v5_header_size)
    );
    overwrite_u32(v4, vice::render::mesh_asset_magic_size, vice::render::mesh_asset_format_version_v4);
    overwrite_u64(
        v4,
        mesh_fingerprint_offset,
        fingerprint(std::span<const std::byte>{v4}.subspan(mesh_v4_header_size))
    );
    return v4;
}

} // namespace

int main() {
    using namespace vice;

    const auto source = make_multi_page_source();
    const auto cooked = tools::mesh_cooker::cook(source);
    const auto cooked_repeat = tools::mesh_cooker::cook(source);
    assert(cooked.success);
    assert(cooked_repeat.success);
    assert(cooked.bytes == cooked_repeat.bytes);

    const auto decoded = render::decode_mesh_asset(cooked.bytes);
    assert(decoded.asset.has_value());
    assert(decoded.asset->version == render::mesh_asset_format_version_v5);
    assert(decoded.asset->meshlet_geometry.meshlets.size() == 33U);
    assert(decoded.asset->virtual_geometry_pages.pages.size() == 2U);

    const auto& geometry = decoded.asset->meshlet_geometry;
    const auto& pages = decoded.asset->virtual_geometry_pages.pages;
    assert(pages[0].meshlet_offset == 0U);
    assert(pages[0].meshlet_count == render::virtual_geometry_page_max_meshlets);
    assert(pages[1].meshlet_offset == render::virtual_geometry_page_max_meshlets);
    assert(pages[1].meshlet_count == 1U);

    std::size_t meshlet_coverage = 0U;
    std::size_t vertex_coverage = 0U;
    std::size_t triangle_coverage = 0U;
    for (const auto& page : pages) {
        assert(page.meshlet_offset == meshlet_coverage);
        assert(page.vertex_index_offset == vertex_coverage);
        assert(page.triangle_index_offset == triangle_coverage);
        for (std::uint32_t local = 0U; local < page.meshlet_count; ++local) {
            assert(contains(page.bounds, geometry.meshlets[page.meshlet_offset + local].bounds));
        }
        meshlet_coverage += page.meshlet_count;
        vertex_coverage += page.vertex_index_count;
        triangle_coverage += page.triangle_index_count;
    }
    assert(meshlet_coverage == geometry.meshlets.size());
    assert(vertex_coverage == geometry.vertex_indices.size());
    assert(triangle_coverage == geometry.triangle_indices.size());

    const auto legacy_v4 = downgrade_to_v4(cooked.bytes, pages.size());
    const auto decoded_v4 = render::decode_mesh_asset(legacy_v4);
    assert(decoded_v4.asset.has_value());
    assert(decoded_v4.asset->version == render::mesh_asset_format_version_v4);
    assert(decoded_v4.asset->meshlet_geometry == geometry);
    assert(decoded_v4.asset->virtual_geometry_pages.pages.empty());

    const std::size_t first_page_offset = mesh_v5_header_size +
        decoded.asset->vertices.size() * sizeof(render::MeshVertex) +
        decoded.asset->indices.size() * sizeof(std::uint32_t) +
        geometry.meshlets.size() * meshlet_descriptor_bytes +
        geometry.vertex_indices.size() * sizeof(std::uint32_t) +
        geometry.triangle_indices.size();
    assert(first_page_offset + page_descriptor_bytes <= cooked.bytes.size());

    auto corrupt_page_range = cooked.bytes;
    overwrite_u32(corrupt_page_range, first_page_offset + sizeof(std::uint32_t), 31U);
    overwrite_u64(
        corrupt_page_range,
        mesh_fingerprint_offset,
        fingerprint(std::span<const std::byte>{corrupt_page_range}.subspan(mesh_v5_header_size))
    );
    assert(!render::decode_mesh_asset(corrupt_page_range).asset.has_value());

    auto corrupt_page_bounds = cooked.bytes;
    overwrite_u32(
        corrupt_page_bounds,
        first_page_offset + sizeof(std::uint32_t) * 6U + sizeof(float) * 3U,
        std::bit_cast<std::uint32_t>(-1.0F)
    );
    overwrite_u64(
        corrupt_page_bounds,
        mesh_fingerprint_offset,
        fingerprint(std::span<const std::byte>{corrupt_page_bounds}.subspan(mesh_v5_header_size))
    );
    assert(!render::decode_mesh_asset(corrupt_page_bounds).asset.has_value());

    auto invalid_page_count = cooked.bytes;
    overwrite_u32(invalid_page_count, mesh_v4_header_size, 0U);
    assert(!render::decode_mesh_asset(invalid_page_count).asset.has_value());

    auto truncated = cooked.bytes;
    truncated.pop_back();
    assert(!render::decode_mesh_asset(truncated).asset.has_value());

    return 0;
}
