#import "compute_pipeline.hpp"

#import <Metal/Metal.h>

#include <array>
#include <cstddef>
#include <cstdint>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

int main() {
    @autoreleasepool {
        using namespace vice::render;
        using namespace vice::render::metal_detail;

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        CHECK(device != nil);

        constexpr const char* source_text = R"MSL(
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
            initWithBytes:source_text
            length:sizeof(R"MSL(
#include <metal_stdlib>
using namespace metal;

kernel void vice_compute_test(
    device uint* output [[buffer(0)]],
    uint index [[thread_position_in_grid]])
{
    output[index] = 0xBEEF0000u + index;
}
)MSL") - 1U
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

        const ComputePipelineBuildResult pipeline = build_compute_pipeline(
            device,
            function,
            resources,
            {}
        );
        CHECK(pipeline.success);
        CHECK(pipeline.pipeline_state != nil);
        CHECK(pipeline.layout.valid);
        CHECK(pipeline.layout.resources.size() == 1U);
        CHECK(pipeline.diagnostics.empty());

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
                 threadsPerThreadgroup:MTLSizeMake(element_count, 1U, 1U)];
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
            {}
        );
        CHECK(!invalid.success);
        CHECK(!invalid.diagnostics.empty());

        return 0;
    }
}

#undef CHECK
