#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/visibility.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

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

[[nodiscard]] bool read_batch_output(
    vice::render::Device& device,
    std::unique_ptr<vice::render::CommandList> commands,
    const vice::runtime::RuntimeGpuDrawBatchResult& result,
    const std::size_t instance_count,
    std::vector<std::uint32_t>& compacted,
    std::vector<vice::render::IndexedIndirectDrawArguments>& arguments
) {
    using namespace vice;
    const std::uint64_t compacted_size =
        static_cast<std::uint64_t>(instance_count) * sizeof(std::uint32_t);
    const std::uint64_t argument_size =
        static_cast<std::uint64_t>(result.batch_count) * sizeof(render::IndexedIndirectDrawArguments);
    auto compacted_readback = make_readback(device, compacted_size, "GPU batch compacted readback");
    auto arguments_readback = make_readback(device, argument_size, "GPU batch argument readback");
    if (!commands || !compacted_readback || !arguments_readback ||
        result.compacted_instance_indices == nullptr || result.indirect_arguments == nullptr ||
        !commands->copy_buffer(
            *result.compacted_instance_indices, 0U, *compacted_readback, 0U, compacted_size
        ) ||
        !commands->copy_buffer(
            *result.indirect_arguments, 0U, *arguments_readback, 0U, argument_size
        )) {
        return false;
    }
    const auto submission = device.submit(std::move(commands));
    if (submission == render::invalid_submission || !device.wait_for_submission(submission)) return false;

    compacted.assign(instance_count, 0U);
    arguments.assign(result.batch_count, {});
    return compacted_readback->read(
               0U, std::as_writable_bytes(std::span{compacted})
           ) &&
        arguments_readback->read(
            0U, std::as_writable_bytes(std::span{arguments})
        );
}

[[nodiscard]] bool test_visibility_to_batches() {
    using namespace vice;
    using namespace vice::runtime;

    auto device = render::Device::create();
    if (!device) {
        std::cerr << "Could not create Tier 1 device for GPU batch smoke.\n";
        return false;
    }

    RuntimeVisibilityCuller culler;
    RuntimeGpuDrawBatcher batcher;
    std::string diagnostics;
    if (!culler.initialize(*device, &diagnostics) || !batcher.initialize(*device, &diagnostics)) {
        std::cerr << "Could not initialize GPU visibility/batching: " << diagnostics << '\n';
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
        RuntimeVisibilitySphere{{0.0F, 0.0F, -5.0F}, 0.25F, 0U},
        RuntimeVisibilitySphere{{8.0F, 0.0F, -5.0F}, 0.10F, 1U},
        RuntimeVisibilitySphere{{-1.0F, 0.0F, -4.0F}, 0.25F, 2U},
        RuntimeVisibilitySphere{{0.0F, 0.0F, -12.0F}, 0.25F, 3U},
        RuntimeVisibilitySphere{{1.0F, 1.0F, -6.0F}, 0.25F, 4U},
        RuntimeVisibilitySphere{{-9.0F, 0.0F, -5.0F}, 0.10F, 5U},
    };
    const std::array<std::uint32_t, 6U> assignments{0U, 1U, 0U, 1U, 1U, 0U};
    const std::array batches{
        RuntimeGpuDrawBatch{.index_count = 12U, .first_index = 3U, .vertex_offset = -2},
        RuntimeGpuDrawBatch{.index_count = 6U, .first_index = 20U, .vertex_offset = 5},
    };

    auto commands = device->create_command_list();
    if (!commands ||
        !culler.cull(*commands, spheres, frustum, &diagnostics) ||
        !batcher.build(*commands, culler.result(), assignments, batches, &diagnostics)) {
        std::cerr << "Could not encode visibility -> GPU batches: " << diagnostics << '\n';
        return false;
    }

    const auto result = batcher.result();
    if (result.batch_count != 2U || result.instance_capacity < assignments.size()) {
        std::cerr << "GPU batcher exposed an invalid result contract.\n";
        return false;
    }

    std::vector<std::uint32_t> compacted;
    std::vector<render::IndexedIndirectDrawArguments> arguments;
    if (!read_batch_output(
            *device, std::move(commands), result, assignments.size(), compacted, arguments
        )) {
        std::cerr << "Could not read GPU batch output.\n";
        return false;
    }

    if (arguments.size() != 2U ||
        arguments[0].index_count != 12U || arguments[0].instance_count != 2U ||
        arguments[0].first_index != 3U || arguments[0].vertex_offset != -2 ||
        arguments[0].first_instance != 0U ||
        arguments[1].index_count != 6U || arguments[1].instance_count != 1U ||
        arguments[1].first_index != 20U || arguments[1].vertex_offset != 5 ||
        arguments[1].first_instance != 0U || result.ranges.size() != 2U ||
        result.ranges[1].offset != 3U) {
        std::cerr << "GPU batcher generated unexpected indexed indirect arguments.\n";
        return false;
    }

    std::vector<std::uint32_t> batch0(compacted.begin(), compacted.begin() + arguments[0].instance_count);
    std::vector<std::uint32_t> batch1(
        compacted.begin() + result.ranges[1].offset,
        compacted.begin() + result.ranges[1].offset + arguments[1].instance_count
    );
    std::sort(batch0.begin(), batch0.end());
    std::sort(batch1.begin(), batch1.end());
    if (batch0 != std::vector<std::uint32_t>{0U, 2U} ||
        batch1 != std::vector<std::uint32_t>{4U}) {
        std::cerr << "GPU batcher compacted visible instances into the wrong batch ranges.\n";
        return false;
    }

    std::array<RuntimeVisibilitySphere, 6U> hidden = spheres;
    for (std::size_t index = 0U; index < hidden.size(); ++index) {
        hidden[index].center = {50.0F + static_cast<float>(index), 0.0F, -5.0F};
    }
    auto zero_commands = device->create_command_list();
    if (!zero_commands ||
        !culler.cull(*zero_commands, hidden, frustum, &diagnostics) ||
        !batcher.build(*zero_commands, culler.result(), assignments, batches, &diagnostics)) {
        std::cerr << "Could not encode zero-visible GPU batches: " << diagnostics << '\n';
        return false;
    }
    compacted.clear();
    arguments.clear();
    if (!read_batch_output(
            *device, std::move(zero_commands), batcher.result(), assignments.size(), compacted, arguments
        ) || arguments.size() != 2U || arguments[0].instance_count != 0U ||
        arguments[1].instance_count != 0U) {
        std::cerr << "Zero-visible GPU batches did not produce zero indirect instance counts.\n";
        return false;
    }

    return true;
}

} // namespace

#include "runtime_virtual_geometry_tests.inc"

int main() {
    return test_visibility_to_batches() && test_runtime_virtual_geometry_submission() ? 0 : 1;
}
