#pragma once

#include <vice/render/rhi.hpp>
#include <vice/runtime/gpu_material_table.hpp>
#include <vice/runtime/texture.hpp>
#include <vice/runtime/virtual_texture_streaming.hpp>
#include <vice/world/resource.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

inline constexpr std::uint32_t runtime_virtual_texture_invalid_index =
    std::numeric_limits<std::uint32_t>::max();

struct RuntimeGpuVirtualTextureDescriptor {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t mip_count = 0U;
    std::uint32_t first_page = 0U;

    friend bool operator==(const RuntimeGpuVirtualTextureDescriptor&, const RuntimeGpuVirtualTextureDescriptor&) = default;
};

static_assert(sizeof(RuntimeGpuVirtualTextureDescriptor) == 16U);

struct RuntimeGpuVirtualTexturePage {
    std::uint32_t physical_slot = runtime_virtual_texture_invalid_index;
    std::uint32_t resolved_mip = 0U;
    std::uint32_t resolved_tile_x = 0U;
    std::uint32_t resolved_tile_y = 0U;

    friend bool operator==(const RuntimeGpuVirtualTexturePage&, const RuntimeGpuVirtualTexturePage&) = default;
};

static_assert(sizeof(RuntimeGpuVirtualTexturePage) == 16U);

struct RuntimeVirtualTextureSamplingPlan {
    std::vector<world::ResourceId> texture_ids;
    std::vector<RuntimeGpuVirtualTextureDescriptor> textures;
    std::vector<RuntimeGpuVirtualTexturePage> pages;
    std::vector<RuntimeVirtualTextureTileKey> physical_keys;
    std::vector<render::Texture*> physical_textures;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }

    [[nodiscard]] std::optional<std::uint32_t> texture_index(
        world::ResourceId texture
    ) const noexcept;
};

[[nodiscard]] RuntimeVirtualTextureSamplingPlan build_runtime_virtual_texture_sampling_plan(
    const CookedTextureCatalog& catalog,
    const RuntimeVirtualTextureResidentPool& resident_pool,
    std::span<const world::ResourceId> textures
);

[[nodiscard]] std::uint32_t runtime_virtual_texture_page_index(
    const RuntimeGpuVirtualTextureDescriptor& descriptor,
    std::uint32_t mip_level,
    std::uint32_t tile_x,
    std::uint32_t tile_y
) noexcept;

[[nodiscard]] bool apply_runtime_virtual_texture_sampling(
    RuntimeGpuMaterialTablePlan& materials,
    const RuntimeVirtualTextureSamplingPlan& virtual_textures,
    std::string* diagnostics = nullptr
);

} // namespace vice::runtime
