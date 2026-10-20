#import "compute_pipeline.hpp"

#include <algorithm>
#include <cstdint>

namespace vice::render::metal_detail {
namespace {

constexpr std::uint64_t descriptor_entry_size = 3U * sizeof(std::uint64_t);

[[nodiscard]] bool same_location_namespace(
    const MetalShaderResourceLocationDesc& location,
    const ShaderResourceBindingDesc& resource
) noexcept {
    return location.register_class == resource.register_class &&
        location.binding == resource.binding &&
        location.space == resource.space;
}

[[nodiscard]] bool validate_locations(
    const std::span<const ShaderResourceBindingDesc> resources,
    const std::span<const MetalShaderResourceLocationDesc> locations,
    std::string& diagnostics
) {
    if (locations.empty()) {
        for (const auto& resource : resources) {
            const std::uint64_t last_binding =
                static_cast<std::uint64_t>(resource.binding) + resource.count;
            if (resource.unbounded || resource.space != 0U || last_binding > 31U) {
                diagnostics =
                    "Metal source compute shaders require bounded resources in register space 0 "
                    "with native binding indices below 31.";
                return false;
            }
        }
        return true;
    }

    for (const auto& resource : resources) {
        if (resource.unbounded) {
            diagnostics = "Metal automatic compute layout does not support unbounded resource ranges.";
            return false;
        }
        const auto found = std::find_if(
            locations.begin(),
            locations.end(),
            [&](const MetalShaderResourceLocationDesc& location) {
                return same_location_namespace(location, resource);
            }
        );
        if (found == locations.end()) {
            diagnostics = "Metal compute shader is missing a converter-reflected resource location.";
            return false;
        }
        const std::uint64_t required =
            static_cast<std::uint64_t>(resource.count) * descriptor_entry_size;
        if (found->size < required) {
            diagnostics = "Metal compute resource location is too small for its reflected array.";
            return false;
        }
    }

    for (const auto& location : locations) {
        const auto logical = std::find_if(
            resources.begin(),
            resources.end(),
            [&](const ShaderResourceBindingDesc& resource) {
                return same_location_namespace(location, resource);
            }
        );
        if (logical == resources.end()) {
            diagnostics = "Metal compute converter location does not match a logical resource.";
            return false;
        }
    }
    return true;
}

} // namespace

ComputePipelineBuildResult build_compute_pipeline(
    id<MTLDevice> device,
    id<MTLFunction> function,
    const std::span<const ShaderResourceBindingDesc> resources,
    const std::span<const MetalShaderResourceLocationDesc> locations
) {
    ComputePipelineBuildResult result;
    if (device == nil || function == nil) {
        result.diagnostics = "Metal compute pipeline requires a valid device and function.";
        return result;
    }

    result.layout = compile_compute_pipeline_layout(resources);
    if (!result.layout.valid) {
        result.diagnostics = result.layout.diagnostics.empty()
            ? "VICE rejected the Metal compute pipeline resource layout."
            : "VICE rejected the Metal compute pipeline resource layout. " + result.layout.diagnostics;
        return result;
    }

    if (!validate_locations(resources, locations, result.diagnostics)) {
        return result;
    }

    NSError* error = nil;
    result.pipeline_state = [device newComputePipelineStateWithFunction:function error:&error];
    if (result.pipeline_state == nil) {
        result.diagnostics = "Metal could not create the compute pipeline state.";
        if (error != nil && error.localizedDescription != nil) {
            result.diagnostics += " ";
            result.diagnostics += error.localizedDescription.UTF8String;
        }
        return result;
    }

    result.locations.assign(locations.begin(), locations.end());
    result.success = true;
    return result;
}

} // namespace vice::render::metal_detail
