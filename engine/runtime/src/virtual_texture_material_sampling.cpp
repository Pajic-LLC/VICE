#include <vice/runtime/virtual_texture_sampling.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace vice::runtime {
namespace {

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] std::uint32_t virtual_index_for_slot(
    const RuntimeGpuMaterialTablePlan& materials,
    const RuntimeVirtualTextureSamplingPlan& virtual_textures,
    const std::uint32_t slot
) noexcept {
    if (slot == runtime_gpu_resource_unassigned || slot >= materials.texture_ids.size()) {
        return runtime_gpu_resource_unassigned;
    }
    const auto index = virtual_textures.texture_index(materials.texture_ids[slot]);
    return index.has_value() ? *index : runtime_gpu_resource_unassigned;
}

} // namespace

bool apply_runtime_virtual_texture_sampling(
    RuntimeGpuMaterialTablePlan& materials,
    const RuntimeVirtualTextureSamplingPlan& virtual_textures,
    std::string* const diagnostics
) {
    if (!virtual_textures) {
        set_diagnostics(diagnostics, virtual_textures.diagnostics);
        return false;
    }
    if (!materials.texture_channels.empty() &&
        materials.texture_channels.size() != materials.texture_ids.size()) {
        set_diagnostics(diagnostics, "Runtime GPU material texture semantic table is misaligned.");
        return false;
    }

    for (auto& record : materials.records) {
        record.virtual_base_color_texture = runtime_gpu_resource_unassigned;
        record.virtual_normal_texture = runtime_gpu_resource_unassigned;
        record.virtual_orm_texture = runtime_gpu_resource_unassigned;
        record.virtual_emissive_texture = runtime_gpu_resource_unassigned;

        if (record.sampler == runtime_gpu_resource_unassigned || record.sampler >= materials.samplers.size()) {
            set_diagnostics(diagnostics, "Runtime GPU material record references an invalid sampler index.");
            return false;
        }
        const auto& sampler = materials.samplers[record.sampler];
        if (sampler.address_u != render::AddressMode::repeat ||
            sampler.address_v != render::AddressMode::repeat) {
            continue;
        }

        record.virtual_base_color_texture = virtual_index_for_slot(
            materials, virtual_textures, record.base_color_texture
        );
        record.virtual_normal_texture = virtual_index_for_slot(
            materials, virtual_textures, record.normal_texture
        );
        record.virtual_orm_texture = virtual_index_for_slot(
            materials, virtual_textures, record.orm_texture
        );
        record.virtual_emissive_texture = virtual_index_for_slot(
            materials, virtual_textures, record.emissive_texture
        );
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::runtime
