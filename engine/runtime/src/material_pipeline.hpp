#pragma once

#include <vice/render/rhi.hpp>
#include <vice/runtime/scene.hpp>
#include <vice/runtime/texture.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace vice::runtime::detail {

struct RuntimeMaterialBinding {
    render::GraphicsPipeline* pipeline = nullptr;
    render::Buffer* constants = nullptr;
    render::Texture* base_color_texture = nullptr;
    render::Texture* normal_texture = nullptr;
    render::Texture* orm_texture = nullptr;
    render::Texture* emissive_texture = nullptr;
    render::Sampler* sampler = nullptr;
    std::size_t constants_size = 0U;
    std::uint32_t directional_shadow_cascade_count = 0;
    std::uint32_t shadow_texture_binding_base = 1U;
    std::uint32_t point_shadow_light_capacity = 0U;
    std::uint32_t point_shadow_texture_binding_base = 5U;
    std::uint32_t environment_source_count = 0U;
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

    [[nodiscard]] bool set_texture_residency(
        std::span<const world::ResourceId> resources,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeMaterialBinding find(world::ResourceId resource_id) const noexcept;
    [[nodiscard]] const CookedRenderMaterialCatalog& catalog() const noexcept;
    [[nodiscard]] const CookedTextureCatalog& texture_catalog() const noexcept;
    [[nodiscard]] std::uint64_t texture_residency_generation() const noexcept;
    [[nodiscard]] std::uint64_t resident_texture_bytes() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime::detail
