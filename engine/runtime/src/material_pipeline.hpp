#pragma once

#include <vice/render/rhi.hpp>
#include <vice/runtime/scene.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace vice::runtime::detail {

struct RuntimeMaterialBinding {
    render::GraphicsPipeline* pipeline = nullptr;
    render::Buffer* constants = nullptr;
    render::Texture* base_color_texture = nullptr;
    render::Sampler* sampler = nullptr;
    bool supports_directional_shadow = false;
};

class RuntimeMaterialLibrary final {
public:
    RuntimeMaterialLibrary();
    ~RuntimeMaterialLibrary();

    RuntimeMaterialLibrary(const RuntimeMaterialLibrary&) = delete;
    RuntimeMaterialLibrary& operator=(const RuntimeMaterialLibrary&) = delete;
    RuntimeMaterialLibrary(RuntimeMaterialLibrary&&) noexcept;
    RuntimeMaterialLibrary& operator=(RuntimeMaterialLibrary&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        render::TextureFormat color_format,
        std::span<const render::VertexBufferLayoutDesc> vertex_layouts,
        const std::filesystem::path& content_root,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeMaterialBinding find(world::ResourceId resource_id) const noexcept;
    [[nodiscard]] const CookedRenderMaterialCatalog& catalog() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime::detail
