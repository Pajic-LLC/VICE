#pragma once

#include <vice/runtime/post_process.hpp>
#include <vice/runtime/ray_diffuse_gi.hpp>
#include <vice/runtime/ray_diffuse_gi_denoise.hpp>
#include <vice/runtime/ray_diffuse_gi_settings.hpp>
#include <vice/runtime/ray_diffuse_gi_temporal.hpp>
#include <vice/runtime/ray_hit_radiance_history.hpp>
#include <vice/world/spatial.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace vice::runtime::detail {

struct RuntimeRayDiffuseGiPipelineManifests final {
    std::filesystem::path trace;
    std::filesystem::path hit_history;
    std::filesystem::path denoise;
    std::filesystem::path temporal;
};

struct RuntimeRayDiffuseGiPipelineInput final {
    render::CommandList* commands = nullptr;
    render::Texture* current_depth = nullptr;
    render::Texture* previous_depth = nullptr;
    render::Texture* motion_vectors = nullptr;
    render::Texture* surface_guide = nullptr;
    render::Texture* previous_scene_color = nullptr;
    render::AccelerationStructure* scene_tlas = nullptr;
    render::Buffer* scene_instance_indices = nullptr;
    std::uint32_t scene_instance_index_count = 0U;
    render::Texture* environment_specular = nullptr;
    render::Sampler* environment_sampler = nullptr;
    RuntimeRayDiffuseGiCamera camera{};
    RuntimeTemporalFrameState temporal{};
    world::GlobalPosition current_origin{};
    RuntimeRayDiffuseGiSettings settings{};
    float environment_intensity = 0.0F;
    float environment_rotation_radians = 0.0F;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
};

class RuntimeRayDiffuseGiPipeline final {
public:
    [[nodiscard]] bool initialize(
        render::Device& device,
        const RuntimeRayDiffuseGiPipelineManifests& manifests,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool resize(
        std::uint32_t width,
        std::uint32_t height,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool dispatch(
        const RuntimeRayDiffuseGiPipelineInput& input,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] render::Texture* radiance() const noexcept;

private:
    RuntimeRayDiffuseGiPass trace_;
    RuntimeRayHitRadianceHistoryPass hit_history_;
    RuntimeRayDiffuseGiDenoisePass denoise_;
    RuntimeRayDiffuseGiTemporalPass temporal_;
    bool initialized_ = false;
    bool valid_ = false;
    std::uint32_t width_ = 0U;
    std::uint32_t height_ = 0U;
};

} // namespace vice::runtime::detail
