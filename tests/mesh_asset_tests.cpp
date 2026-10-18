#include <vice/render/mesh_asset.hpp>
#include <vice/tools/mesh_cooker.hpp>

#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

    return 0;
}
