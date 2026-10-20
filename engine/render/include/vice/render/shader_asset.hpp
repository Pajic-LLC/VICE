#pragma once

#include <vice/render/rhi.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vice::render {

struct CookedShaderManifest {
    bool valid = false;
    std::uint32_t version = 0;
    ShaderStage stage = ShaderStage::vertex;
    std::string entry_point;
    std::string dxil_artifact;
    std::string metallib_artifact;
    std::vector<ShaderResourceBindingDesc> resources;
    std::vector<MetalShaderResourceLocationDesc> metal_resource_locations;
    std::optional<ComputeThreadGroupSize> compute_thread_group_size;
    std::string diagnostics;
};

[[nodiscard]] CookedShaderManifest parse_cooked_shader_manifest(std::string_view json);

struct CookedShaderLoadResult {
    std::unique_ptr<ShaderModule> module;
    std::optional<ComputeThreadGroupSize> compute_thread_group_size;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return module != nullptr;
    }
};

[[nodiscard]] CookedShaderLoadResult load_cooked_shader(
    Device& device,
    const std::filesystem::path& manifest_path
);

} // namespace vice::render
