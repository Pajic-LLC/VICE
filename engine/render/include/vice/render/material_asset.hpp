#pragma once

#include <vice/render/rhi.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::render {

inline constexpr std::uint32_t render_material_asset_format_version = 1U;
inline constexpr std::size_t render_material_asset_magic_size = 8U;
inline constexpr std::size_t render_material_stable_id_size = 16U;

using RenderMaterialStableId = std::array<std::uint8_t, render_material_stable_id_size>;

[[nodiscard]] constexpr bool valid_render_material_id(const RenderMaterialStableId& id) noexcept {
    for (const std::uint8_t value : id) {
        if (value != 0U) return true;
    }
    return false;
}

struct RenderMaterialAsset {
    std::uint32_t version = render_material_asset_format_version;
    RenderMaterialStableId stable_id{};
    std::uint64_t source_fingerprint = 0U;
    Color base_color{1.0F, 1.0F, 1.0F, 1.0F};
    std::string vertex_shader_manifest;
    std::string fragment_shader_manifest;
};

struct RenderMaterialEncodeResult {
    std::vector<std::byte> bytes;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty() && !bytes.empty();
    }
};

struct RenderMaterialDecodeResult {
    std::optional<RenderMaterialAsset> asset;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return asset.has_value();
    }
};

[[nodiscard]] RenderMaterialEncodeResult encode_render_material_asset(
    const RenderMaterialAsset& asset
);

[[nodiscard]] RenderMaterialDecodeResult decode_render_material_asset(
    std::span<const std::byte> bytes
);

[[nodiscard]] RenderMaterialDecodeResult load_render_material_asset(
    const std::filesystem::path& path
);

} // namespace vice::render
