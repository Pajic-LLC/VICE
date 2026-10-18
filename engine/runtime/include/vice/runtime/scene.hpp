#pragma once

#include <vice/math/math.hpp>
#include <vice/platform/window.hpp>
#include <vice/render/material_asset.hpp>
#include <vice/render/mesh_asset.hpp>
#include <vice/render/rhi.hpp>
#include <vice/world/world.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

struct CookedMeshRecord {
    world::ResourceId resource_id{};
    std::filesystem::path path;
    render::MeshAsset asset;
};

class CookedMeshCatalog final {
public:
    [[nodiscard]] bool load(const std::filesystem::path& content_root);
    [[nodiscard]] const CookedMeshRecord* find(world::ResourceId resource_id) const noexcept;
    [[nodiscard]] std::span<const CookedMeshRecord> records() const noexcept;
    [[nodiscard]] const std::string& diagnostics() const noexcept;

private:
    std::vector<CookedMeshRecord> records_;
    std::string diagnostics_;
};

struct CookedRenderMaterialRecord {
    world::ResourceId resource_id{};
    std::filesystem::path path;
    render::RenderMaterialAsset asset;
};

class CookedRenderMaterialCatalog final {
public:
    [[nodiscard]] bool load(const std::filesystem::path& content_root);
    [[nodiscard]] const CookedRenderMaterialRecord* find(world::ResourceId resource_id) const noexcept;
    [[nodiscard]] std::span<const CookedRenderMaterialRecord> records() const noexcept;
    [[nodiscard]] const std::string& diagnostics() const noexcept;

private:
    std::vector<CookedRenderMaterialRecord> records_;
    std::string diagnostics_;
};

struct RenderMeshInstance {
    world::PersistentId entity{};
    world::ResourceId mesh{};
    std::optional<world::ResourceId> material;
    world::SpatialState spatial{};
    world::AuthoredTransform authored{};
};

struct RuntimeCameraView {
    world::PersistentId entity{};
    world::Camera camera{};
    math::Mat4 view_projection{};
};

[[nodiscard]] std::vector<RenderMeshInstance> collect_render_mesh_instances(
    const world::World& world
);

[[nodiscard]] math::Mat4 runtime_model_matrix(const RenderMeshInstance& instance) noexcept;

[[nodiscard]] std::optional<RuntimeCameraView> primary_camera_view(
    const world::World& world,
    float aspect_ratio
) noexcept;

class RuntimeSceneRenderer final {
public:
    RuntimeSceneRenderer();
    ~RuntimeSceneRenderer();

    RuntimeSceneRenderer(const RuntimeSceneRenderer&) = delete;
    RuntimeSceneRenderer& operator=(const RuntimeSceneRenderer&) = delete;
    RuntimeSceneRenderer(RuntimeSceneRenderer&&) noexcept;
    RuntimeSceneRenderer& operator=(RuntimeSceneRenderer&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        render::TextureFormat color_format,
        const std::filesystem::path& content_root,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool resize(
        platform::WindowMetrics metrics,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool render(
        const world::World& world,
        render::Swapchain& swapchain,
        platform::WindowMetrics metrics,
        render::Color clear_color,
        std::string* diagnostics = nullptr
    );

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
