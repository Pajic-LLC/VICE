#pragma once

#include <vice/math/math.hpp>
#include <vice/render/rhi.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace vice::runtime {

struct RuntimeReflectionSurfaceGuideSample {
    float normal_oct[2]{0.5F, 0.5F};
    float roughness = 1.0F;
    float metallic = 0.0F;
};

[[nodiscard]] RuntimeReflectionSurfaceGuideSample encode_runtime_reflection_surface_guide(
    math::Vec3 world_normal,
    float roughness,
    float metallic
) noexcept;

[[nodiscard]] math::Vec3 decode_runtime_reflection_surface_guide_normal(
    const RuntimeReflectionSurfaceGuideSample& sample
) noexcept;

[[nodiscard]] constexpr render::Color runtime_reflection_surface_guide_neutral() noexcept {
    return render::Color{0.5F, 0.5F, 1.0F, 0.0F};
}

struct RuntimeReflectionSurfaceGuideDraw {
    render::Buffer* vertices = nullptr;
    render::Buffer* indices = nullptr;
    render::Buffer* visible_scene_instances = nullptr;
    render::BufferViewDesc visible_instance_view{};
    render::Buffer* indirect_arguments = nullptr;
    std::uint64_t indirect_argument_offset = 0U;
};

struct RuntimeReflectionSurfaceGuideFrame {
    render::Texture* depth = nullptr;
    render::Buffer* frame_constants = nullptr;
    std::uint64_t frame_constants_size = 0U;
    render::Buffer* models = nullptr;
    std::uint32_t model_count = 0U;
    render::Buffer* instance_materials = nullptr;
    std::uint32_t instance_material_count = 0U;
    render::Buffer* material_records = nullptr;
    std::uint32_t material_count = 0U;
    std::span<render::Texture* const> material_textures;
    std::span<render::Sampler* const> material_samplers;
    std::span<const RuntimeReflectionSurfaceGuideDraw> draws;
};

class RuntimeReflectionSurfaceGuidePass final {
public:
    RuntimeReflectionSurfaceGuidePass();
    ~RuntimeReflectionSurfaceGuidePass();

    RuntimeReflectionSurfaceGuidePass(const RuntimeReflectionSurfaceGuidePass&) = delete;
    RuntimeReflectionSurfaceGuidePass& operator=(const RuntimeReflectionSurfaceGuidePass&) = delete;
    RuntimeReflectionSurfaceGuidePass(RuntimeReflectionSurfaceGuidePass&&) noexcept;
    RuntimeReflectionSurfaceGuidePass& operator=(RuntimeReflectionSurfaceGuidePass&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        const std::filesystem::path& vertex_shader_manifest,
        const std::filesystem::path& fragment_shader_manifest,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool resize(
        std::uint32_t width,
        std::uint32_t height,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool encode(
        render::CommandList& commands,
        const RuntimeReflectionSurfaceGuideFrame& frame,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] render::Texture* texture() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
