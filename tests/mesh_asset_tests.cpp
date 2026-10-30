#include <vice/render/mesh_asset.hpp>
#include <vice/render/meshlet.hpp>
#include <vice/tools/mesh_cooker.hpp>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
constexpr std::size_t mesh_v5_header_size = vice::render::mesh_asset_magic_size +
    sizeof(std::uint32_t) * 7U + sizeof(std::uint64_t) + vice::render::mesh_stable_id_size;
constexpr std::size_t mesh_fingerprint_offset = vice::render::mesh_asset_magic_size +
    sizeof(std::uint32_t) * 3U;
constexpr std::size_t meshlet_descriptor_bytes = sizeof(std::uint32_t) * 4U + sizeof(float) * 4U;

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

void append_f32(std::vector<std::byte>& bytes, const float value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

std::uint64_t fingerprint(const std::span<const std::byte> bytes) {
    std::uint64_t hash = fnv_offset_basis;
    for (const std::byte value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
}

std::vector<std::byte> legacy_mesh(
    const std::uint32_t version,
    const std::span<const vice::render::MeshVertex> vertices,
    const std::span<const std::uint32_t> indices,
    const vice::render::MeshStableId& stable_id
) {
    std::vector<std::byte> payload;
    for (const auto& vertex : vertices) {
        for (const float component : vertex.position) append_f32(payload, component);
        for (const float component : vertex.color) append_f32(payload, component);
        if (version >= vice::render::mesh_asset_format_version_v3) {
            for (const float component : vertex.normal) append_f32(payload, component);
            for (const float component : vertex.uv) append_f32(payload, component);
        }
    }
    for (const std::uint32_t index : indices) append_u32(payload, index);

    std::vector<std::byte> result;
    constexpr std::string_view magic = "VICEMESH";
    for (const char value : magic) result.push_back(static_cast<std::byte>(value));
    append_u32(result, version);
    append_u32(result, static_cast<std::uint32_t>(vertices.size()));
    append_u32(result, static_cast<std::uint32_t>(indices.size()));
    append_u64(result, fingerprint(payload));
    if (version >= vice::render::mesh_asset_format_version_v2) {
        for (const std::uint8_t value : stable_id) result.push_back(static_cast<std::byte>(value));
    }
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

bool nearly_equal(const float left, const float right) {
    return std::abs(left - right) < 0.0001F;
}

vice::render::MeshVertex meshlet_vertex(const float x, const float y, const float z) {
    vice::render::MeshVertex vertex;
    vertex.position[0] = x;
    vertex.position[1] = y;
    vertex.position[2] = z;
    return vertex;
}

std::vector<std::uint32_t> reconstruct_meshlet_indices(
    const vice::render::MeshletGeometry& geometry
) {
    std::vector<std::uint32_t> result;
    for (const auto& meshlet : geometry.meshlets) {
        assert(meshlet.vertex_count > 0U);
        assert(meshlet.vertex_count <= vice::render::meshlet_max_vertices);
        assert(meshlet.triangle_count > 0U);
        assert(meshlet.triangle_count <= vice::render::meshlet_max_triangles);
        assert(static_cast<std::size_t>(meshlet.vertex_offset) + meshlet.vertex_count <=
            geometry.vertex_indices.size());
        assert(static_cast<std::size_t>(meshlet.triangle_offset) + meshlet.triangle_count * 3U <=
            geometry.triangle_indices.size());
        for (std::uint32_t triangle = 0U; triangle < meshlet.triangle_count; ++triangle) {
            for (std::uint32_t corner = 0U; corner < 3U; ++corner) {
                const std::size_t local_offset = static_cast<std::size_t>(meshlet.triangle_offset) +
                    static_cast<std::size_t>(triangle) * 3U + corner;
                const std::uint8_t local_index = geometry.triangle_indices[local_offset];
                assert(local_index < meshlet.vertex_count);
                result.push_back(geometry.vertex_indices[
                    static_cast<std::size_t>(meshlet.vertex_offset) + local_index
                ]);
            }
        }
    }
    return result;
}

void assert_meshlet_bounds_contain_vertices(
    const std::span<const vice::render::MeshVertex> vertices,
    const vice::render::MeshletGeometry& geometry
) {
    for (const auto& meshlet : geometry.meshlets) {
        assert(std::isfinite(meshlet.bounds.center[0]));
        assert(std::isfinite(meshlet.bounds.center[1]));
        assert(std::isfinite(meshlet.bounds.center[2]));
        assert(std::isfinite(meshlet.bounds.radius));
        assert(meshlet.bounds.radius >= 0.0F);
        for (std::uint32_t local_index = 0U; local_index < meshlet.vertex_count; ++local_index) {
            const auto global_index = geometry.vertex_indices[
                static_cast<std::size_t>(meshlet.vertex_offset) + local_index
            ];
            const auto& vertex = vertices[global_index];
            const double dx = static_cast<double>(vertex.position[0]) - meshlet.bounds.center[0];
            const double dy = static_cast<double>(vertex.position[1]) - meshlet.bounds.center[1];
            const double dz = static_cast<double>(vertex.position[2]) - meshlet.bounds.center[2];
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double tolerance = std::max(
                1.0e-4,
                static_cast<double>(meshlet.bounds.radius) * 1.0e-5
            );
            assert(distance <= static_cast<double>(meshlet.bounds.radius) + tolerance);
        }
    }
}

} // namespace

int main() {
    {
        const auto parsed = vice::tools::mesh_cooker::parse_source_manifest(
            "vice-mesh-source 2\nvirtual_geometry dense\nname deforming\n"
            "id 84000000-0000-4000-8000-000000000021\n"
            "vertex 0 0 0 1 1 1 0 0 1 0 0\n"
            "vertex 1 0 0 1 1 1 0 0 1 1 0\n"
            "vertex 0 1 0 1 1 1 0 0 1 0 1\ntriangle 0 1 2\n");
        assert(parsed.source && !parsed.source->build_virtual_geometry_proxies);
        const auto cooked = vice::tools::mesh_cooker::cook(*parsed.source);
        assert(cooked.success);
        const auto decoded = vice::render::decode_mesh_asset(cooked.bytes);
        assert(decoded.asset && decoded.asset->vertices.size() == 3);
        assert(decoded.asset->indices == parsed.source->indices);
        assert(decoded.asset->virtual_geometry_proxies.proxies.empty());
        assert(!vice::tools::mesh_cooker::parse_source_manifest(
            "vice-mesh-source 2\nvirtual_geometry invented\n").source);
    }

    using namespace vice;

    constexpr std::string_view source_v1 = R"(
vice-mesh-source 1
name test_ramp
id 50000000-0000-4000-8000-000000000099
collision triangle_mesh
vertex 0 0 0 0.2 0.4 0.8
vertex 0 0 1 0.2 0.5 0.9
vertex 2 1 0 0.8 0.4 0.2
vertex 2 1 1 0.9 0.5 0.2
triangle 0 2 1
triangle 1 2 3
)";

    const auto parsed_v1 = tools::mesh_cooker::parse_source_manifest(source_v1);
    assert(parsed_v1.source.has_value());
    assert(parsed_v1.source->version == tools::mesh_cooker::mesh_source_format_version_v1);
    assert(render::valid(parsed_v1.source->stable_id));
    assert(std::abs(parsed_v1.source->vertices[0].normal[1]) > 0.1F);

    const auto cooked_v5 = tools::mesh_cooker::cook(*parsed_v1.source);
    const auto cooked_v5_repeat = tools::mesh_cooker::cook(*parsed_v1.source);
    assert(cooked_v5.success);
    assert(cooked_v5_repeat.success);
    assert(cooked_v5.bytes == cooked_v5_repeat.bytes);
    const auto decoded_v5 = render::decode_mesh_asset(cooked_v5.bytes);
    assert(decoded_v5.asset.has_value());
    assert(decoded_v5.asset->version == render::mesh_asset_format_version_v5);
    assert(decoded_v5.asset->stable_id == parsed_v1.source->stable_id);
    assert(decoded_v5.asset->vertices.size() == 4U);
    assert(!decoded_v5.asset->meshlet_geometry.meshlets.empty());
    assert(!decoded_v5.asset->virtual_geometry_pages.pages.empty());
    assert(reconstruct_meshlet_indices(decoded_v5.asset->meshlet_geometry) ==
        parsed_v1.source->indices);
    assert_meshlet_bounds_contain_vertices(
        decoded_v5.asset->vertices,
        decoded_v5.asset->meshlet_geometry
    );

    constexpr std::string_view source_v2 = R"(
vice-mesh-source 2
name textured_triangle
id 50000000-0000-4000-8000-000000000124
collision none
vertex 0 0 0 1 1 1 0 1 0 0 0
vertex 1 0 0 1 1 1 0 1 0 1 0
vertex 0 0 1 1 1 1 0 1 0 0 1
triangle 0 1 2
)";
    const auto parsed_v2 = tools::mesh_cooker::parse_source_manifest(source_v2);
    assert(parsed_v2.source.has_value());
    assert(parsed_v2.source->version == tools::mesh_cooker::mesh_source_format_version_v2);
    assert(nearly_equal(parsed_v2.source->vertices[1].uv[0], 1.0F));
    const auto cooked_surface = tools::mesh_cooker::cook(*parsed_v2.source);
    assert(cooked_surface.success);
    const auto decoded_surface = render::decode_mesh_asset(cooked_surface.bytes);
    assert(decoded_surface.asset.has_value());
    assert(decoded_surface.asset->version == render::mesh_asset_format_version_v5);
    assert(nearly_equal(decoded_surface.asset->vertices[2].uv[1], 1.0F));
    assert(nearly_equal(decoded_surface.asset->vertices[0].normal[1], 1.0F));
    assert(reconstruct_meshlet_indices(decoded_surface.asset->meshlet_geometry) ==
        parsed_v2.source->indices);
    assert(decoded_surface.asset->virtual_geometry_pages.pages.size() == 1U);

    const auto legacy_v1 = legacy_mesh(
        render::mesh_asset_format_version_v1,
        parsed_v1.source->vertices,
        parsed_v1.source->indices,
        parsed_v1.source->stable_id
    );
    const auto decoded_legacy_v1 = render::decode_mesh_asset(legacy_v1);
    assert(decoded_legacy_v1.asset.has_value());
    assert(decoded_legacy_v1.asset->version == render::mesh_asset_format_version_v1);
    assert(!render::valid(decoded_legacy_v1.asset->stable_id));
    assert(nearly_equal(decoded_legacy_v1.asset->vertices[0].normal[1], 1.0F));
    assert(nearly_equal(decoded_legacy_v1.asset->vertices[0].uv[0], 0.0F));
    assert(decoded_legacy_v1.asset->meshlet_geometry.meshlets.empty());
    assert(decoded_legacy_v1.asset->virtual_geometry_pages.pages.empty());

    const auto legacy_v2 = legacy_mesh(
        render::mesh_asset_format_version_v2,
        parsed_v1.source->vertices,
        parsed_v1.source->indices,
        parsed_v1.source->stable_id
    );
    const auto decoded_legacy_v2 = render::decode_mesh_asset(legacy_v2);
    assert(decoded_legacy_v2.asset.has_value());
    assert(decoded_legacy_v2.asset->version == render::mesh_asset_format_version_v2);
    assert(decoded_legacy_v2.asset->stable_id == parsed_v1.source->stable_id);
    assert(decoded_legacy_v2.asset->meshlet_geometry.meshlets.empty());
    assert(decoded_legacy_v2.asset->virtual_geometry_pages.pages.empty());

    const auto legacy_v3 = legacy_mesh(
        render::mesh_asset_format_version_v3,
        parsed_v1.source->vertices,
        parsed_v1.source->indices,
        parsed_v1.source->stable_id
    );
    const auto decoded_legacy_v3 = render::decode_mesh_asset(legacy_v3);
    assert(decoded_legacy_v3.asset.has_value());
    assert(decoded_legacy_v3.asset->version == render::mesh_asset_format_version_v3);
    assert(decoded_legacy_v3.asset->stable_id == parsed_v1.source->stable_id);
    assert(decoded_legacy_v3.asset->meshlet_geometry.meshlets.empty());
    assert(decoded_legacy_v3.asset->virtual_geometry_pages.pages.empty());
    assert(nearly_equal(
        decoded_legacy_v3.asset->vertices[0].normal[1],
        parsed_v1.source->vertices[0].normal[1]
    ));

    auto corrupted = cooked_v5.bytes;
    corrupted.back() ^= std::byte{0x01U};
    assert(!render::decode_mesh_asset(corrupted).asset.has_value());

    auto semantic_corruption = cooked_v5.bytes;
    const auto& cooked_geometry = decoded_v5.asset->meshlet_geometry;
    const std::size_t local_triangle_stream_offset = mesh_v5_header_size +
        decoded_v5.asset->vertices.size() * sizeof(render::MeshVertex) +
        decoded_v5.asset->indices.size() * sizeof(std::uint32_t) +
        cooked_geometry.meshlets.size() * meshlet_descriptor_bytes +
        cooked_geometry.vertex_indices.size() * sizeof(std::uint32_t);
    assert(local_triangle_stream_offset < semantic_corruption.size());
    semantic_corruption[local_triangle_stream_offset] ^= std::byte{0x01U};
    overwrite_u64(
        semantic_corruption,
        mesh_fingerprint_offset,
        fingerprint(std::span<const std::byte>{semantic_corruption}.subspan(mesh_v5_header_size))
    );
    assert(!render::decode_mesh_asset(semantic_corruption).asset.has_value());

    constexpr std::string_view no_id = R"(
vice-mesh-source 1
name legacy_source
vertex 0 0 0 1 1 1
vertex 1 0 0 1 1 1
vertex 0 1 0 1 1 1
triangle 0 1 2
)";
    const auto legacy_source = tools::mesh_cooker::parse_source_manifest(no_id);
    assert(legacy_source.source.has_value());
    assert(!render::valid(legacy_source.source->stable_id));
    assert(tools::mesh_cooker::cook(*legacy_source.source).success);

    constexpr std::string_view invalid_id = R"(
vice-mesh-source 1
name invalid_id
id definitely-not-a-uuid
vertex 0 0 0 1 1 1
vertex 1 0 0 1 1 1
vertex 0 1 0 1 1 1
triangle 0 1 2
)";
    assert(!tools::mesh_cooker::parse_source_manifest(invalid_id).source.has_value());

    constexpr std::string_view bad_v2 = R"(
vice-mesh-source 2
name invalid_surface
vertex 0 0 0 1 1 1 0 0 0 0 0
vertex 1 0 0 1 1 1 0 1 0 1 0
vertex 0 1 0 1 1 1 0 1 0 0 1
triangle 0 1 2
)";
    assert(!tools::mesh_cooker::parse_source_manifest(bad_v2).source.has_value());

    const std::vector<render::MeshVertex> quad_vertices{
        meshlet_vertex(-1.0F, 0.0F, -1.0F),
        meshlet_vertex(1.0F, 0.0F, -1.0F),
        meshlet_vertex(-1.0F, 0.0F, 1.0F),
        meshlet_vertex(1.0F, 0.0F, 1.0F),
    };
    const std::vector<std::uint32_t> quad_indices{0U, 1U, 2U, 2U, 1U, 3U};
    const auto quad_meshlets = render::build_meshlets(quad_vertices, quad_indices);
    const auto quad_meshlets_repeat = render::build_meshlets(quad_vertices, quad_indices);
    assert(quad_meshlets);
    assert(quad_meshlets_repeat);
    assert(*quad_meshlets.geometry == *quad_meshlets_repeat.geometry);
    assert(quad_meshlets.geometry->meshlets.size() == 1U);
    assert(quad_meshlets.geometry->meshlets[0].vertex_count == 4U);
    assert(quad_meshlets.geometry->meshlets[0].triangle_count == 2U);
    assert(reconstruct_meshlet_indices(*quad_meshlets.geometry) == quad_indices);
    assert_meshlet_bounds_contain_vertices(quad_vertices, *quad_meshlets.geometry);

    std::vector<render::MeshVertex> vertex_limit_vertices;
    std::vector<std::uint32_t> vertex_limit_indices;
    for (std::uint32_t index = 0U; index < 66U; ++index) {
        vertex_limit_vertices.push_back(meshlet_vertex(
            static_cast<float>(index),
            static_cast<float>(index % 5U),
            static_cast<float>(index % 7U)
        ));
        vertex_limit_indices.push_back(index);
    }
    const auto vertex_limited = render::build_meshlets(
        vertex_limit_vertices, vertex_limit_indices
    );
    assert(vertex_limited);
    assert(vertex_limited.geometry->meshlets.size() == 2U);
    assert(vertex_limited.geometry->meshlets[0].vertex_count == 63U);
    assert(vertex_limited.geometry->meshlets[0].triangle_count == 21U);
    assert(vertex_limited.geometry->meshlets[1].vertex_count == 3U);
    assert(vertex_limited.geometry->meshlets[1].triangle_count == 1U);
    assert(reconstruct_meshlet_indices(*vertex_limited.geometry) == vertex_limit_indices);
    assert_meshlet_bounds_contain_vertices(vertex_limit_vertices, *vertex_limited.geometry);

    const std::vector<render::MeshVertex> triangle_limit_vertices{
        meshlet_vertex(0.0F, 0.0F, 0.0F),
        meshlet_vertex(1.0F, 0.0F, 0.0F),
        meshlet_vertex(0.0F, 1.0F, 0.0F),
    };
    std::vector<std::uint32_t> triangle_limit_indices;
    triangle_limit_indices.reserve(127U * 3U);
    for (std::uint32_t triangle = 0U; triangle < 127U; ++triangle) {
        triangle_limit_indices.push_back(0U);
        triangle_limit_indices.push_back(1U);
        triangle_limit_indices.push_back(2U);
    }
    const auto triangle_limited = render::build_meshlets(
        triangle_limit_vertices, triangle_limit_indices
    );
    assert(triangle_limited);
    assert(triangle_limited.geometry->meshlets.size() == 2U);
    assert(triangle_limited.geometry->meshlets[0].vertex_count == 3U);
    assert(triangle_limited.geometry->meshlets[0].triangle_count == 126U);
    assert(triangle_limited.geometry->meshlets[1].triangle_count == 1U);
    assert(reconstruct_meshlet_indices(*triangle_limited.geometry) == triangle_limit_indices);

    render::MeshletGeometry page_geometry;
    for (std::uint32_t index = 0U; index < 33U; ++index) {
        page_geometry.meshlets.push_back(render::Meshlet{
            .vertex_offset = index * 3U,
            .vertex_count = 3U,
            .triangle_offset = index * 3U,
            .triangle_count = 1U,
            .bounds = render::MeshletBounds{
                .center = {static_cast<float>(index), 0.0F, 0.0F},
                .radius = 0.25F,
            },
        });
        page_geometry.vertex_indices.push_back(index * 3U);
        page_geometry.vertex_indices.push_back(index * 3U + 1U);
        page_geometry.vertex_indices.push_back(index * 3U + 2U);
        page_geometry.triangle_indices.push_back(0U);
        page_geometry.triangle_indices.push_back(1U);
        page_geometry.triangle_indices.push_back(2U);
    }

    const auto page_build = render::build_virtual_geometry_pages(page_geometry);
    const auto page_build_repeat = render::build_virtual_geometry_pages(page_geometry);
    assert(page_build);
    assert(page_build_repeat);
    assert(*page_build.page_table == *page_build_repeat.page_table);
    assert(page_build.page_table->pages.size() == 2U);

    const auto& first_page = page_build.page_table->pages[0];
    assert(first_page.meshlet_offset == 0U);
    assert(first_page.meshlet_count == render::virtual_geometry_page_max_meshlets);
    assert(first_page.vertex_index_offset == 0U);
    assert(first_page.vertex_index_count == 96U);
    assert(first_page.triangle_index_offset == 0U);
    assert(first_page.triangle_index_count == 96U);
    assert(nearly_equal(first_page.bounds.center[0], 15.5F));
    assert(nearly_equal(first_page.bounds.center[1], 0.0F));
    assert(nearly_equal(first_page.bounds.center[2], 0.0F));
    assert(nearly_equal(first_page.bounds.radius, 15.75F));

    const auto& second_page = page_build.page_table->pages[1];
    assert(second_page.meshlet_offset == 32U);
    assert(second_page.meshlet_count == 1U);
    assert(second_page.vertex_index_offset == 96U);
    assert(second_page.vertex_index_count == 3U);
    assert(second_page.triangle_index_offset == 96U);
    assert(second_page.triangle_index_count == 3U);
    assert(nearly_equal(second_page.bounds.center[0], 32.0F));
    assert(nearly_equal(second_page.bounds.radius, 0.25F));

    auto noncanonical_page_geometry = page_geometry;
    noncanonical_page_geometry.meshlets[1].vertex_offset += 1U;
    assert(!render::build_virtual_geometry_pages(noncanonical_page_geometry));

    auto invalid_page_bounds = page_geometry;
    invalid_page_bounds.meshlets[0].bounds.radius = std::numeric_limits<float>::infinity();
    assert(!render::build_virtual_geometry_pages(invalid_page_bounds));

    auto invalid_page_local_index = page_geometry;
    invalid_page_local_index.triangle_indices[0] = 3U;
    assert(!render::build_virtual_geometry_pages(invalid_page_local_index));

    auto trailing_page_stream = page_geometry;
    trailing_page_stream.vertex_indices.push_back(999U);
    assert(!render::build_virtual_geometry_pages(trailing_page_stream));
    assert(!render::build_virtual_geometry_pages(render::MeshletGeometry{}));

    auto invalid_vertices = triangle_limit_vertices;
    invalid_vertices[0].position[0] = std::numeric_limits<float>::infinity();
    assert(!render::build_meshlets(invalid_vertices, triangle_limit_indices));
    assert(!render::build_meshlets(
        triangle_limit_vertices,
        std::vector<std::uint32_t>{0U, 1U, 3U}
    ));
    assert(!render::build_meshlets(
        triangle_limit_vertices,
        std::vector<std::uint32_t>{0U, 0U, 1U}
    ));

    return 0;
}
