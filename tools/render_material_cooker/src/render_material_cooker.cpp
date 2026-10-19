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

[[nodiscard]] std::optional<render::Filter> parse_filter(const std::string_view value) noexcept {
    if (value == "nearest") return render::Filter::nearest;
    if (value == "linear") return render::Filter::linear;
    return std::nullopt;
}

[[nodiscard]] std::optional<render::AddressMode> parse_address(const std::string_view value) noexcept {
    if (value == "clamp") return render::AddressMode::clamp_to_edge;
    if (value == "repeat") return render::AddressMode::repeat;
    if (value == "mirror") return render::AddressMode::mirrored_repeat;
    return std::nullopt;
}

[[nodiscard]] bool valid_v2_pbr(const MaterialSource& source) noexcept {
    if (!std::isfinite(source.metallic) || !std::isfinite(source.roughness) ||
        source.metallic < 0.0F || source.metallic > 1.0F ||
        source.roughness < 0.0F || source.roughness > 1.0F ||
        !render::valid_texture_id(source.base_color_texture)) {
        return false;
    }
    render::SamplerDesc sampler;
    sampler.min_filter = source.min_filter;
    sampler.mag_filter = source.mag_filter;
    sampler.mip_filter = source.mip_filter;
    sampler.address_u = source.address_u;
    sampler.address_v = source.address_v;
    sampler.address_w = source.address_w;
    sampler.max_anisotropy = source.max_anisotropy;
    return render::is_valid(sampler);
}

[[nodiscard]] bool valid_v3_pbr(const MaterialSource& source) noexcept {
    return valid_v2_pbr(source) && render::valid_texture_id(source.normal_texture) &&
        render::valid_texture_id(source.orm_texture) && render::valid_texture_id(source.emissive_texture) &&
        valid_color(source.emissive_color) && std::isfinite(source.normal_scale) &&
        source.normal_scale >= 0.0F && std::isfinite(source.emissive_intensity) &&
        source.emissive_intensity >= 0.0F;
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
    bool metallic_seen = false;
    bool roughness_seen = false;
    bool texture_seen = false;
    bool normal_texture_seen = false;
    bool orm_texture_seen = false;
    bool emissive_texture_seen = false;
    bool normal_scale_seen = false;
    bool emissive_color_seen = false;
    bool emissive_intensity_seen = false;
    bool sampler_seen = false;
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
                (version != render_material_source_format_version_v1 &&
                    version != render_material_source_format_version_v2 &&
                    version != render_material_source_format_version_v3)) {
                fail("expected `vice-render-material 1`, `vice-render-material 2`, or `vice-render-material 3`.");
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
        } else if (keyword == "metallic") {
            if (metallic_seen || !(values >> parsed.metallic) || !std::isfinite(parsed.metallic) ||
                parsed.metallic < 0.0F || parsed.metallic > 1.0F) {
                fail(metallic_seen ? "metallic is duplicated." : "metallic must be between 0 and 1.");
                return result;
            }
            metallic_seen = true;
        } else if (keyword == "roughness") {
            if (roughness_seen || !(values >> parsed.roughness) || !std::isfinite(parsed.roughness) ||
                parsed.roughness < 0.0F || parsed.roughness > 1.0F) {
                fail(roughness_seen ? "roughness is duplicated." : "roughness must be between 0 and 1.");
                return result;
            }
            roughness_seen = true;
        } else if (keyword == "base_color_texture") {
            std::string id_text;
            if (texture_seen || !(values >> id_text)) {
                fail(texture_seen ? "base-color texture is duplicated." : "base-color texture ID is missing.");
                return result;
            }
            const auto id = world::PersistentId::parse(id_text);
            if (!id.has_value() || id->is_nil()) {
                fail("base-color texture ID is invalid.");
                return result;
            }
            std::copy(id->bytes().begin(), id->bytes().end(), parsed.base_color_texture.begin());
            texture_seen = true;
        } else if (keyword == "normal_texture") {
            std::string id_text;
            if (parsed.version < render_material_source_format_version_v3) {
                fail("normal_texture requires render material version 3.");
                return result;
            }
            if (normal_texture_seen || !(values >> id_text)) {
                fail(normal_texture_seen ? "normal texture is duplicated." : "normal texture ID is missing.");
                return result;
            }
            const auto id = world::PersistentId::parse(id_text);
            if (!id.has_value() || id->is_nil()) {
                fail("normal texture ID is invalid.");
                return result;
            }
            std::copy(id->bytes().begin(), id->bytes().end(), parsed.normal_texture.begin());
            normal_texture_seen = true;
        } else if (keyword == "orm_texture") {
            std::string id_text;
            if (parsed.version < render_material_source_format_version_v3) {
                fail("orm_texture requires render material version 3.");
                return result;
            }
            if (orm_texture_seen || !(values >> id_text)) {
                fail(orm_texture_seen ? "ORM texture is duplicated." : "ORM texture ID is missing.");
                return result;
            }
            const auto id = world::PersistentId::parse(id_text);
            if (!id.has_value() || id->is_nil()) {
                fail("ORM texture ID is invalid.");
                return result;
            }
            std::copy(id->bytes().begin(), id->bytes().end(), parsed.orm_texture.begin());
            orm_texture_seen = true;
        } else if (keyword == "emissive_texture") {
            std::string id_text;
            if (parsed.version < render_material_source_format_version_v3) {
                fail("emissive_texture requires render material version 3.");
                return result;
            }
            if (emissive_texture_seen || !(values >> id_text)) {
                fail(emissive_texture_seen ? "emissive texture is duplicated." : "emissive texture ID is missing.");
                return result;
            }
            const auto id = world::PersistentId::parse(id_text);
            if (!id.has_value() || id->is_nil()) {
                fail("emissive texture ID is invalid.");
                return result;
            }
            std::copy(id->bytes().begin(), id->bytes().end(), parsed.emissive_texture.begin());
            emissive_texture_seen = true;
        } else if (keyword == "normal_scale") {
            if (parsed.version < render_material_source_format_version_v3) {
                fail("normal_scale requires render material version 3.");
                return result;
            }
            if (normal_scale_seen || !(values >> parsed.normal_scale) || !std::isfinite(parsed.normal_scale) ||
                parsed.normal_scale < 0.0F) {
                fail(normal_scale_seen ? "normal scale is duplicated." : "normal scale must be finite and non-negative.");
                return result;
            }
            normal_scale_seen = true;
        } else if (keyword == "emissive_color") {
            if (parsed.version < render_material_source_format_version_v3) {
                fail("emissive_color requires render material version 3.");
                return result;
            }
            parsed.emissive_color.alpha = 1.0F;
            if (emissive_color_seen || !(values >> parsed.emissive_color.red >> parsed.emissive_color.green >>
                    parsed.emissive_color.blue) || !valid_color(parsed.emissive_color)) {
                fail(emissive_color_seen ? "emissive color is duplicated." : "emissive color is invalid.");
                return result;
            }
            emissive_color_seen = true;
        } else if (keyword == "emissive_intensity") {
            if (parsed.version < render_material_source_format_version_v3) {
                fail("emissive_intensity requires render material version 3.");
                return result;
            }
            if (emissive_intensity_seen || !(values >> parsed.emissive_intensity) ||
                !std::isfinite(parsed.emissive_intensity) || parsed.emissive_intensity < 0.0F) {
                fail(emissive_intensity_seen ? "emissive intensity is duplicated." :
                    "emissive intensity must be finite and non-negative.");
                return result;
            }
            emissive_intensity_seen = true;
        } else if (keyword == "sampler") {
            std::string min_filter;
            std::string mag_filter;
            std::string mip_filter;
            std::string address_u;
            std::string address_v;
            std::string address_w;
            std::uint32_t anisotropy = 0U;
            if (sampler_seen || !(values >> min_filter >> mag_filter >> mip_filter >>
                    address_u >> address_v >> address_w >> anisotropy)) {
                fail(sampler_seen ? "sampler is duplicated." : "sampler data is invalid.");
                return result;
            }
            const auto parsed_min = parse_filter(min_filter);
            const auto parsed_mag = parse_filter(mag_filter);
            const auto parsed_mip = parse_filter(mip_filter);
            const auto parsed_u = parse_address(address_u);
            const auto parsed_v = parse_address(address_v);
            const auto parsed_w = parse_address(address_w);
            if (!parsed_min.has_value() || !parsed_mag.has_value() || !parsed_mip.has_value() ||
                !parsed_u.has_value() || !parsed_v.has_value() || !parsed_w.has_value()) {
                fail("sampler uses an unknown filter or address mode.");
                return result;
            }
            parsed.min_filter = *parsed_min;
            parsed.mag_filter = *parsed_mag;
            parsed.mip_filter = *parsed_mip;
            parsed.address_u = *parsed_u;
            parsed.address_v = *parsed_v;
            parsed.address_w = *parsed_w;
            parsed.max_anisotropy = anisotropy;
            render::SamplerDesc sampler;
            sampler.min_filter = parsed.min_filter;
            sampler.mag_filter = parsed.mag_filter;
            sampler.mip_filter = parsed.mip_filter;
            sampler.address_u = parsed.address_u;
            sampler.address_v = parsed.address_v;
            sampler.address_w = parsed.address_w;
            sampler.max_anisotropy = parsed.max_anisotropy;
            if (!render::is_valid(sampler)) {
                fail("sampler settings are not valid for VICE.");
                return result;
            }
            sampler_seen = true;
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
    if (parsed.version >= render_material_source_format_version_v2 &&
        (!metallic_seen || !roughness_seen || !texture_seen || !sampler_seen || !valid_v2_pbr(parsed))) {
        result.diagnostics = "Render material v2+ requires metallic, roughness, base_color_texture, and sampler.";
        return result;
    }
    if (parsed.version >= render_material_source_format_version_v3 &&
        (!normal_texture_seen || !orm_texture_seen || !emissive_texture_seen || !normal_scale_seen ||
            !emissive_color_seen || !emissive_intensity_seen || !valid_v3_pbr(parsed))) {
        result.diagnostics = "Render material v3 requires normal/ORM/emissive textures, normal scale, emissive color, and emissive intensity.";
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
    if ((source.version != render_material_source_format_version_v1 &&
            source.version != render_material_source_format_version_v2 &&
            source.version != render_material_source_format_version_v3) ||
        !render::valid_render_material_id(source.stable_id) || source.source_fingerprint == 0U ||
        !safe_shader_source_path(source.shader_source) || !valid_entry_point(source.vertex_entry) ||
        !valid_entry_point(source.fragment_entry) || !valid_color(source.base_color) ||
        (source.version >= render_material_source_format_version_v2 && !valid_v2_pbr(source)) ||
        (source.version >= render_material_source_format_version_v3 && !valid_v3_pbr(source))) {
        result.diagnostics = "Render material source is invalid.";
        return result;
    }

    render::RenderMaterialAsset asset;
    asset.version = source.version;
    asset.stable_id = source.stable_id;
    asset.source_fingerprint = source.source_fingerprint;
    asset.base_color = source.base_color;
    asset.metallic = source.metallic;
    asset.roughness = source.roughness;
    asset.base_color_texture = source.base_color_texture;
    asset.normal_texture = source.normal_texture;
    asset.orm_texture = source.orm_texture;
    asset.emissive_texture = source.emissive_texture;
    asset.emissive_color = source.emissive_color;
    asset.emissive_intensity = source.emissive_intensity;
    asset.normal_scale = source.normal_scale;
    asset.min_filter = source.min_filter;
    asset.mag_filter = source.mag_filter;
    asset.mip_filter = source.mip_filter;
    asset.address_u = source.address_u;
    asset.address_v = source.address_v;
    asset.address_w = source.address_w;
    asset.max_anisotropy = source.max_anisotropy;
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
