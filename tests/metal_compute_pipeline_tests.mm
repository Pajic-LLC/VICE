#import "compute_pipeline.hpp"

#import <Metal/Metal.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

int main() {
    @autoreleasepool {
        using namespace vice::render;
        using namespace vice::render::metal_detail;

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        CHECK(device != nil);

        constexpr std::string_view source_text = R"MSL(
#include <metal_stdlib>
using namespace metal;

kernel void vice_compute_test(
    device uint* output [[buffer(0)]],
    uint index [[thread_position_in_grid]])
{
    output[index] = 0xBEEF0000u + index;
}
)MSL";
        NSString* source = [[NSString alloc]
            initWithBytes:source_text.data()
            length:source_text.size()
            encoding:NSUTF8StringEncoding];
        CHECK(source != nil);

        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        CHECK(library != nil);
        id<MTLFunction> function = [library newFunctionWithName:@"vice_compute_test"];
        CHECK(function != nil);

        constexpr std::array resources{
            ShaderResourceBindingDesc{
                .register_class = ShaderRegisterClass::unordered_access,
                .binding = 0U,
                .space = 0U,
                .count = 1U,
                .unbounded = false,
            },
        };
        constexpr ComputeThreadGroupSize group_size{
            .x = 4U,
            .y = 1U,
            .z = 1U,
        };

        const ComputePipelineBuildResult pipeline = build_compute_pipeline(
            device,
            function,
            resources,
            {},
            group_size
        );
        CHECK(pipeline.success);
        CHECK(pipeline.pipeline_state != nil);
        CHECK(pipeline.layout.valid);
        CHECK(pipeline.layout.resources.size() == 1U);
        CHECK(pipeline.thread_group_size.x == 4U);
        CHECK(pipeline.thread_group_size.y == 1U);
        CHECK(pipeline.thread_group_size.z == 1U);
        CHECK(pipeline.diagnostics.empty());

        constexpr std::array unbounded_resources{
            ShaderResourceBindingDesc{
                .register_class = ShaderRegisterClass::unordered_access,
                .binding = 0U,
                .space = 0U,
                .count = 0U,
                .unbounded = true,
            },
        };
        constexpr std::array explicit_locations{
            MetalShaderResourceLocationDesc{
                .register_class = ShaderRegisterClass::unordered_access,
                .binding = 0U,
                .space = 0U,
                .argument_buffer_offset = 0U,
                .size = sizeof(std::uint64_t),
                .model = MetalShaderResourceBindingModel::descriptor_table,
            },
        };
        const ComputePipelineBuildResult explicit_pipeline = build_compute_pipeline(
            device,
            function,
            unbounded_resources,
            explicit_locations,
            group_size
        );
        CHECK(explicit_pipeline.success);
        CHECK(explicit_pipeline.layout.valid);
        CHECK(explicit_pipeline.layout.resources.size() == 1U);
        CHECK(explicit_pipeline.layout.resources[0].unbounded);
        CHECK(explicit_pipeline.locations.size() == 1U);
        CHECK(explicit_pipeline.locations[0].model == MetalShaderResourceBindingModel::descriptor_table);

        constexpr NSUInteger element_count = 4U;
        constexpr NSUInteger byte_count = element_count * sizeof(std::uint32_t);
        id<MTLBuffer> output = [device newBufferWithLength:byte_count options:MTLResourceStorageModePrivate];
        id<MTLBuffer> readback = [device newBufferWithLength:byte_count options:MTLResourceStorageModeShared];
        CHECK(output != nil);
        CHECK(readback != nil);

        id<MTLCommandQueue> queue = [device newCommandQueue];
        CHECK(queue != nil);
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        CHECK(commands != nil);
        id<MTLComputeCommandEncoder> compute = [commands computeCommandEncoder];
        CHECK(compute != nil);
        [compute setComputePipelineState:pipeline.pipeline_state];
        [compute setBuffer:output offset:0U atIndex:0U];
        [compute useResource:output usage:MTLResourceUsageWrite];
        [compute dispatchThreadgroups:MTLSizeMake(1U, 1U, 1U)
                 threadsPerThreadgroup:MTLSizeMake(
                     pipeline.thread_group_size.x,
                     pipeline.thread_group_size.y,
                     pipeline.thread_group_size.z
                 )];
        [compute endEncoding];

        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        CHECK(blit != nil);
        [blit copyFromBuffer:output
                sourceOffset:0U
                    toBuffer:readback
           destinationOffset:0U
                        size:byte_count];
        [blit endEncoding];

        [commands commit];
        [commands waitUntilCompleted];
        CHECK(commands.status == MTLCommandBufferStatusCompleted);

        const auto* values = static_cast<const std::uint32_t*>(readback.contents);
        CHECK(values != nullptr);
        for (std::uint32_t index = 0U; index < element_count; ++index) {
            CHECK(values[index] == 0xBEEF0000U + index);
        }

        const ComputePipelineBuildResult invalid = build_compute_pipeline(
            nil,
            function,
            resources,
            {},
            group_size
        );
        CHECK(!invalid.success);
        CHECK(!invalid.diagnostics.empty());

        return 0;
    }
}

#undef CHECK
