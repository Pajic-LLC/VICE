#include <vice/tools/mesh_cooker.hpp>

#include <vice/render/meshlet.hpp>
#include <vice/world/persistent_id.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
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

[[nodiscard]] bool finite_legacy_vertex(const render::MeshVertex& vertex) noexcept {
    for (const float component : vertex.position) {
        if (!std::isfinite(component)) return false;
    }
    for (const float component : vertex.color) {
        if (!std::isfinite(component) || component < 0.0F || component > 1.0F) return false;
    }
    return true;
}

void normalize_normal(render::MeshVertex& vertex) noexcept {
    const float length_squared =
        vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] +
        vertex.normal[2] * vertex.normal[2];
    if (length_squared <= 1.0e-8F) return;
    const float inverse = 1.0F / std::sqrt(length_squared);
    vertex.normal[0] *= inverse;
    vertex.normal[1] *= inverse;
    vertex.normal[2] *= inverse;
}

void derive_legacy_normals(MeshSource& mesh) noexcept {
    for (auto& vertex : mesh.vertices) {
        vertex.normal[0] = 0.0F;
        vertex.normal[1] = 0.0F;
        vertex.normal[2] = 0.0F;
        vertex.uv[0] = 0.0F;
        vertex.uv[1] = 0.0F;
    }
    for (std::size_t offset = 0U; offset + 2U < mesh.indices.size(); offset += 3U) {
        const auto a = mesh.indices[offset];
        const auto b = mesh.indices[offset + 1U];
        const auto c = mesh.indices[offset + 2U];
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size()) continue;
        const auto& pa = mesh.vertices[a].position;
        const auto& pb = mesh.vertices[b].position;
        const auto& pc = mesh.vertices[c].position;
        const float abx = pb[0] - pa[0];
        const float aby = pb[1] - pa[1];
        const float abz = pb[2] - pa[2];
        const float acx = pc[0] - pa[0];
        const float acy = pc[1] - pa[1];
        const float acz = pc[2] - pa[2];
        const float nx = aby * acz - abz * acy;
        const float ny = abz * acx - abx * acz;
        const float nz = abx * acy - aby * acx;
        for (const auto index : {a, b, c}) {
            mesh.vertices[index].normal[0] += nx;
            mesh.vertices[index].normal[1] += ny;
            mesh.vertices[index].normal[2] += nz;
        }
    }
    for (auto& vertex : mesh.vertices) {
        const float length_squared =
            vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] +
            vertex.normal[2] * vertex.normal[2];
        if (length_squared <= 1.0e-8F) {
            vertex.normal[0] = 0.0F;
            vertex.normal[1] = 1.0F;
            vertex.normal[2] = 0.0F;
        } else {
            normalize_normal(vertex);
        }
    }
}

[[nodiscard]] std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

} // namespace

ParseResult parse_source_manifest(const std::string_view source) {
    ParseResult result;
    MeshSource parsed;
    std::istringstream stream{std::string{source}};
    std::string line;
    std::size_t line_number = 0U;
    bool header_seen = false;
    bool id_seen = false;

    const auto fail = [&](const std::string& message) {
        result.diagnostics = "Mesh source line " + std::to_string(line_number) + ": " + message;
    };

    while (std::getline(stream, line)) {
        ++line_number;
        if (const auto comment = line.find('#'); comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(std::move(line));
        if (line.empty()) continue;

        std::istringstream values{line};
        std::string keyword;
        values >> keyword;
        if (!header_seen) {
            std::uint32_t version = 0U;
            if (keyword != "vice-mesh-source" || !(values >> version) ||
                (version != mesh_source_format_version_v1 && version != mesh_source_format_version_v2)) {
                fail("expected `vice-mesh-source 1` or `vice-mesh-source 2`.");
                return result;
            }
            std::string trailing;
            if (values >> trailing) {
                fail("source header has trailing data.");
                return result;
            }
            parsed.version = version;
            header_seen = true;
            continue;
        }

        if (keyword == "name") {
            if (!parsed.name.empty() || !(values >> parsed.name) || parsed.name.empty()) {
                fail("invalid or duplicate mesh name.");
                return result;
            }
        } else if (keyword == "id") {
            std::string id_text;
            if (id_seen || !(values >> id_text)) {
                fail("invalid or duplicate mesh id.");
                return result;
            }
            const auto id = world::PersistentId::parse(id_text);
            if (!id.has_value() || id->is_nil()) {
                fail("mesh id must be a valid non-nil UUID.");
                return result;
            }
            parsed.stable_id = id->bytes();
            id_seen = true;
        } else if (keyword == "collision") {
            std::string mode;
            if (!(values >> mode)) {
                fail("collision requires a mode.");
                return result;
            }
            if (mode == "none") parsed.collision = CollisionMode::none;
            else if (mode == "triangle_mesh") parsed.collision = CollisionMode::triangle_mesh;
            else if (mode == "convex_hull") parsed.collision = CollisionMode::convex_hull;
            else {
                fail("unknown collision mode.");
                return result;
            }
        } else if (keyword == "vertex") {
            render::MeshVertex vertex;
            const bool base = static_cast<bool>(values >>
                vertex.position[0] >> vertex.position[1] >> vertex.position[2] >>
                vertex.color[0] >> vertex.color[1] >> vertex.color[2]);
            if (!base) {
                fail("invalid vertex.");
                return result;
            }
            if (parsed.version >= mesh_source_format_version_v2) {
                if (!(values >> vertex.normal[0] >> vertex.normal[1] >> vertex.normal[2] >>
                        vertex.uv[0] >> vertex.uv[1]) || !finite_surface_vertex(vertex)) {
                    fail("v2 vertex requires a finite non-zero normal and UV0.");
                    return result;
                }
                normalize_normal(vertex);
            } else if (!finite_legacy_vertex(vertex)) {
                fail("invalid vertex.");
                return result;
            }
            parsed.vertices.push_back(vertex);
        } else if (keyword == "triangle") {
            std::uint32_t a = 0U;
            std::uint32_t b = 0U;
            std::uint32_t c = 0U;
            if (!(values >> a >> b >> c) || a == b || b == c || a == c) {
                fail("invalid triangle.");
                return result;
            }
            parsed.indices.push_back(a);
            parsed.indices.push_back(b);
            parsed.indices.push_back(c);
        } else {
            fail("unknown keyword `" + keyword + "`.");
            return result;
        }

        std::string trailing;
        if (values >> trailing) {
            fail("statement has trailing data.");
            return result;
        }
    }

    if (!header_seen) {
        result.diagnostics = "Mesh source is missing its header.";
        return result;
    }
    if (parsed.name.empty()) {
        result.diagnostics = "Mesh source is missing a name.";
        return result;
    }
    if (parsed.vertices.size() < 3U || parsed.indices.size() < 3U ||
        parsed.indices.size() % 3U != 0U) {
        result.diagnostics = "Mesh source does not contain indexed triangle geometry.";
        return result;
    }
    for (const std::uint32_t index : parsed.indices) {
        if (index >= parsed.vertices.size()) {
            result.diagnostics = "Mesh source contains an out-of-range triangle index.";
            return result;
        }
    }
    if (parsed.version == mesh_source_format_version_v1) {
        derive_legacy_normals(parsed);
    }

    result.source = std::move(parsed);
    return result;
}

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

    constexpr std::size_t meshlet_descriptor_bytes = sizeof(std::uint32_t) * 4U + sizeof(float) * 4U;
    constexpr std::size_t page_descriptor_bytes = sizeof(std::uint32_t) * 6U + sizeof(float) * 4U;
    std::vector<std::byte> payload;
    payload.reserve(
        source.vertices.size() * sizeof(render::MeshVertex) +
        source.indices.size() * sizeof(std::uint32_t) +
        meshlets.meshlets.size() * meshlet_descriptor_bytes +
        meshlets.vertex_indices.size() * sizeof(std::uint32_t) +
        meshlets.triangle_indices.size() +
        pages.pages.size() * page_descriptor_bytes
    );
    for (const auto& vertex : source.vertices) {
        for (const float component : vertex.position) append_f32(payload, component);
        for (const float component : vertex.color) append_f32(payload, component);
        for (const float component : vertex.normal) append_f32(payload, component);
        for (const float component : vertex.uv) append_f32(payload, component);
    }
    for (const std::uint32_t index : source.indices) append_u32(payload, index);
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
    for (const std::uint32_t index : meshlets.vertex_indices) append_u32(payload, index);
    for (const std::uint8_t index : meshlets.triangle_indices) append_u8(payload, index);
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

    result.source_fingerprint = fingerprint(payload);
    constexpr std::size_t v5_header_bytes = render::mesh_asset_magic_size +
        sizeof(std::uint32_t) * 7U + sizeof(std::uint64_t) + render::mesh_stable_id_size;
    result.bytes.reserve(v5_header_bytes + payload.size());
    constexpr std::string_view magic = "VICEMESH";
    for (const char value : magic) result.bytes.push_back(static_cast<std::byte>(value));
    append_u32(result.bytes, render::mesh_asset_format_version);
    append_u32(result.bytes, static_cast<std::uint32_t>(source.vertices.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(source.indices.size()));
    append_u64(result.bytes, result.source_fingerprint);
    for (const std::uint8_t value : source.stable_id) append_u8(result.bytes, value);
    append_u32(result.bytes, static_cast<std::uint32_t>(meshlets.meshlets.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(meshlets.vertex_indices.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(meshlets.triangle_indices.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(pages.pages.size()));
    result.bytes.insert(result.bytes.end(), payload.begin(), payload.end());

    const auto decoded = render::decode_mesh_asset(result.bytes);
    if (!decoded.asset.has_value()) {
        result.bytes.clear();
        result.diagnostics = "Cooked mesh failed runtime validation: " + decoded.diagnostics;
        return result;
    }
    if (decoded.asset->meshlet_geometry != meshlets) {
        result.bytes.clear();
        result.diagnostics = "Cooked mesh cluster metadata did not round-trip deterministically.";
        return result;
    }
    if (decoded.asset->virtual_geometry_pages != pages) {
        result.bytes.clear();
        result.diagnostics = "Cooked virtual geometry page metadata did not round-trip deterministically.";
        return result;
    }

    result.success = true;
    return result;
}

bool write_asset(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes,
    std::string* diagnostics
) {
    const auto fail = [&](const std::string& message) {
        if (diagnostics != nullptr) *diagnostics = message;
        return false;
    };
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return fail("Could not create mesh asset output directory: " + error.message());
    }
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) return fail("Could not open mesh asset output: " + path.string());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) return fail("Could not write mesh asset output: " + path.string());
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::tools::mesh_cooker
