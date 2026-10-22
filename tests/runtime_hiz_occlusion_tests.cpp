#include <vice/math/math.hpp>
#include <vice/runtime/gpu_visibility_set.hpp>
#include <vice/runtime/hiz.hpp>
#include <vice/runtime/hiz_occlusion.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "CHECK failed: " #condition " at line " << __LINE__ << '\n'; \
        return __LINE__; \
    } \
} while (false)

int main() {
    using namespace vice;
    using namespace vice::render;
    using namespace vice::runtime;

    auto device = Device::create();
    if (!device) return 0;

    constexpr std::uint32_t width = 64U;
    constexpr std::uint32_t height = 64U;
    constexpr float near_plane = 0.1F;
    constexpr float far_plane = 100.0F;

    RuntimeHiZPyramid pyramid;
    RuntimeGpuIdentityVisibility identity_visibility;
    RuntimeHiZOcclusionCuller occlusion;
    std::string diagnostics;
    CHECK(pyramid.initialize(*device, &diagnostics));
    CHECK(pyramid.resize(width, height, &diagnostics));
    CHECK(identity_visibility.initialize(*device, &diagnostics));
    CHECK(occlusion.initialize(*device, &diagnostics));

    TextureDesc depth_desc;
    depth_desc.width = width;
    depth_desc.height = height;
    depth_desc.format = TextureFormat::depth32_float;
    depth_desc.usage = TextureUsage::depth_attachment | TextureUsage::sampled;
    depth_desc.debug_name = "VICE Hi-Z occlusion execution depth";
    auto depth = device->create_texture(depth_desc);
    CHECK(depth != nullptr);

    const std::array spheres{
        RuntimeVisibilitySphere{
            .center = {0.0F, 0.0F, -0.13F},
            .radius = 0.005F,
            .instance_index = 0U,
        },
        RuntimeVisibilitySphere{
            .center = {0.0F, 0.0F, -1.0F},
            .radius = 0.05F,
            .instance_index = 1U,
        },
        RuntimeVisibilitySphere{
            .center = {0.0F, 0.0F, -0.105F},
            .radius = 0.02F,
            .instance_index = 2U,
        },
    };

    const math::Mat4 view_projection = math::perspective_rh_zo(
        1.0F,
        1.0F,
        near_plane,
        far_plane
    );

    auto commands = device->create_command_list();
    CHECK(commands != nullptr);
    CHECK(commands->begin_depth_render_pass(*depth, 0.25F, true));
    CHECK(commands->end_render_pass());
    if (!pyramid.build(*commands, *depth, &diagnostics)) {
        std::cerr << diagnostics << '\n';
        return __LINE__;
    }
    if (!identity_visibility.prepare(
            *commands,
            static_cast<std::uint32_t>(spheres.size()),
            &diagnostics
        )) {
        std::cerr << diagnostics << '\n';
        return __LINE__;
    }
    if (!occlusion.cull(
            *commands,
            identity_visibility.result(),
            spheres,
            view_projection,
            near_plane,
            far_plane,
            pyramid,
            0.001F,
            &diagnostics
        )) {
        std::cerr << diagnostics << '\n';
        return __LINE__;
    }

    const RuntimeVisibilityGpuResult result = occlusion.result();
    CHECK(result.visible_count != nullptr);
    CHECK(result.visible_indices != nullptr);
    CHECK(result.capacity >= spheres.size());

    BufferDesc count_readback_desc;
    count_readback_desc.size = sizeof(std::uint32_t);
    count_readback_desc.usage = BufferUsage::copy_destination;
    count_readback_desc.memory = MemoryLocation::gpu_to_cpu;
    count_readback_desc.debug_name = "VICE Hi-Z occlusion count readback";
    auto count_readback = device->create_buffer(count_readback_desc);
    CHECK(count_readback != nullptr);

    BufferDesc indices_readback_desc;
    indices_readback_desc.size = result.capacity * sizeof(std::uint32_t);
    indices_readback_desc.usage = BufferUsage::copy_destination;
    indices_readback_desc.memory = MemoryLocation::gpu_to_cpu;
    indices_readback_desc.debug_name = "VICE Hi-Z occlusion indices readback";
    auto indices_readback = device->create_buffer(indices_readback_desc);
    CHECK(indices_readback != nullptr);

    CHECK(commands->copy_buffer(
        *result.visible_count,
        0U,
        *count_readback,
        0U,
        sizeof(std::uint32_t)
    ));
    CHECK(commands->copy_buffer(
        *result.visible_indices,
        0U,
        *indices_readback,
        0U,
        result.capacity * sizeof(std::uint32_t)
    ));

    const SubmissionId submission = device->submit(std::move(commands));
    CHECK(submission != invalid_submission);
    CHECK(device->wait_for_submission(submission));

    std::uint32_t visible_count = 0U;
    CHECK(count_readback->read(
        0U,
        std::as_writable_bytes(std::span{&visible_count, 1})
    ));
    CHECK(visible_count == 2U);

    std::array<std::uint32_t, 4U> visible_indices{};
    CHECK(result.capacity <= visible_indices.size());
    CHECK(indices_readback->read(
        0U,
        std::as_writable_bytes(std::span{visible_indices}.first(result.capacity))
    ));
    std::sort(visible_indices.begin(), visible_indices.begin() + visible_count);
    CHECK(visible_indices[0] == 0U);
    CHECK(visible_indices[1] == 2U);
    CHECK(device->wait_idle());

    std::cout << "Hi-Z occlusion execution verified: kept scene instances 0 and 2; rejected 1\n";
    return 0;
}

#undef CHECK
