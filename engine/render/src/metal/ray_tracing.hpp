#pragma once

#include <vice/render/ray_tracing.hpp>

#import <Metal/Metal.h>

#include <cstdint>
#include <functional>
#include <string>

namespace vice::render::metal {

struct BufferBinding {
    id<MTLBuffer> buffer = nil;
    std::uint64_t offset = 0U;
};

using BufferBindingResolver = std::function<BufferBinding(const Buffer&)>;
using AccelerationStructureResolver =
    std::function<id<MTLAccelerationStructure>(const AccelerationStructure&)>;

struct AccelerationStructureBuildPlan {
    AccelerationStructureType type = AccelerationStructureType::bottom_level;
    AccelerationStructureBuildFlags flags = AccelerationStructureBuildFlags::none;
    MTLAccelerationStructureDescriptor* descriptor = nil;
    id<MTLBuffer> instance_buffer = nil;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return descriptor != nil && diagnostics.empty();
    }
};

struct AccelerationStructureBuildSizeResult {
    AccelerationStructureBuildSizes sizes{};
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

struct AccelerationStructureResources {
    AccelerationStructureType type = AccelerationStructureType::bottom_level;
    AccelerationStructureBuildFlags flags = AccelerationStructureBuildFlags::none;
    AccelerationStructureBuildSizes sizes{};
    id<MTLAccelerationStructure> result = nil;
    id<MTLBuffer> scratch = nil;
    id<MTLBuffer> instance_buffer = nil;
    id<MTLBuffer> converter_header = nil, instance_contributions = nil;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty() && result != nil && scratch != nil;
    }
};

[[nodiscard]] bool supports_ray_tracing(id<MTLDevice> device) noexcept;

[[nodiscard]] AccelerationStructureBuildPlan build_acceleration_structure_plan(
    id<MTLDevice> device,
    const AccelerationStructureBuildDesc& desc,
    const BufferBindingResolver& buffer_binding,
    const AccelerationStructureResolver& acceleration_structure = {}
);

[[nodiscard]] AccelerationStructureBuildSizeResult query_acceleration_structure_build_sizes(
    id<MTLDevice> device,
    const AccelerationStructureBuildPlan& plan
);

[[nodiscard]] AccelerationStructureResources allocate_acceleration_structure_resources(
    id<MTLDevice> device,
    const AccelerationStructureBuildPlan& plan,
    const AccelerationStructureBuildSizes& sizes
);

[[nodiscard]] bool encode_acceleration_structure_build(
    id<MTLCommandBuffer> command_buffer,
    const AccelerationStructureBuildPlan& plan,
    AccelerationStructureResources& resources,
    bool refit = false,
    const AccelerationStructureResources* source = nullptr,
    std::string* diagnostics = nullptr
);

} // namespace vice::render::metal
