#define cook cook_v6_archive_core
#include "mesh_cooker_v6.cpp"
#undef cook

#include <vice/render/virtual_geometry_stream.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace vice::tools::mesh_cooker {
namespace {

constexpr std::array<std::byte, 8U> stream_footer_magic{
    std::byte{'V'}, std::byte{'G'}, std::byte{'P'}, std::byte{'A'},
    std::byte{'G'}, std::byte{'E'}, std::byte{'0'}, std::byte{'7'},
};
constexpr std::size_t stream_directory_entry_bytes = 40U;
constexpr std::uint64_t stream_fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t stream_fnv_prime = 1'099'511'628'211ULL;

[[nodiscard]] std::uint64_t stream_fingerprint(const std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = stream_fnv_offset_basis;
    for (const auto value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= stream_fnv_prime;
    }
    return hash;
}

void stream_append_u32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
    }
}

void stream_append_u64(std::vector<std::byte>& bytes, const std::uint64_t value) {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
    }
}

void stream_append_f32(std::vector<std::byte>& bytes, const float value) {
    stream_append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

void stream_overwrite_u32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value
) {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] std::vector<std::byte> make_page_payload(
    const render::MeshAsset& asset,
    const std::uint32_t page_index,
    std::uint32_t& vertex_count,
    std::uint32_t& index_count
) {
    const auto& page = asset.virtual_geometry_pages.pages[page_index];
    std::vector<render::MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> remap(asset.vertices.size(), render::virtual_geometry_invalid_node);

    for (std::uint32_t meshlet_local = 0U; meshlet_local < page.meshlet_count; ++meshlet_local) {
        const auto& meshlet = asset.meshlet_geometry.meshlets[page.meshlet_offset + meshlet_local];
        for (std::uint32_t triangle = 0U; triangle < meshlet.triangle_count; ++triangle) {
            for (std::uint32_t corner = 0U; corner < 3U; ++corner) {
                const auto local_index = asset.meshlet_geometry.triangle_indices[
                    static_cast<std::size_t>(meshlet.triangle_offset) + triangle * 3U + corner
                ];
                const auto global_index = asset.meshlet_geometry.vertex_indices[
                    static_cast<std::size_t>(meshlet.vertex_offset) + local_index
                ];
                auto& compact_index = remap[global_index];
                if (compact_index == render::virtual_geometry_invalid_node) {
                    compact_index = static_cast<std::uint32_t>(vertices.size());
                    vertices.push_back(asset.vertices[global_index]);
                }
                indices.push_back(compact_index);
            }
        }
    }

    vertex_count = static_cast<std::uint32_t>(vertices.size());
    index_count = static_cast<std::uint32_t>(indices.size());
    std::vector<std::byte> payload;
    payload.reserve(
        sizeof(std::uint32_t) * 2U + vertices.size() * sizeof(render::MeshVertex) +
        indices.size() * sizeof(std::uint32_t)
    );
    stream_append_u32(payload, vertex_count);
    stream_append_u32(payload, index_count);
    for (const auto& vertex : vertices) {
        for (const float value : vertex.position) stream_append_f32(payload, value);
        for (const float value : vertex.color) stream_append_f32(payload, value);
        for (const float value : vertex.normal) stream_append_f32(payload, value);
        for (const float value : vertex.uv) stream_append_f32(payload, value);
    }
    for (const auto index : indices) stream_append_u32(payload, index);
    return payload;
}

} // namespace

CookResult cook(const MeshSource& source) {
    auto result = cook_v6_archive_core(source);
    if (!result.success) return result;

    const auto core_decoded = render::decode_mesh_asset(result.bytes);
    if (!core_decoded.asset.has_value() ||
        core_decoded.asset->version != render::mesh_asset_format_version_v6 ||
        core_decoded.asset->virtual_geometry_pages.pages.empty()) {
        result.success = false;
        result.bytes.clear();
        result.diagnostics = "Cooked v6 core could not seed the v7 virtual geometry page archive.";
        return result;
    }
    const auto& asset = *core_decoded.asset;
    if (asset.virtual_geometry_pages.pages.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.success = false;
        result.bytes.clear();
        result.diagnostics = "Virtual geometry page count exceeds the v7 stream directory contract.";
        return result;
    }

    const std::uint64_t core_size = result.bytes.size();
    std::vector<render::VirtualGeometryStreamPageEntry> entries;
    entries.reserve(asset.virtual_geometry_pages.pages.size());
    for (std::uint32_t page_index = 0U;
         page_index < asset.virtual_geometry_pages.pages.size();
         ++page_index) {
        std::uint32_t vertex_count = 0U;
        std::uint32_t index_count = 0U;
        auto payload = make_page_payload(asset, page_index, vertex_count, index_count);
        if (vertex_count == 0U || index_count == 0U || index_count % 3U != 0U) {
            result.success = false;
            result.bytes.clear();
            result.diagnostics = "Virtual geometry page produced an empty or invalid stream payload.";
            return result;
        }
        const std::uint64_t payload_offset = result.bytes.size();
        const std::uint64_t payload_size = payload.size();
        entries.push_back(render::VirtualGeometryStreamPageEntry{
            .page_index = page_index,
            .vertex_count = vertex_count,
            .index_count = index_count,
            .payload_offset = payload_offset,
            .payload_size = payload_size,
            .payload_fingerprint = stream_fingerprint(payload),
        });
        result.bytes.insert(result.bytes.end(), payload.begin(), payload.end());
    }

    const std::uint64_t directory_offset = result.bytes.size();
    std::vector<std::byte> directory;
    directory.reserve(entries.size() * stream_directory_entry_bytes);
    for (const auto& entry : entries) {
        stream_append_u32(directory, entry.page_index);
        stream_append_u32(directory, entry.vertex_count);
        stream_append_u32(directory, entry.index_count);
        stream_append_u32(directory, 0U);
        stream_append_u64(directory, entry.payload_offset);
        stream_append_u64(directory, entry.payload_size);
        stream_append_u64(directory, entry.payload_fingerprint);
    }
    const auto directory_fingerprint = stream_fingerprint(directory);
    result.bytes.insert(result.bytes.end(), directory.begin(), directory.end());

    result.bytes.insert(result.bytes.end(), stream_footer_magic.begin(), stream_footer_magic.end());
    stream_append_u64(result.bytes, core_size);
    stream_append_u64(result.bytes, directory_offset);
    stream_append_u32(result.bytes, static_cast<std::uint32_t>(entries.size()));
    stream_append_u32(result.bytes, 0U);
    stream_append_u64(result.bytes, directory_fingerprint);
    stream_overwrite_u32(
        result.bytes,
        render::mesh_asset_magic_size,
        render::mesh_asset_format_version_v7
    );

    const auto decoded = render::decode_mesh_asset(result.bytes);
    if (!decoded.asset.has_value() || decoded.asset->version != render::mesh_asset_format_version_v7 ||
        decoded.asset->meshlet_geometry != asset.meshlet_geometry ||
        decoded.asset->virtual_geometry_pages != asset.virtual_geometry_pages ||
        decoded.asset->virtual_geometry_proxies != asset.virtual_geometry_proxies) {
        result.success = false;
        result.bytes.clear();
        result.diagnostics = "Cooked v7 page archive did not round-trip deterministically.";
        return result;
    }

    result.success = true;
    result.diagnostics.clear();
    return result;
}

} // namespace vice::tools::mesh_cooker
