#include <vice/runtime/visibility.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {

[[nodiscard]] bool close_enough(
    const float left,
    const float right,
    const float epsilon = 1.0e-4F
) noexcept {
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] std::unique_ptr<vice::render::Buffer> make_readback(
    vice::render::Device& device,
    const std::uint64_t size,
    const char* const name
) {
    vice::render::BufferDesc desc;
    desc.size = size;
    desc.usage = vice::render::BufferUsage::copy_destination;
    desc.memory = vice::render::MemoryLocation::gpu_to_cpu;
    desc.debug_name = name;
    return device.create_buffer(desc);
}

[[nodiscard]] bool test_cpu_reference() {
    using namespace vice;
    using namespace vice::runtime;

    const auto frustum = runtime_visibility_frustum(
        {0.0F, 0.0F, -2.0F},
        {0.0F, 3.0F, 0.0F},
        1.57079632679489661923F,
        1.0F,
        1.0F,
        10.0F
    );
    if (!valid(frustum)) {
        std::cerr << "Visibility frustum rejected a valid non-unit camera basis.\n";
        return false;
    }

    const RuntimeVisibilitySphere inside{{0.0F, 0.0F, -5.0F}, 0.5F, 11U};
    const RuntimeVisibilitySphere outside_side{{8.0F, 0.0F, -5.0F}, 0.1F, 22U};
    const RuntimeVisibilitySphere tangent_near{{0.0F, 0.0F, -0.5F}, 0.5F, 33U};
    const RuntimeVisibilitySphere before_near{{0.0F, 0.0F, -0.5F}, 0.1F, 44U};
    const RuntimeVisibilitySphere tangent_far{{0.0F, 0.0F, -11.0F}, 1.0F, 55U};
    const RuntimeVisibilitySphere beyond_far{{0.0F, 0.0F, -11.0F}, 0.25F, 66U};

    if (!runtime_visibility_intersects(frustum, inside) ||
        runtime_visibility_intersects(frustum, outside_side) ||
        !runtime_visibility_intersects(frustum, tangent_near) ||
        runtime_visibility_intersects(frustum, before_near) ||
        !runtime_visibility_intersects(frustum, tangent_far) ||
        runtime_visibility_intersects(frustum, beyond_far)) {
        std::cerr << "CPU frustum/sphere visibility reference produced an unexpected result.\n";
        return false;
    }

    const auto invalid_frustum = runtime_visibility_frustum(
        {0.0F, 1.0F, 0.0F},
        {0.0F, 2.0F, 0.0F},
        1.0F,
        1.0F,
        0.1F,
        10.0F
    );
    if (valid(invalid_frustum)) {
        std::cerr << "Visibility frustum accepted a parallel forward/up basis.\n";
        return false;
    }

    std::array<render::MeshVertex, 4U> vertices{};
    vertices[0].position[0] = -2.0F;
    vertices[0].position[1] = -1.0F;
    vertices[1].position[0] = 2.0F;
    vertices[1].position[1] = 1.0F;
    vertices[2].position[0] = -2.0F;
    vertices[2].position[1] = 1.0F;
    vertices[3].position[0] = 2.0F;
    vertices[3].position[1] = -1.0F;

    const auto mesh_sphere = runtime_mesh_bounding_sphere(vertices);
    if (!valid(mesh_sphere) || !close_enough(mesh_sphere.center.x, 0.0F) ||
        !close_enough(mesh_sphere.center.y, 0.0F) ||
        !close_enough(mesh_sphere.radius, std::sqrt(5.0F))) {
        std::cerr << "Mesh bounding sphere derivation is not conservative as expected.\n";
        return false;
    }

    auto transform = math::identity();
    transform(0U, 0U) = 2.0F;
    transform(1U, 1U) = 3.0F;
    transform(2U, 2U) = 0.5F;
    transform(0U, 3U) = 7.0F;
    transform(1U, 3U) = -4.0F;
    transform(2U, 3U) = 2.0F;
    const auto transformed = runtime_transform_bounding_sphere(mesh_sphere, transform);
    if (!valid(transformed) || !close_enough(transformed.center.x, 7.0F) ||
        !close_enough(transformed.center.y, -4.0F) ||
        !close_enough(transformed.center.z, 2.0F) ||
        !close_enough(transformed.radius, mesh_sphere.radius * 3.0F)) {
        std::cerr << "Bounding sphere transform did not preserve a conservative radius.\n";
        return false;
    }

    return true;
}

[[nodiscard]] bool test_gpu_compaction() {
    using namespace vice;
    using namespace vice::runtime;

    auto device = render::Device::create();
    if (!device) {
        std::cerr << "Could not create Tier 1 render device for visibility smoke.\n";
        return false;
    }

    RuntimeVisibilityCuller culler;
    std::string diagnostics;
    if (!culler.initialize(*device, &diagnostics)) {
        std::cerr << "Could not initialize visibility culler: " << diagnostics << '\n';
        return false;
    }

    const auto frustum = runtime_visibility_frustum(
        {0.0F, 0.0F, -1.0F},
        {0.0F, 1.0F, 0.0F},
        1.57079632679489661923F,
        1.0F,
        1.0F,
        10.0F
    );
    const std::array spheres{
        RuntimeVisibilitySphere{{0.0F, 0.0F, -5.0F}, 0.5F, 11U},
        RuntimeVisibilitySphere{{8.0F, 0.0F, -5.0F}, 0.1F, 22U},
        RuntimeVisibilitySphere{{0.0F, 0.0F, -0.5F}, 0.5F, 33U},
        RuntimeVisibilitySphere{{0.0F, 0.0F, -11.0F}, 0.25F, 44U},
        RuntimeVisibilitySphere{{-1.0F, 1.0F, -4.0F}, 0.25F, 55U},
    };

    auto commands = device->create_command_list();
    if (!commands || !culler.cull(*commands, spheres, frustum, &diagnostics)) {
        std::cerr << "Could not encode visibility compaction: " << diagnostics << '\n';
        return false;
    }

    const auto gpu_result = culler.result();
    if (gpu_result.visible_count == nullptr || gpu_result.visible_indices == nullptr ||
        gpu_result.capacity < spheres.size()) {
        std::cerr << "Visibility culler did not expose valid GPU output buffers.\n";
        return false;
    }

    auto count_readback = make_readback(*device, sizeof(std::uint32_t), "visibility count readback");
    auto indices_readback = make_readback(
        *device,
        static_cast<std::uint64_t>(spheres.size()) * sizeof(std::uint32_t),
        "visibility indices readback"
    );
    if (!count_readback || !indices_readback ||
        !commands->copy_buffer(
            *gpu_result.visible_count,
            0U,
            *count_readback,
            0U,
            sizeof(std::uint32_t)
        ) ||
        !commands->copy_buffer(
            *gpu_result.visible_indices,
            0U,
            *indices_readback,
            0U,
            static_cast<std::uint64_t>(spheres.size()) * sizeof(std::uint32_t)
        )) {
        std::cerr << "Could not encode visibility readback copies.\n";
        return false;
    }

    const auto submission = device->submit(std::move(commands));
    if (submission == render::invalid_submission || !device->wait_for_submission(submission)) {
        std::cerr << "Visibility GPU submission failed.\n";
        return false;
    }

    std::uint32_t visible_count = 0U;
    if (!count_readback->read(
            0U,
            std::as_writable_bytes(std::span{&visible_count, 1})
        )) {
        std::cerr << "Could not read visibility count.\n";
        return false;
    }
    if (visible_count != 3U) {
        std::cerr << "Visibility count mismatch: expected 3, got " << visible_count << ".\n";
        return false;
    }

    std::vector<std::uint32_t> visible_indices(visible_count);
    if (!indices_readback->read(
            0U,
            std::as_writable_bytes(std::span{visible_indices})
        )) {
        std::cerr << "Could not read compacted visibility indices.\n";
        return false;
    }
    std::sort(visible_indices.begin(), visible_indices.end());
    const std::array expected{11U, 33U, 55U};
    if (!std::equal(visible_indices.begin(), visible_indices.end(), expected.begin(), expected.end())) {
        std::cerr << "GPU visibility compacted an unexpected instance set.\n";
        return false;
    }

    auto empty_commands = device->create_command_list();
    auto empty_count_readback = make_readback(
        *device,
        sizeof(std::uint32_t),
        "empty visibility count readback"
    );
    if (!empty_commands || !empty_count_readback ||
        !culler.cull(*empty_commands, {}, frustum, &diagnostics)) {
        std::cerr << "Zero-instance visibility reset failed: " << diagnostics << '\n';
        return false;
    }
    const auto empty_result = culler.result();
    if (empty_result.visible_count == nullptr ||
        !empty_commands->copy_buffer(
            *empty_result.visible_count,
            0U,
            *empty_count_readback,
            0U,
            sizeof(std::uint32_t)
        )) {
        std::cerr << "Could not encode zero-instance visibility readback.\n";
        return false;
    }
    const auto empty_submission = device->submit(std::move(empty_commands));
    if (empty_submission == render::invalid_submission ||
        !device->wait_for_submission(empty_submission)) {
        std::cerr << "Zero-instance visibility submission failed.\n";
        return false;
    }
    visible_count = 99U;
    if (!empty_count_readback->read(
            0U,
            std::as_writable_bytes(std::span{&visible_count, 1})
        ) || visible_count != 0U) {
        std::cerr << "Zero-instance visibility did not reset the GPU count to zero.\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    if (!test_cpu_reference()) return 1;
    if (!test_gpu_compaction()) return 2;
    return 0;
}
