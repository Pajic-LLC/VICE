#include <vice/tools/render_material_cooker.hpp>

#include <vice/world/persistent_id.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace vice::tools::render_material_cooker {
namespace {

constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

[[nodiscard]] std::uint64_t fingerprint(const std::string_view text) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const unsigned char value : text) {
        hash ^= value;
        hash *= fnv_prime;
    }
    return hash;
}

[[nodiscard]] std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] bool safe_shader_source_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        path.extension() != ".hlsl") {
        return false;
    }
    for (const auto& part : path) {
        if (part == "." || part == "..") return false;
    }
    return true;
}

[[nodiscard]] bool valid_entry_point(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 128U) return false;
    const auto valid_first = [](const unsigned char character) {
        return std::isalpha(character) != 0 || character == '_';
    };
    const auto valid_rest = [](const unsigned char character) {
        return std::isalnum(character) != 0 || character == '_';
    };
    if (!valid_first(static_cast<unsigned char>(value.front()))) return false;
    return std::all_of(value.begin() + 1, value.end(), [&](const char character) {
        return valid_rest(static_cast<unsigned char>(character));
    });
}

[[nodiscard]] bool valid_color(const render::Color color) noexcept {
    return std::isfinite(color.red) && std::isfinite(color.green) &&
        std::isfinite(color.blue) && std::isfinite(color.alpha) &&
        color.red >= 0.0F && color.green >= 0.0F && color.blue >= 0.0F &&
        color.alpha >= 0.0F && color.alpha <= 1.0F;
}

} // namespace

ParseResult parse_source_manifest(const std::string_view source_text) {
    ParseResult result;
    if (source_text.empty()) {
        result.diagnostics = "Render material source is empty.";
        return result;
    }

    MaterialSource parsed;
    bool header_seen = false;
    bool id_seen = false;
    bool shader_seen = false;
    bool vertex_seen = false;
    bool fragment_seen = false;
    bool color_seen = false;
    std::istringstream source{std::string{source_text}};
    std::string line;
    std::size_t line_number = 0U;

    const auto fail = [&](const std::string& message) {
        result.diagnostics = "Render material source line " + std::to_string(line_number) + ": " + message;
    };

    while (std::getline(source, line)) {
        ++line_number;
        if (const auto comment = line.find('#'); comment != std::string::npos) line.erase(comment);
        line = trim(std::move(line));
        if (line.empty()) continue;

        std::istringstream values{line};
        std::string keyword;
        values >> keyword;
        if (!header_seen) {
            std::uint32_t version = 0U;
            if (keyword != "vice-render-material" || !(values >> version) ||
                version != render_material_source_format_version) {
                fail("expected `vice-render-material 1`.");
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

        if (keyword == "id") {
            std::string id_text;
            if (id_seen || !(values >> id_text)) {
                fail(id_seen ? "material ID is duplicated." : "material ID is missing.");
                return result;
            }
            const auto id = world::PersistentId::parse(id_text);
            if (!id.has_value() || id->is_nil()) {
                fail("material ID is invalid.");
                return result;
            }
            std::copy(id->bytes().begin(), id->bytes().end(), parsed.stable_id.begin());
            id_seen = true;
        } else if (keyword == "shader") {
            std::string shader;
            if (shader_seen || !(values >> std::quoted(shader))) {
                fail(shader_seen ? "shader source is duplicated." : "shader source path is invalid.");
                return result;
            }
            parsed.shader_source = std::filesystem::path{shader}.lexically_normal();
            if (!safe_shader_source_path(parsed.shader_source)) {
                fail("shader source must be a project-relative .hlsl path without traversal.");
                return result;
            }
            shader_seen = true;
        } else if (keyword == "vertex") {
            if (vertex_seen || !(values >> parsed.vertex_entry) || !valid_entry_point(parsed.vertex_entry)) {
                fail(vertex_seen ? "vertex entry point is duplicated." : "vertex entry point is invalid.");
                return result;
            }
            vertex_seen = true;
        } else if (keyword == "fragment") {
            if (fragment_seen || !(values >> parsed.fragment_entry) || !valid_entry_point(parsed.fragment_entry)) {
                fail(fragment_seen ? "fragment entry point is duplicated." : "fragment entry point is invalid.");
                return result;
            }
            fragment_seen = true;
        } else if (keyword == "base_color") {
            if (color_seen || !(values >> parsed.base_color.red >> parsed.base_color.green >>
                    parsed.base_color.blue >> parsed.base_color.alpha) || !valid_color(parsed.base_color)) {
                fail(color_seen ? "base color is duplicated." : "base color is invalid.");
                return result;
            }
            color_seen = true;
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

    if (!header_seen || !id_seen || !shader_seen || !vertex_seen || !fragment_seen || !color_seen) {
        result.diagnostics = "Render material source is missing one or more required fields.";
        return result;
    }
    parsed.source_fingerprint = fingerprint(source_text);
    if (parsed.source_fingerprint == 0U) parsed.source_fingerprint = 1U;
    result.source = std::move(parsed);
    return result;
}

CookResult cook(
    const MaterialSource& source,
    std::string vertex_shader_manifest,
    std::string fragment_shader_manifest
) {
    CookResult result;
    if (!render::valid_render_material_id(source.stable_id) || source.source_fingerprint == 0U ||
        !safe_shader_source_path(source.shader_source) || !valid_entry_point(source.vertex_entry) ||
        !valid_entry_point(source.fragment_entry) || !valid_color(source.base_color)) {
        result.diagnostics = "Render material source is invalid.";
        return result;
    }

    render::RenderMaterialAsset asset;
    asset.stable_id = source.stable_id;
    asset.source_fingerprint = source.source_fingerprint;
    asset.base_color = source.base_color;
    asset.vertex_shader_manifest = std::move(vertex_shader_manifest);
    asset.fragment_shader_manifest = std::move(fragment_shader_manifest);
    auto encoded = render::encode_render_material_asset(asset);
    if (!encoded) {
        result.diagnostics = encoded.diagnostics;
        return result;
    }
    result.success = true;
    result.bytes = std::move(encoded.bytes);
    return result;
}

bool write_asset(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes,
    std::string* const diagnostics
) {
    const auto fail = [&](std::string message) {
        if (diagnostics != nullptr) *diagnostics = std::move(message);
        return false;
    };
    if (path.empty() || bytes.empty()) return fail("Render material output path or payload is empty.");

    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return fail("Could not create render material output directory: " + error.message());
    }

    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        if (!output) return fail("Could not open temporary render material asset.");
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        if (!output) {
            std::filesystem::remove(temporary, error);
            return fail("Could not write temporary render material asset.");
        }
    }

    error.clear();
    if (std::filesystem::exists(path, error)) {
        if (error) {
            std::filesystem::remove(temporary, error);
            return fail("Could not inspect existing render material asset: " + error.message());
        }
        std::filesystem::remove(path, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return fail("Could not replace render material asset: " + error.message());
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return fail("Could not finalize render material asset: " + error.message());
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::tools::render_material_cooker
