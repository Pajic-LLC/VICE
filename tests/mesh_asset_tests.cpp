#include <vice/render/mesh_asset.hpp>
#include <vice/render/meshlet.hpp>
#include <vice/tools/mesh_cooker.hpp>

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
            const float dx = vertex.position[0] - meshlet.bounds.center[0];
            const float dy = vertex.position[1] - meshlet.bounds.center[1];
            const float dz = vertex.position[2] - meshlet.bounds.center[2];
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            assert(distance <= meshlet.bounds.radius + 0.0001F);
        }
    }
}

} // namespace

int main() {
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

    const auto cooked_v3 = tools::mesh_cooker::cook(*parsed_v1.source);
    assert(cooked_v3.success);
    const auto decoded_v3 = render::decode_mesh_asset(cooked_v3.bytes);
    assert(decoded_v3.asset.has_value());
    assert(decoded_v3.asset->version == render::mesh_asset_format_version_v3);
    assert(decoded_v3.asset->stable_id == parsed_v1.source->stable_id);
    assert(decoded_v3.asset->vertices.size() == 4U);

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
    assert(nearly_equal(decoded_surface.asset->vertices[2].uv[1], 1.0F));
    assert(nearly_equal(decoded_surface.asset->vertices[0].normal[1], 1.0F));

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

    auto corrupted = cooked_v3.bytes;
    corrupted.back() ^= std::byte{0x01U};
    assert(!render::decode_mesh_asset(corrupted).asset.has_value());

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

    auto invalid_vertices = triangle_limit_vertices;
    invalid_vertices[0].position[0] = std::numeric_limits<float>::infinity();
    assert(!render::build_meshlets(invalid_vertices, triangle_limit_indices));
    assert(!render::build_meshlets(triangle_limit_vertices, std::vector<std::uint32_t>{0U, 1U, 3U}));
    assert(!render::build_meshlets(triangle_limit_vertices, std::vector<std::uint32_t>{0U, 0U, 1U}));

    return 0;
}
