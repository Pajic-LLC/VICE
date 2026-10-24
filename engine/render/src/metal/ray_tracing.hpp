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

} // namespace vice::render::metal
