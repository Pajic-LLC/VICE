#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/visibility.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <string>

namespace {

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

[[nodiscard]] bool run_test() {
    using namespace vice;
    using namespace vice::runtime;

    auto device = render::Device::create();
    if (!device) return false;

    RuntimeVisibilityCuller culler;
    RuntimeGpuDrawBatcher batcher;
    RuntimeGpuInstanceTable instance_table;
    std::string diagnostics;
    if (!culler.initialize(*device, &diagnostics) ||
        !batcher.initialize(*device, &diagnostics) ||
        !instance_table.initialize(*device, &diagnostics)) {
        std::cerr << diagnostics << '\n';
        return false;
    }

    const auto frustum = runtime_visibility_frustum(
        {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F},
        1.57079632679489661923F, 1.0F, 0.1F, 100.0F
    );
    const std::array spheres{
        RuntimeVisibilitySphere{{0.0F, 0.0F, -5.0F}, 0.5F, 0U},
        RuntimeVisibilitySphere{{1.0F, 0.0F, -5.0F}, 0.5F, 1U},
    };
    const std::array assignments{
        0U,
        runtime_gpu_draw_batch_unassigned,
    };
    const std::array batches{
        RuntimeGpuDrawBatch{.index_count = 3U, .first_index = 0U, .vertex_offset = 0},
    };
    std::array<math::Mat4, 2U> models{
        math::translation({1.0F, 2.0F, 3.0F}),
        math::translation({4.0F, 5.0F, 6.0F}),
    };

    auto commands = device->create_command_list();
    if (!commands ||
        !instance_table.upload(*commands, models, &diagnostics) ||
        !culler.cull(*commands, spheres, frustum, &diagnostics) ||
        !batcher.build(*commands, culler.result(), assignments, batches, &diagnostics)) {
        std::cerr << diagnostics << '\n';
        return false;
    }

    const auto table_result = instance_table.result();
    const auto batch_result = batcher.result();
    if (table_result.models == nullptr || table_result.instance_count != models.size() ||
        batch_result.compacted_instance_indices == nullptr || batch_result.indirect_arguments == nullptr ||
        batch_result.batch_count != 1U) {
        return false;
    }

    auto model_readback = make_readback(*device, sizeof(models), "GPU default model readback");
    auto compacted_readback = make_readback(
        *device, sizeof(std::uint32_t) * models.size(), "GPU default compacted readback"
    );
    auto indirect_readback = make_readback(
        *device, sizeof(render::IndexedIndirectDrawArguments), "GPU default indirect readback"
    );
    if (!model_readback || !compacted_readback || !indirect_readback ||
        !commands->copy_buffer(*table_result.models, 0U, *model_readback, 0U, sizeof(models)) ||
        !commands->copy_buffer(
            *batch_result.compacted_instance_indices,
            0U,
            *compacted_readback,
            0U,
            sizeof(std::uint32_t) * models.size()
        ) ||
        !commands->copy_buffer(
            *batch_result.indirect_arguments,
            0U,
            *indirect_readback,
            0U,
            sizeof(render::IndexedIndirectDrawArguments)
        )) {
        return false;
    }

    const auto submission = device->submit(std::move(commands));
    if (submission == render::invalid_submission || !device->wait_for_submission(submission)) return false;

    std::array<math::Mat4, 2U> uploaded{};
    std::array<std::uint32_t, 2U> compacted{};
    render::IndexedIndirectDrawArguments indirect{};
    if (!model_readback->read(0U, std::as_writable_bytes(std::span{uploaded})) ||
        !compacted_readback->read(0U, std::as_writable_bytes(std::span{compacted})) ||
        !indirect_readback->read(
            0U, std::as_writable_bytes(std::span{&indirect, 1})
        )) {
        return false;
    }

    if (std::memcmp(uploaded.data(), models.data(), sizeof(models)) != 0) {
        std::cerr << "GPU instance model table did not preserve uploaded matrices.\n";
        return false;
    }
    if (indirect.index_count != 3U || indirect.instance_count != 1U ||
        indirect.first_index != 0U || indirect.vertex_offset != 0 || indirect.first_instance != 0U ||
        compacted[0] != 0U) {
        std::cerr << "Unbatched visible instance leaked into the GPU default draw batch.\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    return run_test() ? 0 : 1;
}
