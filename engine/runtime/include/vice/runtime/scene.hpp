#pragma once

#include <vice/math/math.hpp>
#include <vice/platform/window.hpp>
#include <vice/render/material_asset.hpp>
#include <vice/render/mesh_asset.hpp>
#include <vice/render/rhi.hpp>
#include <vice/world/world.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
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
    math::Vec3 position{};
    math::Vec3 forward{0.0F, 0.0F, -1.0F};
    math::Vec3 up{0.0F, 1.0F, 0.0F};
    float aspect_ratio = 1.0F;
    math::Mat4 view_projection{};
};

inline constexpr std::size_t max_runtime_lights = 8U;
inline constexpr std::size_t runtime_shadow_cascade_count = 4U;
inline constexpr std::uint32_t runtime_shadow_map_size = 2048U;
inline constexpr float runtime_shadow_distance = 250.0F;
inline constexpr float runtime_shadow_split_lambda = 0.65F;
inline constexpr std::size_t runtime_local_reflection_probe_count = 2U;

struct RuntimeLight {
    world::PersistentId entity{};
    world::Light light{};
    math::Vec3 position{};
    math::Vec3 direction{};
};

struct RuntimeReflectionProbe {
    world::PersistentId entity{};
    world::ReflectionProbe probe{};
    world::ResourceId environment{};
    world::GlobalPosition position{};
    world::AuthoredTransform authored{};
};

struct RuntimeReflectionBlend {
    std::array<std::optional<world::ResourceId>, runtime_local_reflection_probe_count> local_environments{};
    std::array<float, runtime_local_reflection_probe_count> local_weights{};
    float global_weight = 1.0F;
};

struct RuntimeShadowCascade {
    float split_near = 0.0F;
    float split_far = 0.0F;
    math::Mat4 view_projection{};
};

struct RuntimeShadowView {
    world::PersistentId light_entity{};
    std::size_t light_index = 0;
    std::array<RuntimeShadowCascade, runtime_shadow_cascade_count> cascades{};
};

[[nodiscard]] std::vector<RuntimeLight> collect_runtime_lights(const world::World& world);

[[nodiscard]] std::vector<RuntimeReflectionProbe> collect_runtime_reflection_probes(
    const world::World& world
);

[[nodiscard]] RuntimeReflectionBlend select_runtime_reflection_probes(
    world::GlobalPosition point,
    std::span<const RuntimeReflectionProbe> probes
) noexcept;

[[nodiscard]] std::optional<std::size_t> select_runtime_shadow_light(
    std::span<const RuntimeLight> lights
) noexcept;

[[nodiscard]] std::array<float, runtime_shadow_cascade_count> runtime_shadow_cascade_splits(
    const RuntimeCameraView& camera
) noexcept;

[[nodiscard]] std::optional<RuntimeShadowView> directional_shadow_view(
    const RuntimeCameraView& camera,
    std::span<const RuntimeLight> lights
) noexcept;

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

    [[nodiscard]] bool set_exposure(
        float exposure,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool set_bloom(
        float threshold,
        float soft_knee,
        float strength,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool set_vignette(
        float intensity,
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
