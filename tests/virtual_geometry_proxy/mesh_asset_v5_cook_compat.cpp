#include <vice/render/mesh_asset.hpp>
#include <vice/tools/mesh_cooker.hpp>

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace vice::tools::mesh_cooker {
namespace {

constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
constexpr std::size_t v5_header_size = render::mesh_asset_magic_size +
    sizeof(std::uint32_t) * 7U + sizeof(std::uint64_t) + render::mesh_stable_id_size;
constexpr std::size_t v6_header_size = v5_header_size + sizeof(std::uint32_t) * 5U;
constexpr std::size_t meshlet_descriptor_bytes = sizeof(std::uint32_t) * 4U + sizeof(float) * 4U;
constexpr std::size_t page_descriptor_bytes = sizeof(std::uint32_t) * 6U + sizeof(float) * 4U;

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

[[nodiscard]] std::uint64_t fingerprint(const std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const auto value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
}

[[nodiscard]] std::size_t base_payload_size(const std::span<const std::byte> bytes) {
    const auto vertex_count = read_u32(bytes, 12U);
    const auto index_count = read_u32(bytes, 16U);
    const auto meshlet_count = read_u32(bytes, 44U);
    const auto meshlet_vertex_count = read_u32(bytes, 48U);
    const auto meshlet_triangle_count = read_u32(bytes, 52U);
    const auto page_count = read_u32(bytes, 56U);
    return static_cast<std::size_t>(vertex_count) * sizeof(render::MeshVertex) +
        static_cast<std::size_t>(index_count) * sizeof(std::uint32_t) +
        static_cast<std::size_t>(meshlet_count) * meshlet_descriptor_bytes +
        static_cast<std::size_t>(meshlet_vertex_count) * sizeof(std::uint32_t) +
        meshlet_triangle_count + static_cast<std::size_t>(page_count) * page_descriptor_bytes;
}

} // namespace

CookResult cook_v5_fixture(const MeshSource& source) {
    CookResult result = cook(source);
    if (!result.success || result.bytes.size() < v6_header_size) return result;
    const auto source_bytes = std::span<const std::byte>{result.bytes};
    if (read_u32(source_bytes, render::mesh_asset_magic_size) != render::mesh_asset_format_version_v6) {
        result.success = false;
        result.diagnostics = "v5 fixture compatibility expected a v6 cooker result.";
        result.bytes.clear();
        return result;
    }

    const std::size_t base_size = base_payload_size(source_bytes);
    if (v6_header_size + base_size > source_bytes.size()) {
        result.success = false;
        result.diagnostics = "v5 fixture compatibility could not locate the v6 base payload.";
        result.bytes.clear();
        return result;
    }
    const auto base_payload = source_bytes.subspan(v6_header_size, base_size);

    std::vector<std::byte> v5;
    v5.reserve(v5_header_size + base_size);
    v5.insert(v5.end(), source_bytes.begin(), source_bytes.begin() + render::mesh_asset_magic_size);
    append_u32(v5, render::mesh_asset_format_version_v5);
    append_u32(v5, read_u32(source_bytes, 12U));
    append_u32(v5, read_u32(source_bytes, 16U));
    const auto v5_fingerprint = fingerprint(base_payload);
    append_u64(v5, v5_fingerprint);
    v5.insert(v5.end(), source_bytes.begin() + 28U, source_bytes.begin() + 44U);
    append_u32(v5, read_u32(source_bytes, 44U));
    append_u32(v5, read_u32(source_bytes, 48U));
    append_u32(v5, read_u32(source_bytes, 52U));
    append_u32(v5, read_u32(source_bytes, 56U));
    v5.insert(v5.end(), base_payload.begin(), base_payload.end());

    const auto decoded = render::decode_mesh_asset(v5);
    if (!decoded.asset.has_value() || decoded.asset->version != render::mesh_asset_format_version_v5) {
        result.success = false;
        result.diagnostics = "v5 fixture compatibility produced an invalid legacy asset.";
        result.bytes.clear();
        return result;
    }
    result.source_fingerprint = v5_fingerprint;
    result.bytes = std::move(v5);
    result.success = true;
    result.diagnostics.clear();
    return result;
}

} // namespace vice::tools::mesh_cooker
