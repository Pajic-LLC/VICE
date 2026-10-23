#pragma once

#include <vice/render/rhi.hpp>
#include <vice/runtime/texture.hpp>
#include <vice/world/resource.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace vice::runtime::detail {

enum class RuntimeMaterialTextureChannel : std::uint8_t {
    base_color,
    normal,
    orm,
    emissive,
};

class RuntimeTextureLibrary final {
public:
    RuntimeTextureLibrary();
    ~RuntimeTextureLibrary();

    RuntimeTextureLibrary(const RuntimeTextureLibrary&) = delete;
    RuntimeTextureLibrary& operator=(const RuntimeTextureLibrary&) = delete;
    RuntimeTextureLibrary(RuntimeTextureLibrary&&) noexcept;
    RuntimeTextureLibrary& operator=(RuntimeTextureLibrary&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        const std::filesystem::path& content_root,
        std::string* diagnostics = nullptr,
        bool eager_upload = true
    );

    [[nodiscard]] bool set_residency(
        std::span<const world::ResourceId> resources,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] render::Texture* find(world::ResourceId resource_id) const noexcept;
    [[nodiscard]] render::Texture* resolve(
        world::ResourceId resource_id,
        RuntimeMaterialTextureChannel channel
    ) const noexcept;
    [[nodiscard]] bool resident(world::ResourceId resource_id) const noexcept;
    [[nodiscard]] std::uint64_t resident_bytes() const noexcept;
    [[nodiscard]] std::uint64_t residency_generation() const noexcept;
    [[nodiscard]] const CookedTextureCatalog& catalog() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime::detail
