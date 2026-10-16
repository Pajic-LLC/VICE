#include <vice/tools/mesh_cooker.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
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

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
    }
}

void append_u64(std::vector<std::byte>& bytes, const std::uint64_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
    }
}

void append_f32(std::vector<std::byte>& bytes, const float value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] bool finite_vertex(const render::MeshVertex& vertex) noexcept {
    for (const float component : vertex.position) {
        if (!std::isfinite(component)) return false;
    }
    for (const float component : vertex.color) {
        if (!std::isfinite(component) || component < 0.0F || component > 1.0F) return false;
    }
    return true;
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
    std::size_t line_number = 0;
    bool header_seen = false;

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
            std::uint32_t version = 0;
            if (keyword != "vice-mesh-source" || !(values >> version) ||
                version != render::mesh_asset_format_version) {
                fail("expected `vice-mesh-source 1`.");
                return result;
            }
            std::string trailing;
            if (values >> trailing) {
                fail("source header has trailing data.");
                return result;
            }
            header_seen = true;
            continue;
        }

        if (keyword == "name") {
            if (!parsed.name.empty() || !(values >> parsed.name) || parsed.name.empty()) {
                fail("invalid or duplicate mesh name.");
                return result;
            }
        } else if (keyword == "collision") {
            std::string mode;
            if (!(values >> mode)) {
                fail("collision requires a mode.");
                return result;
            }
            if (mode == "none") {
                parsed.collision = CollisionMode::none;
            } else if (mode == "triangle_mesh") {
                parsed.collision = CollisionMode::triangle_mesh;
            } else if (mode == "convex_hull") {
                parsed.collision = CollisionMode::convex_hull;
            } else {
                fail("unknown collision mode.");
                return result;
            }
        } else if (keyword == "vertex") {
            render::MeshVertex vertex;
            if (!(values >> vertex.position[0] >> vertex.position[1] >> vertex.position[2] >>
                    vertex.color[0] >> vertex.color[1] >> vertex.color[2]) ||
                !finite_vertex(vertex)) {
                fail("invalid vertex.");
                return result;
            }
            parsed.vertices.push_back(vertex);
        } else if (keyword == "triangle") {
            std::uint32_t a = 0;
            std::uint32_t b = 0;
            std::uint32_t c = 0;
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
    for (const auto& vertex : source.vertices) {
        if (!finite_vertex(vertex)) {
            result.diagnostics = "Mesh source contains an invalid vertex.";
            return result;
        }
    }
    for (std::size_t offset = 0; offset < source.indices.size(); offset += 3U) {
        const auto a = source.indices[offset];
        const auto b = source.indices[offset + 1U];
        const auto c = source.indices[offset + 2U];
        if (a >= source.vertices.size() || b >= source.vertices.size() ||
            c >= source.vertices.size() || a == b || b == c || a == c) {
            result.diagnostics = "Mesh source contains invalid triangle indices.";
            return result;
        }
    }

    std::vector<std::byte> payload;
    payload.reserve(source.vertices.size() * sizeof(render::MeshVertex) +
        source.indices.size() * sizeof(std::uint32_t));
    for (const auto& vertex : source.vertices) {
        for (const float component : vertex.position) append_f32(payload, component);
        for (const float component : vertex.color) append_f32(payload, component);
    }
    for (const std::uint32_t index : source.indices) append_u32(payload, index);

    result.source_fingerprint = fingerprint(payload);
    result.bytes.reserve(render::mesh_asset_magic_size + sizeof(std::uint32_t) * 3U +
        sizeof(std::uint64_t) + payload.size());
    constexpr std::string_view magic = "VICEMESH";
    for (const char value : magic) result.bytes.push_back(static_cast<std::byte>(value));
    append_u32(result.bytes, render::mesh_asset_format_version);
    append_u32(result.bytes, static_cast<std::uint32_t>(source.vertices.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(source.indices.size()));
    append_u64(result.bytes, result.source_fingerprint);
    result.bytes.insert(result.bytes.end(), payload.begin(), payload.end());

    const auto decoded = render::decode_mesh_asset(result.bytes);
    if (!decoded.asset.has_value()) {
        result.bytes.clear();
        result.diagnostics = "Cooked mesh failed runtime validation: " + decoded.diagnostics;
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
