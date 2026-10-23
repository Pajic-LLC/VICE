#pragma once

#include <vice/render/rhi.hpp>
#include <vice/runtime/scene.hpp>
#include <vice/runtime/texture.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

inline constexpr std::uint32_t runtime_gpu_resource_unassigned =
    std::numeric_limits<std::uint32_t>::max();

enum class RuntimeGpuMaterialTextureChannel : std::uint8_t {
    base_color,
    normal,
    orm,
    emissive,
};

struct RuntimeGpuMaterialSampler {
    render::Filter min_filter = render::Filter::linear;
    render::Filter mag_filter = render::Filter::linear;
    render::Filter mip_filter = render::Filter::linear;
    render::AddressMode address_u = render::AddressMode::repeat;
    render::AddressMode address_v = render::AddressMode::repeat;
    render::AddressMode address_w = render::AddressMode::repeat;
    std::uint32_t max_anisotropy = 1U;

    [[nodiscard]] constexpr bool operator==(
        const RuntimeGpuMaterialSampler& other
    ) const noexcept = default;
};

struct RuntimeGpuMaterialRecord {
    float base_color[4]{1.0F, 1.0F, 1.0F, 1.0F};
    float emissive_color[4]{0.0F, 0.0F, 0.0F, 1.0F};
    float metallic = 0.0F;
    float roughness = 1.0F;
    float normal_scale = 1.0F;
    float emissive_intensity = 0.0F;
    std::uint32_t base_color_texture = runtime_gpu_resource_unassigned;
    std::uint32_t normal_texture = runtime_gpu_resource_unassigned;
    std::uint32_t orm_texture = runtime_gpu_resource_unassigned;
    std::uint32_t emissive_texture = runtime_gpu_resource_unassigned;
    std::uint32_t sampler = runtime_gpu_resource_unassigned;
    std::uint32_t reserved[3]{};
};

static_assert(sizeof(RuntimeGpuMaterialRecord) == 80U);

struct RuntimeGpuMaterialTablePlan {
    std::vector<RuntimeGpuMaterialRecord> records;
    std::vector<world::ResourceId> material_ids;
    std::vector<world::ResourceId> texture_ids;
    std::vector<RuntimeGpuMaterialTextureChannel> texture_channels;
    std::vector<RuntimeGpuMaterialSampler> samplers;

    [[nodiscard]] std::optional<std::uint32_t> material_index(
        world::ResourceId resource_id
    ) const noexcept;
};

struct RuntimeGpuMaterialTableBuildInput {
    std::span<const CookedRenderMaterialRecord> materials;
    std::span<const CookedTextureRecord> textures;
    bool validate_texture_references = false;
};

struct RuntimeGpuMaterialTableBuildResult {
    RuntimeGpuMaterialTablePlan plan;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeGpuMaterialTableBuildResult build_runtime_gpu_material_table_plan(
    const RuntimeGpuMaterialTableBuildInput& input
);

struct RuntimeGpuMaterialTableResult {
    render::Buffer* records = nullptr;
    std::uint32_t material_count = 0U;
    std::uint32_t capacity = 0U;
};

class RuntimeGpuMaterialTable final {
public:
    RuntimeGpuMaterialTable();
    ~RuntimeGpuMaterialTable();

    RuntimeGpuMaterialTable(const RuntimeGpuMaterialTable&) = delete;
    RuntimeGpuMaterialTable& operator=(const RuntimeGpuMaterialTable&) = delete;
    RuntimeGpuMaterialTable(RuntimeGpuMaterialTable&&) noexcept;
    RuntimeGpuMaterialTable& operator=(RuntimeGpuMaterialTable&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool upload(
        render::CommandList& commands,
        std::span<const RuntimeGpuMaterialRecord> records,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeGpuMaterialTableResult result() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime