#import "ray_tracing.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace vice::render::metal {
namespace {

[[nodiscard]] MTLAccelerationStructureUsage native_usage(
    const AccelerationStructureBuildFlags flags
) noexcept {
    NSUInteger result = MTLAccelerationStructureUsageNone;
    if (has_flag(flags, AccelerationStructureBuildFlags::allow_update)) {
        result |= MTLAccelerationStructureUsageRefit;
    }
    if (has_flag(flags, AccelerationStructureBuildFlags::prefer_fast_build)) {
        result |= MTLAccelerationStructureUsagePreferFastBuild;
    }
    return static_cast<MTLAccelerationStructureUsage>(result);
}

[[nodiscard]] MTLIndexType native_index_type(const IndexFormat format) noexcept {
    return format == IndexFormat::uint16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

[[nodiscard]] bool add_offset(
    const std::uint64_t base,
    const std::uint64_t offset,
    NSUInteger& result
) noexcept {
    if (base > std::numeric_limits<std::uint64_t>::max() - offset) {
        return false;
    }
    const std::uint64_t sum = base + offset;
    if (sum > std::numeric_limits<NSUInteger>::max()) {
        return false;
    }
    result = static_cast<NSUInteger>(sum);
    return true;
}

[[nodiscard]] bool native_size(const std::uint64_t value, NSUInteger& result) noexcept {
    if (value == 0U || value > std::numeric_limits<NSUInteger>::max()) {
        return false;
    }
    result = static_cast<NSUInteger>(value);
    return true;
}

[[nodiscard]] MTLPackedFloat4x3 native_transform(
    const std::array<float, 12>& transform
) noexcept {
    const std::array<float, 12> columns{
        transform[0], transform[4], transform[8],
        transform[1], transform[5], transform[9],
        transform[2], transform[6], transform[10],
        transform[3], transform[7], transform[11],
    };
    static_assert(sizeof(MTLPackedFloat4x3) == sizeof(columns));
    MTLPackedFloat4x3 result{};
    std::memcpy(&result, columns.data(), sizeof(result));
    return result;
}

void set_diagnostics(std::string* const diagnostics, const char* const message) {
    if (diagnostics != nullptr) *diagnostics = message;
}

} // namespace

bool supports_ray_tracing(id<MTLDevice> device) noexcept {
    return device != nil && device.supportsRaytracing;
}

AccelerationStructureBuildPlan build_acceleration_structure_plan(
    id<MTLDevice> device,
    const AccelerationStructureBuildDesc& desc,
    const BufferBindingResolver& buffer_binding,
    const AccelerationStructureResolver& acceleration_structure
) {
    AccelerationStructureBuildPlan result;
    result.type = desc.type;
    result.flags = desc.flags;

    if (device == nil || !is_valid(desc)) {
        result.diagnostics = "Invalid VICE acceleration-structure build description.";
        return result;
    }

    if (desc.type == AccelerationStructureType::bottom_level) {
        if (!buffer_binding) {
            result.diagnostics = "Metal BLAS planning requires a buffer binding resolver.";
            return result;
        }

        NSMutableArray<MTLAccelerationStructureGeometryDescriptor*>* geometries =
            [NSMutableArray arrayWithCapacity:desc.geometries.size()];
        for (const auto& geometry : desc.geometries) {
            const BufferBinding vertex = buffer_binding(*geometry.vertex_buffer);
            const BufferBinding index = buffer_binding(*geometry.index_buffer);
            if (vertex.buffer == nil || index.buffer == nil) {
                result.diagnostics = "Metal could not resolve a BLAS source buffer.";
                return result;
            }

            NSUInteger vertex_offset = 0U;
            NSUInteger index_offset = 0U;
            if (!add_offset(vertex.offset, geometry.vertex_offset, vertex_offset) ||
                !add_offset(index.offset, geometry.index_offset, index_offset)) {
                result.diagnostics = "Metal BLAS source buffer offset overflowed.";
                return result;
            }

            MTLAccelerationStructureTriangleGeometryDescriptor* native =
                [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
            native.vertexBuffer = vertex.buffer;
            native.vertexBufferOffset = vertex_offset;
            native.vertexStride = geometry.vertex_stride;
            native.vertexFormat = MTLAttributeFormatFloat3;
            native.indexBuffer = index.buffer;
            native.indexBufferOffset = index_offset;
            native.indexType = native_index_type(geometry.index_format);
            native.triangleCount = geometry.index_count / 3U;
            native.opaque = geometry.opaque;
            [geometries addObject:native];
        }

        MTLPrimitiveAccelerationStructureDescriptor* primitive =
            [MTLPrimitiveAccelerationStructureDescriptor descriptor];
        primitive.usage = native_usage(desc.flags);
        primitive.geometryDescriptors = geometries;
        result.descriptor = primitive;
        return result;
    }

    if (!acceleration_structure) {
        result.diagnostics = "Metal TLAS planning requires an acceleration-structure resolver.";
        return result;
    }
    if (desc.instances.size() > std::numeric_limits<std::uint32_t>::max() ||
        desc.instances.size() > std::numeric_limits<NSUInteger>::max() /
            sizeof(MTLAccelerationStructureUserIDInstanceDescriptor)) {
        result.diagnostics = "Metal TLAS instance count exceeds the supported range.";
        return result;
    }

    NSMutableArray<id<MTLAccelerationStructure>>* structures =
        [NSMutableArray arrayWithCapacity:desc.instances.size()];
    std::vector<MTLAccelerationStructureUserIDInstanceDescriptor> instances;
    instances.reserve(desc.instances.size());

    for (std::size_t index = 0; index < desc.instances.size(); ++index) {
        const auto& instance = desc.instances[index];
        id<MTLAccelerationStructure> structure = acceleration_structure(*instance.bottom_level);
        if (structure == nil) {
            result.diagnostics = "Metal could not resolve a TLAS bottom-level acceleration structure.";
            return result;
        }
        [structures addObject:structure];

        MTLAccelerationStructureUserIDInstanceDescriptor native{};
        native.transformationMatrix = native_transform(instance.transform);
        native.mask = instance.mask;
        native.intersectionFunctionTableOffset = 0U;
        native.accelerationStructureIndex = static_cast<std::uint32_t>(index);
        native.userID = instance.instance_id;
        instances.push_back(native);
    }

    const NSUInteger instance_bytes = static_cast<NSUInteger>(
        instances.size() * sizeof(MTLAccelerationStructureUserIDInstanceDescriptor)
    );
    id<MTLBuffer> instance_buffer = [device newBufferWithBytes:instances.data()
                                                        length:instance_bytes
                                                       options:MTLResourceStorageModeShared];
    if (instance_buffer == nil) {
        result.diagnostics = "Metal could not allocate the TLAS instance descriptor buffer.";
        return result;
    }

    MTLInstanceAccelerationStructureDescriptor* instanced =
        [MTLInstanceAccelerationStructureDescriptor descriptor];
    instanced.usage = native_usage(desc.flags);
    instanced.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeUserID;
    instanced.instancedAccelerationStructures = structures;
    instanced.instanceCount = desc.instances.size();
    instanced.instanceDescriptorBuffer = instance_buffer;
    instanced.instanceDescriptorBufferOffset = 0U;
    instanced.instanceDescriptorStride = sizeof(MTLAccelerationStructureUserIDInstanceDescriptor);

    result.descriptor = instanced;
    result.instance_buffer = instance_buffer;
    return result;
}

AccelerationStructureBuildSizeResult query_acceleration_structure_build_sizes(
    id<MTLDevice> device,
    const AccelerationStructureBuildPlan& plan
) {
    AccelerationStructureBuildSizeResult result;
    if (!plan) {
        result.diagnostics = plan.diagnostics.empty()
            ? "Invalid Metal acceleration-structure build plan."
            : plan.diagnostics;
        return result;
    }
    if (!supports_ray_tracing(device)) {
        result.diagnostics = "The active Metal device does not support ray tracing.";
        return result;
    }

    const MTLAccelerationStructureSizes native =
        [device accelerationStructureSizesWithDescriptor:plan.descriptor];
    result.sizes = AccelerationStructureBuildSizes{
        .result_size = native.accelerationStructureSize,
        .build_scratch_size = native.buildScratchBufferSize,
        .update_scratch_size = native.refitScratchBufferSize,
    };
    if (!is_valid(result.sizes)) {
        result.diagnostics = "Metal returned invalid acceleration-structure build sizes.";
        result.sizes = {};
    }
    return result;
}

AccelerationStructureResources allocate_acceleration_structure_resources(
    id<MTLDevice> device,
    const AccelerationStructureBuildPlan& plan,
    const AccelerationStructureBuildSizes& sizes
) {
    AccelerationStructureResources result;
    result.type = plan.type;
    result.flags = plan.flags;
    result.sizes = sizes;

    if (device == nil || !plan || !is_valid(sizes)) {
        result.diagnostics = "Invalid Metal acceleration-structure allocation request.";
        return result;
    }
    if (!supports_ray_tracing(device)) {
        result.diagnostics = "The active Metal device does not support ray tracing.";
        return result;
    }

    NSUInteger result_size = 0U;
    const std::uint64_t scratch_requirement = std::max(
        sizes.build_scratch_size,
        has_flag(plan.flags, AccelerationStructureBuildFlags::allow_update)
            ? sizes.update_scratch_size
            : 0U
    );
    NSUInteger scratch_size = 0U;
    if (!native_size(sizes.result_size, result_size) ||
        !native_size(scratch_requirement, scratch_size)) {
        result.diagnostics = "Metal acceleration-structure allocation size exceeds the native range.";
        return result;
    }

    result.result = [device newAccelerationStructureWithSize:result_size];
    result.scratch = [device newBufferWithLength:scratch_size options:MTLResourceStorageModePrivate];
    result.instance_buffer = plan.instance_buffer;
    if (result.result == nil || result.scratch == nil) {
        result.diagnostics = "Metal could not allocate acceleration-structure result or scratch memory.";
        result.result = nil;
        result.scratch = nil;
        result.instance_buffer = nil;
    }
    if(result.result != nil && plan.type==AccelerationStructureType::top_level) {
        // Metal Shader Converter IRRaytracingAccelerationStructureGPUHeader is a GPU buffer,
        // not the native resource ID itself. Keep it beside the TLAS for its entire lifetime.
        const NSUInteger count=plan.instance_buffer.length/sizeof(MTLAccelerationStructureUserIDInstanceDescriptor);
        result.instance_contributions=[device newBufferWithLength:std::max<NSUInteger>(4,count*4) options:MTLResourceStorageModeShared];
        result.converter_header=[device newBufferWithLength:64 options:MTLResourceStorageModeShared];
        if(result.instance_contributions==nil || result.converter_header==nil) {
            result.diagnostics="Metal could not allocate the converter acceleration-structure header.";
            result.result=nil;return result;
        }
        std::memset(result.instance_contributions.contents,0,result.instance_contributions.length);
        std::array<std::uint64_t,8> header{};
        header[0]=result.result.gpuResourceID._impl;
        header[1]=result.instance_contributions.gpuAddress;
        std::memcpy(result.converter_header.contents,header.data(),sizeof(header));
    }
    return result;
}

bool encode_acceleration_structure_build(
    id<MTLCommandBuffer> command_buffer,
    const AccelerationStructureBuildPlan& plan,
    AccelerationStructureResources& resources,
    const bool refit,
    const AccelerationStructureResources* const source,
    std::string* const diagnostics
) {
    if (diagnostics != nullptr) diagnostics->clear();
    if (command_buffer == nil || !plan || !resources ||
        plan.type != resources.type || !is_valid(resources.sizes)) {
        set_diagnostics(diagnostics, "Invalid Metal acceleration-structure build resources.");
        return false;
    }
    if (resources.result.size < resources.sizes.result_size) {
        set_diagnostics(diagnostics, "Metal acceleration-structure result memory is undersized.");
        return false;
    }

    const std::uint64_t required_scratch = refit
        ? resources.sizes.update_scratch_size
        : resources.sizes.build_scratch_size;
    if (required_scratch > 0U && resources.scratch.length < required_scratch) {
        set_diagnostics(diagnostics, "Metal acceleration-structure scratch memory is undersized.");
        return false;
    }

    if (plan.type == AccelerationStructureType::top_level) {
        if (plan.instance_buffer == nil) {
            set_diagnostics(diagnostics, "Metal TLAS build is missing its instance descriptor buffer.");
            return false;
        }
        resources.instance_buffer = plan.instance_buffer;
    }

    id<MTLAccelerationStructureCommandEncoder> encoder =
        [command_buffer accelerationStructureCommandEncoder];
    if (encoder == nil) {
        set_diagnostics(diagnostics, "Metal could not create an acceleration-structure command encoder.");
        return false;
    }

    if (refit) {
        if (!has_flag(plan.flags, AccelerationStructureBuildFlags::allow_update) ||
            source == nullptr || !*source || source->type != plan.type ||
            !has_flag(source->flags, AccelerationStructureBuildFlags::allow_update)) {
            [encoder endEncoding];
            set_diagnostics(diagnostics, "Metal acceleration-structure refit was not enabled by the source build.");
            return false;
        }
        id<MTLBuffer> scratch = resources.sizes.update_scratch_size > 0U
            ? resources.scratch
            : nil;
        [encoder refitAccelerationStructure:source->result
                                 descriptor:plan.descriptor
                                destination:resources.result
                              scratchBuffer:scratch
                        scratchBufferOffset:0U];
    } else {
        [encoder buildAccelerationStructure:resources.result
                                 descriptor:plan.descriptor
                              scratchBuffer:resources.scratch
                        scratchBufferOffset:0U];
    }
    [encoder endEncoding];
    return true;
}

} // namespace vice::render::metal
