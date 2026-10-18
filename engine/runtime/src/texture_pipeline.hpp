#pragma once

#include <vice/render/rhi.hpp>
#include <vice/runtime/texture.hpp>
#include <vice/world/resource.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace vice::runtime::detail {

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
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] render::Texture* find(world::ResourceId resource_id) const noexcept;
    [[nodiscard]] const CookedTextureCatalog& catalog() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime::detail
