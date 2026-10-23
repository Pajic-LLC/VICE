#pragma once

#include <vice/render/rhi.hpp>
#include <vice/render/virtual_texture_archive.hpp>
#include <vice/runtime/texture.hpp>
#include <vice/world/resource.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeVirtualTextureTileKey {
    world::ResourceId texture{};
    std::uint32_t mip_level = 0U;
    std::uint32_t tile_x = 0U;
    std::uint32_t tile_y = 0U;

    friend bool operator==(const RuntimeVirtualTextureTileKey&, const RuntimeVirtualTextureTileKey&) = default;
};

struct RuntimeVirtualTextureResidentTile {
    RuntimeVirtualTextureTileKey key{};
    render::VirtualTextureArchiveTile descriptor{};
    std::unique_ptr<render::Texture> texture;
    std::uint64_t last_used = 0U;
};

struct RuntimeVirtualTextureResidentPoolUpdateResult {
    std::uint32_t requested = 0U;
    std::uint32_t resident_hits = 0U;
    std::uint32_t loaded = 0U;
    std::uint32_t evicted = 0U;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

class RuntimeVirtualTextureResidentPool final {
public:
    explicit RuntimeVirtualTextureResidentPool(std::uint64_t budget_bytes);
    ~RuntimeVirtualTextureResidentPool();

    RuntimeVirtualTextureResidentPool(const RuntimeVirtualTextureResidentPool&) = delete;
    RuntimeVirtualTextureResidentPool& operator=(const RuntimeVirtualTextureResidentPool&) = delete;
    RuntimeVirtualTextureResidentPool(RuntimeVirtualTextureResidentPool&&) noexcept;
    RuntimeVirtualTextureResidentPool& operator=(RuntimeVirtualTextureResidentPool&&) noexcept;

    [[nodiscard]] RuntimeVirtualTextureResidentPoolUpdateResult update(
        render::Device& device,
        const CookedTextureCatalog& catalog,
        std::span<const RuntimeVirtualTextureTileKey> requests
    );

    [[nodiscard]] render::Texture* find(const RuntimeVirtualTextureTileKey& key) const noexcept;
    [[nodiscard]] bool resident(const RuntimeVirtualTextureTileKey& key) const noexcept;
    [[nodiscard]] std::uint64_t resident_bytes() const noexcept;
    [[nodiscard]] std::uint64_t budget_bytes() const noexcept;
    [[nodiscard]] std::span<const RuntimeVirtualTextureResidentTile> tiles() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
