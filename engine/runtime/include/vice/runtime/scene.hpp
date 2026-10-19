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
inline constexpr float runtime_shadow_transition_fraction = 0.10F;
inline constexpr std::size_t runtime_point_shadow_light_count = 2U;
inline constexpr std::size_t runtime_point_shadow_face_count = 6U;
inline constexpr std::uint32_t runtime_point_shadow_map_size = 1024U;
inline constexpr std::size_t runtime_local_reflection_probe_count = 2U;

struct RuntimeLight {
    world::PersistentId entity{};
    world::Light light{};
    math::Vec3 position{};
    world::GlobalPosition global_position{};
    math::Vec3 direction{};
};

struct RuntimeAtmosphereSettings {
    float rayleigh_strength = 1.0F;
    float mie_strength = 0.35F;
    float mie_anisotropy = 0.76F;
    float sun_angular_radius = 0.004675F;
    float aerial_density = 0.00045F;
    float horizon_falloff = 0.18F;
    math::Vec3 ground_albedo{0.025F, 0.03F, 0.04F};
};

struct RuntimeAtmosphereFrame {
    math::Vec3 sun_direction{0.431934F, 0.863868F, 0.259161F};
    math::Vec3 sun_color{1.0F, 0.952381F, 0.880952F};
    float sun_intensity = 4.2F;
    RuntimeAtmosphereSettings settings{};
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
    std::array<std::optional<std::size_t>, runtime_local_reflection_probe_count> local_probe_indices{};
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

struct RuntimeShadowCascadeBlend {
    std::size_t primary = 0U;
    std::size_t secondary = 0U;
    float secondary_weight = 0.0F;
};

enum class RuntimePointShadowFace : std::uint8_t {
    positive_x = 0,
    negative_x = 1,
    positive_y = 2,
    negative_y = 3,
    positive_z = 4,
    negative_z = 5,
};

struct RuntimePointShadowView {
    world::PersistentId light_entity{};
    std::size_t light_index = 0U;
    world::GlobalPosition global_position{};
    float range = 0.0F;
    std::array<math::Mat4, runtime_point_shadow_face_count> face_view_projections{};
};

[[nodiscard]] std::vector<RuntimeLight> collect_runtime_lights(const world::World& world);

[[nodiscard]] RuntimeAtmosphereFrame runtime_atmosphere_frame(
    std::span<const RuntimeLight> lights
) noexcept;

[[nodiscard]] math::Vec3 evaluate_runtime_sky_radiance(
    math::Vec3 view_direction,
    const RuntimeAtmosphereFrame& atmosphere
) noexcept;

[[nodiscard]] float runtime_atmosphere_transmittance(
    float distance,
    const RuntimeAtmosphereSettings& settings
) noexcept;

[[nodiscard]] std::vector<RuntimeReflectionProbe> collect_runtime_reflection_probes(
    const world::World& world
);

[[nodiscard]] RuntimeReflectionBlend select_runtime_reflection_probes(
    world::GlobalPosition point,
    std::span<const RuntimeReflectionProbe> probes
) noexcept;

[[nodiscard]] math::Vec3 parallax_correct_reflection_direction(
    world::GlobalPosition point,
    math::Vec3 world_direction,
    const RuntimeReflectionProbe& probe
) noexcept;

[[nodiscard]] std::optional<std::size_t> select_runtime_shadow_light(
    std::span<const RuntimeLight> lights
) noexcept;

[[nodiscard]] std::array<float, runtime_shadow_cascade_count> runtime_shadow_cascade_splits(
    const RuntimeCameraView& camera
) noexcept;

[[nodiscard]] RuntimeShadowCascadeBlend runtime_shadow_cascade_blend(
    float world_depth,
    const std::array<float, runtime_shadow_cascade_count>& splits
) noexcept;

[[nodiscard]] std::optional<RuntimeShadowView> directional_shadow_view(
    const RuntimeCameraView& camera,
    std::span<const RuntimeLight> lights
) noexcept;

[[nodiscard]] RuntimePointShadowFace runtime_point_shadow_face(math::Vec3 light_to_point) noexcept;

[[nodiscard]] math::Mat4 runtime_point_shadow_face_view_projection(
    RuntimePointShadowFace face,
    float range
) noexcept;

[[nodiscard]] std::array<std::optional<RuntimePointShadowView>, runtime_point_shadow_light_count>
runtime_point_shadow_views(
    world::GlobalPosition camera_position,
    std::span<const RuntimeLight> lights
) noexcept;

[[nodiscard]] std::vector<RenderMeshInstance> collect_render_mesh_instances(
    const world::World& world
);

[[nodiscard]] math::Mat4 runtime_model_matrix(const RenderMeshInstance& instance) noexcept;

[[nodiscard]] math::Mat4 runtime_model_matrix_relative_to(
    const RenderMeshInstance& instance,
    world::GlobalPosition origin
) noexcept;

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
