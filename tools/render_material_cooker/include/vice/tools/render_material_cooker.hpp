#pragma once

#include <vice/render/material_asset.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vice::tools::render_material_cooker {

inline constexpr std::uint32_t render_material_source_format_version_v1 = 1U;
inline constexpr std::uint32_t render_material_source_format_version_v2 = 2U;
inline constexpr std::uint32_t render_material_source_format_version =
    render_material_source_format_version_v2;

struct MaterialSource {
    std::uint32_t version = render_material_source_format_version;
    render::RenderMaterialStableId stable_id{};
    std::filesystem::path shader_source;
    std::string vertex_entry;
    std::string fragment_entry;
    render::Color base_color{1.0F, 1.0F, 1.0F, 1.0F};
    float metallic = 0.0F;
    float roughness = 1.0F;
    render::TextureStableId base_color_texture{};
    render::Filter min_filter = render::Filter::linear;
    render::Filter mag_filter = render::Filter::linear;
    render::Filter mip_filter = render::Filter::linear;
    render::AddressMode address_u = render::AddressMode::repeat;
    render::AddressMode address_v = render::AddressMode::repeat;
    render::AddressMode address_w = render::AddressMode::repeat;
    std::uint32_t max_anisotropy = 1U;
    std::uint64_t source_fingerprint = 0U;
};

struct ParseResult {
    std::optional<MaterialSource> source;
    std::string diagnostics;
};

struct CookResult {
    bool success = false;
    std::vector<std::byte> bytes;
    std::string diagnostics;
};

[[nodiscard]] ParseResult parse_source_manifest(std::string_view source);

[[nodiscard]] CookResult cook(
    const MaterialSource& source,
    std::string vertex_shader_manifest,
    std::string fragment_shader_manifest
);

[[nodiscard]] bool write_asset(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes,
    std::string* diagnostics = nullptr
);

} // namespace vice::tools::render_material_cooker
