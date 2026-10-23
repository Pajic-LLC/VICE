#define decode_mesh_asset decode_mesh_asset_v1_v6_dispatch
#define load_mesh_asset load_mesh_asset_v1_v6_file
#include "mesh_asset_v6.cpp"
#undef load_mesh_asset
#undef decode_mesh_asset

#include <vice/render/virtual_geometry_stream.hpp>

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
#include <vector>

namespace vice::render {
namespace {

constexpr std::array<std::byte, 8U> stream_footer_magic{
    std::byte{'V'}, std::byte{'G'}, std::byte{'P'}, std::byte{'A'},
    std::byte{'G'}, std::byte{'E'}, std::byte{'0'}, std::byte{'7'},
};
constexpr std::size_t stream_directory_entry_bytes = 40U;
constexpr std::size_t stream_footer_bytes = 40U;
constexpr std::size_t mesh_version_offset = mesh_asset_magic_size;
constexpr std::uint64_t stream_fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t stream_fnv_prime = 1'099'511'628'211ULL;

struct StreamFooter {
    std::uint64_t core_size = 0U;
    std::uint64_t directory_offset = 0U;
    std::uint32_t page_count = 0U;
    std::uint64_t directory_fingerprint = 0U;
};

[[nodiscard]] std::uint64_t stream_fingerprint(const std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = stream_fnv_offset_basis;
    for (const auto value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= stream_fnv_prime;
    }
    return hash;
}

[[nodiscard]] bool checked_add_u64(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& output
) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) return false;
    output = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply_u64(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& output
) noexcept {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) return false;
    output = left * right;
    return true;
}

[[nodiscard]] bool read_u32_at(
    const std::span<const std::byte> bytes,
    const std::size_t offset,
    std::uint32_t& value
) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) return false;
    value = 0U;
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        value |= static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(bytes[offset + index])
        ) << (index * 8U);
    }
    return true;
}

[[nodiscard]] bool read_u64_at(
    const std::span<const std::byte> bytes,
    const std::size_t offset,
    std::uint64_t& value
) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) return false;
    value = 0U;
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        value |= static_cast<std::uint64_t>(
            std::to_integer<std::uint8_t>(bytes[offset + index])
        ) << (index * 8U);
    }
    return true;
}

void overwrite_u32_at(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value
) {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] bool parse_footer(
    const std::span<const std::byte> bytes,
    StreamFooter& footer,
    std::string& diagnostics
) {
    if (bytes.size() < stream_footer_bytes) {
        diagnostics = "Mesh asset v7 page archive footer is incomplete.";
        return false;
    }
    const auto footer_bytes = bytes.last(stream_footer_bytes);
    if (!std::equal(stream_footer_magic.begin(), stream_footer_magic.end(), footer_bytes.begin())) {
        diagnostics = "Mesh asset v7 page archive footer magic does not match.";
        return false;
    }
    std::uint32_t reserved = 0U;
    if (!read_u64_at(footer_bytes, 8U, footer.core_size) ||
        !read_u64_at(footer_bytes, 16U, footer.directory_offset) ||
        !read_u32_at(footer_bytes, 24U, footer.page_count) ||
        !read_u32_at(footer_bytes, 28U, reserved) ||
        !read_u64_at(footer_bytes, 32U, footer.directory_fingerprint)) {
        diagnostics = "Mesh asset v7 page archive footer is incomplete.";
        return false;
    }
    if (reserved != 0U || footer.page_count == 0U) {
        diagnostics = "Mesh asset v7 page archive footer contains invalid metadata.";
        return false;
    }
    return true;
}

[[nodiscard]] bool expected_page_payload_size(
    const std::uint32_t vertex_count,
    const std::uint32_t index_count,
    std::uint64_t& size
) noexcept {
    std::uint64_t vertex_bytes = 0U;
    std::uint64_t index_bytes = 0U;
    if (vertex_count == 0U || index_count == 0U || index_count % 3U != 0U ||
        !checked_multiply_u64(vertex_count, sizeof(MeshVertex), vertex_bytes) ||
        !checked_multiply_u64(index_count, sizeof(std::uint32_t), index_bytes)) {
        return false;
    }
    size = sizeof(std::uint32_t) * 2U;
    return checked_add_u64(size, vertex_bytes, size) && checked_add_u64(size, index_bytes, size);
}

[[nodiscard]] bool parse_directory(
    const std::span<const std::byte> bytes,
    const StreamFooter& footer,
    VirtualGeometryStreamDirectory& directory,
    std::string& diagnostics
) {
    const std::uint64_t footer_offset = bytes.size() - stream_footer_bytes;
    std::uint64_t directory_size = 0U;
    if (!checked_multiply_u64(footer.page_count, stream_directory_entry_bytes, directory_size) ||
        footer.core_size < mesh_asset_magic_size + sizeof(std::uint32_t) ||
        footer.core_size > footer.directory_offset ||
        footer.directory_offset > footer_offset ||
        directory_size > footer_offset - footer.directory_offset ||
        footer.directory_offset + directory_size != footer_offset) {
        diagnostics = "Mesh asset v7 page archive ranges are invalid.";
        return false;
    }

    const auto directory_bytes = bytes.subspan(
        static_cast<std::size_t>(footer.directory_offset),
        static_cast<std::size_t>(directory_size)
    );
    if (stream_fingerprint(directory_bytes) != footer.directory_fingerprint) {
        diagnostics = "Mesh asset v7 page directory fingerprint does not match.";
        return false;
    }

    directory.core_size = footer.core_size;
    directory.pages.clear();
    directory.pages.reserve(footer.page_count);
    std::uint64_t expected_payload_offset = footer.core_size;
    for (std::uint32_t page_index = 0U; page_index < footer.page_count; ++page_index) {
        const std::size_t offset = static_cast<std::size_t>(page_index) * stream_directory_entry_bytes;
        VirtualGeometryStreamPageEntry entry;
        std::uint32_t reserved = 0U;
        if (!read_u32_at(directory_bytes, offset + 0U, entry.page_index) ||
            !read_u32_at(directory_bytes, offset + 4U, entry.vertex_count) ||
            !read_u32_at(directory_bytes, offset + 8U, entry.index_count) ||
            !read_u32_at(directory_bytes, offset + 12U, reserved) ||
            !read_u64_at(directory_bytes, offset + 16U, entry.payload_offset) ||
            !read_u64_at(directory_bytes, offset + 24U, entry.payload_size) ||
            !read_u64_at(directory_bytes, offset + 32U, entry.payload_fingerprint)) {
            diagnostics = "Mesh asset v7 page directory entry is incomplete.";
            return false;
        }
        std::uint64_t expected_size = 0U;
        if (reserved != 0U || entry.page_index != page_index ||
            entry.payload_offset != expected_payload_offset ||
            !expected_page_payload_size(entry.vertex_count, entry.index_count, expected_size) ||
            entry.payload_size != expected_size || entry.payload_size > footer.directory_offset - entry.payload_offset) {
            diagnostics = "Mesh asset v7 page directory is not canonical.";
            return false;
        }
        expected_payload_offset += entry.payload_size;
        directory.pages.push_back(entry);
    }
    if (expected_payload_offset != footer.directory_offset) {
        diagnostics = "Mesh asset v7 page payloads do not exactly cover their archive range.";
        return false;
    }
    return true;
}

[[nodiscard]] bool stream_finite_vertex(const MeshVertex& vertex) noexcept {
    for (const float value : vertex.position) if (!std::isfinite(value)) return false;
    for (const float value : vertex.color) if (!std::isfinite(value)) return false;
    for (const float value : vertex.normal) if (!std::isfinite(value)) return false;
    for (const float value : vertex.uv) if (!std::isfinite(value)) return false;
    const float normal_length_squared =
        vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] +
        vertex.normal[2] * vertex.normal[2];
    return normal_length_squared > 1.0e-8F;
}

[[nodiscard]] VirtualGeometryStreamPageResult decode_page_payload(
    const std::span<const std::byte> payload,
    const VirtualGeometryStreamPageEntry& entry
) {
    VirtualGeometryStreamPageResult result;
    if (stream_fingerprint(payload) != entry.payload_fingerprint) {
        result.diagnostics = "Virtual geometry page payload fingerprint does not match.";
        return result;
    }
    std::uint32_t vertex_count = 0U;
    std::uint32_t index_count = 0U;
    if (!read_u32_at(payload, 0U, vertex_count) || !read_u32_at(payload, 4U, index_count) ||
        vertex_count != entry.vertex_count || index_count != entry.index_count) {
        result.diagnostics = "Virtual geometry page payload header does not match its directory entry.";
        return result;
    }
    std::uint64_t expected_size = 0U;
    if (!expected_page_payload_size(vertex_count, index_count, expected_size) ||
        expected_size != payload.size()) {
        result.diagnostics = "Virtual geometry page payload size is invalid.";
        return result;
    }

    VirtualGeometryStreamPage page;
    page.vertices.resize(vertex_count);
    page.indices.resize(index_count);
    std::size_t cursor = sizeof(std::uint32_t) * 2U;
    for (auto& vertex : page.vertices) {
        auto read_f32 = [&](float& value) {
            std::uint32_t bits = 0U;
            if (!read_u32_at(payload, cursor, bits)) return false;
            value = std::bit_cast<float>(bits);
            cursor += sizeof(bits);
            return true;
        };
        for (float& value : vertex.position) if (!read_f32(value)) return result;
        for (float& value : vertex.color) if (!read_f32(value)) return result;
        for (float& value : vertex.normal) if (!read_f32(value)) return result;
        for (float& value : vertex.uv) if (!read_f32(value)) return result;
        if (!stream_finite_vertex(vertex)) {
            result.diagnostics = "Virtual geometry page payload contains an invalid vertex.";
            return result;
        }
    }
    for (auto& index : page.indices) {
        if (!read_u32_at(payload, cursor, index)) {
            result.diagnostics = "Virtual geometry page payload contains an incomplete index stream.";
            return result;
        }
        cursor += sizeof(index);
        if (index >= page.vertices.size()) {
            result.diagnostics = "Virtual geometry page payload contains an out-of-range index.";
            return result;
        }
    }
    if (cursor != payload.size()) {
        result.diagnostics = "Virtual geometry page payload contains trailing data.";
        return result;
    }
    result.page = std::move(page);
    return result;
}

void append_u32_stream(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
    }
}

void append_f32_stream(std::vector<std::byte>& bytes, const float value) {
    append_u32_stream(bytes, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] std::vector<std::byte> canonical_page_payload(
    const MeshAsset& asset,
    const std::uint32_t page_index
) {
    const auto& page = asset.virtual_geometry_pages.pages[page_index];
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> remap(asset.vertices.size(), virtual_geometry_invalid_node);
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
                if (compact_index == virtual_geometry_invalid_node) {
                    compact_index = static_cast<std::uint32_t>(vertices.size());
                    vertices.push_back(asset.vertices[global_index]);
                }
                indices.push_back(compact_index);
            }
        }
    }

    std::vector<std::byte> payload;
    append_u32_stream(payload, static_cast<std::uint32_t>(vertices.size()));
    append_u32_stream(payload, static_cast<std::uint32_t>(indices.size()));
    for (const auto& vertex : vertices) {
        for (const float value : vertex.position) append_f32_stream(payload, value);
        for (const float value : vertex.color) append_f32_stream(payload, value);
        for (const float value : vertex.normal) append_f32_stream(payload, value);
        for (const float value : vertex.uv) append_f32_stream(payload, value);
    }
    for (const auto index : indices) append_u32_stream(payload, index);
    return payload;
}

[[nodiscard]] MeshAssetDecodeResult decode_v7(const std::span<const std::byte> bytes) {
    MeshAssetDecodeResult result;
    StreamFooter footer;
    if (!parse_footer(bytes, footer, result.diagnostics)) return result;
    VirtualGeometryStreamDirectory directory;
    if (!parse_directory(bytes, footer, directory, result.diagnostics)) return result;
    if (footer.core_size > bytes.size() || footer.core_size > std::numeric_limits<std::size_t>::max()) {
        result.diagnostics = "Mesh asset v7 core size exceeds the host address space.";
        return result;
    }

    std::vector<std::byte> core(bytes.begin(), bytes.begin() + static_cast<std::size_t>(footer.core_size));
    if (core.size() < mesh_version_offset + sizeof(std::uint32_t)) {
        result.diagnostics = "Mesh asset v7 core is incomplete.";
        return result;
    }
    overwrite_u32_at(core, mesh_version_offset, mesh_asset_format_version_v6);
    auto decoded = decode_mesh_asset_v1_v6_dispatch(core);
    if (!decoded.asset.has_value() || decoded.asset->version != mesh_asset_format_version_v6) {
        result.diagnostics = "Mesh asset v7 core failed v6 validation: " + decoded.diagnostics;
        return result;
    }
    if (directory.pages.size() != decoded.asset->virtual_geometry_pages.pages.size()) {
        result.diagnostics = "Mesh asset v7 page directory count does not match the v6 core page table.";
        return result;
    }

    for (const auto& entry : directory.pages) {
        const auto payload = bytes.subspan(
            static_cast<std::size_t>(entry.payload_offset),
            static_cast<std::size_t>(entry.payload_size)
        );
        const auto page = decode_page_payload(payload, entry);
        if (!page) {
            result.diagnostics = "Mesh asset v7 page " + std::to_string(entry.page_index) +
                " is invalid: " + page.diagnostics;
            return result;
        }
        const auto expected = canonical_page_payload(*decoded.asset, entry.page_index);
        if (!std::equal(expected.begin(), expected.end(), payload.begin(), payload.end())) {
            result.diagnostics = "Mesh asset v7 page payload is not the canonical authoritative leaf geometry.";
            return result;
        }
    }

    decoded.asset->version = mesh_asset_format_version_v7;
    return decoded;
}

[[nodiscard]] bool read_file_range(
    std::ifstream& input,
    const std::uint64_t offset,
    const std::uint64_t size,
    std::vector<std::byte>& output
) {
    if (size > std::numeric_limits<std::size_t>::max() ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }
    output.resize(static_cast<std::size_t>(size));
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) return false;
    input.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(size));
    return input.good() || (input.eof() && input.gcount() == static_cast<std::streamsize>(size));
}

} // namespace

MeshAssetDecodeResult decode_mesh_asset(const std::span<const std::byte> bytes) {
    if (bytes.size() < mesh_asset_magic_size + sizeof(std::uint32_t)) {
        return decode_mesh_asset_v1_v6_dispatch(bytes);
    }
    std::uint32_t version = 0U;
    if (!read_u32_at(bytes, mesh_version_offset, version) || version <= mesh_asset_format_version_v6) {
        return decode_mesh_asset_v1_v6_dispatch(bytes);
    }
    if (version != mesh_asset_format_version_v7) {
        MeshAssetDecodeResult result;
        result.diagnostics = "Mesh asset format version is not supported.";
        return result;
    }
    return decode_v7(bytes);
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

VirtualGeometryStreamDirectoryResult load_virtual_geometry_stream_directory(
    const std::filesystem::path& path
) {
    VirtualGeometryStreamDirectoryResult result;
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.diagnostics = "Could not open cooked mesh asset page archive: " + path.string();
        return result;
    }
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) < stream_footer_bytes) {
        result.diagnostics = "Cooked mesh asset is too small for a v7 page archive.";
        return result;
    }
    const std::uint64_t file_size = static_cast<std::uint64_t>(end);

    std::vector<std::byte> header;
    if (!read_file_range(input, 0U, mesh_asset_magic_size + sizeof(std::uint32_t), header)) {
        result.diagnostics = "Could not read cooked mesh asset header.";
        return result;
    }
    std::uint32_t version = 0U;
    if (!std::equal(mesh_magic.begin(), mesh_magic.end(), header.begin()) ||
        !read_u32_at(header, mesh_version_offset, version) || version != mesh_asset_format_version_v7) {
        result.diagnostics = "Cooked mesh asset does not contain a v7 page archive.";
        return result;
    }

    std::vector<std::byte> footer_bytes;
    if (!read_file_range(input, file_size - stream_footer_bytes, stream_footer_bytes, footer_bytes)) {
        result.diagnostics = "Could not read cooked mesh asset page archive footer.";
        return result;
    }
    StreamFooter footer;
    if (!parse_footer(footer_bytes, footer, result.diagnostics)) return result;

    std::uint64_t directory_size = 0U;
    if (!checked_multiply_u64(footer.page_count, stream_directory_entry_bytes, directory_size) ||
        footer.directory_offset + directory_size + stream_footer_bytes != file_size) {
        result.diagnostics = "Cooked mesh asset page directory range is invalid.";
        return result;
    }
    std::vector<std::byte> directory_bytes;
    if (!read_file_range(input, footer.directory_offset, directory_size, directory_bytes)) {
        result.diagnostics = "Could not read cooked mesh asset page directory.";
        return result;
    }

    std::vector<std::byte> synthetic;
    synthetic.reserve(static_cast<std::size_t>(directory_size + stream_footer_bytes));
    synthetic.insert(synthetic.end(), directory_bytes.begin(), directory_bytes.end());
    synthetic.insert(synthetic.end(), footer_bytes.begin(), footer_bytes.end());
    StreamFooter synthetic_footer = footer;
    synthetic_footer.core_size = 0U;
    synthetic_footer.directory_offset = 0U;
    VirtualGeometryStreamDirectory directory;
    if (stream_fingerprint(directory_bytes) != footer.directory_fingerprint) {
        result.diagnostics = "Cooked mesh asset page directory fingerprint does not match.";
        return result;
    }

    directory.core_size = footer.core_size;
    directory.pages.reserve(footer.page_count);
    std::uint64_t expected_payload_offset = footer.core_size;
    for (std::uint32_t page_index = 0U; page_index < footer.page_count; ++page_index) {
        const std::size_t offset = static_cast<std::size_t>(page_index) * stream_directory_entry_bytes;
        VirtualGeometryStreamPageEntry entry;
        std::uint32_t reserved = 0U;
        if (!read_u32_at(directory_bytes, offset + 0U, entry.page_index) ||
            !read_u32_at(directory_bytes, offset + 4U, entry.vertex_count) ||
            !read_u32_at(directory_bytes, offset + 8U, entry.index_count) ||
            !read_u32_at(directory_bytes, offset + 12U, reserved) ||
            !read_u64_at(directory_bytes, offset + 16U, entry.payload_offset) ||
            !read_u64_at(directory_bytes, offset + 24U, entry.payload_size) ||
            !read_u64_at(directory_bytes, offset + 32U, entry.payload_fingerprint)) {
            result.diagnostics = "Cooked mesh asset page directory entry is incomplete.";
            return result;
        }
        std::uint64_t expected_size = 0U;
        if (reserved != 0U || entry.page_index != page_index ||
            entry.payload_offset != expected_payload_offset ||
            !expected_page_payload_size(entry.vertex_count, entry.index_count, expected_size) ||
            expected_size != entry.payload_size) {
            result.diagnostics = "Cooked mesh asset page directory is not canonical.";
            return result;
        }
        expected_payload_offset += entry.payload_size;
        directory.pages.push_back(entry);
    }
    if (expected_payload_offset != footer.directory_offset) {
        result.diagnostics = "Cooked mesh asset page payloads do not exactly cover their archive range.";
        return result;
    }
    result.directory = std::move(directory);
    return result;
}

VirtualGeometryStreamPageResult load_virtual_geometry_stream_page(
    const std::filesystem::path& path,
    const VirtualGeometryStreamDirectory& directory,
    const std::uint32_t page_index
) {
    VirtualGeometryStreamPageResult result;
    if (page_index >= directory.pages.size() || directory.pages[page_index].page_index != page_index) {
        result.diagnostics = "Virtual geometry page request is outside the canonical stream directory.";
        return result;
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.diagnostics = "Could not open cooked mesh asset page archive: " + path.string();
        return result;
    }
    const auto& entry = directory.pages[page_index];
    std::vector<std::byte> payload;
    if (!read_file_range(input, entry.payload_offset, entry.payload_size, payload)) {
        result.diagnostics = "Could not read requested virtual geometry page payload.";
        return result;
    }
    return decode_page_payload(payload, entry);
}

VirtualGeometryStreamPageResult load_virtual_geometry_stream_page(
    const std::filesystem::path& path,
    const std::uint32_t page_index
) {
    const auto directory = load_virtual_geometry_stream_directory(path);
    if (!directory) {
        VirtualGeometryStreamPageResult result;
        result.diagnostics = directory.diagnostics;
        return result;
    }
    return load_virtual_geometry_stream_page(path, *directory.directory, page_index);
}

} // namespace vice::render
