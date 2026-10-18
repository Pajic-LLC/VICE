#pragma once

#include <vice/render/texture_asset.hpp>
#include <vice/world/resource.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

struct CookedTextureRecord {
    world::ResourceId resource_id{};
    std::filesystem::path path;
    render::TextureAsset asset;
};

class CookedTextureCatalog final {
public:
    [[nodiscard]] bool load(const std::filesystem::path& content_root);
    [[nodiscard]] const CookedTextureRecord* find(world::ResourceId resource_id) const noexcept;
    [[nodiscard]] std::span<const CookedTextureRecord> records() const noexcept;
    [[nodiscard]] const std::string& diagnostics() const noexcept;

private:
    std::vector<CookedTextureRecord> records_;
    std::string diagnostics_;
};

} // namespace vice::runtime
