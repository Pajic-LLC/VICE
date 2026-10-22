#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/gpu_visibility_set.hpp>

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

[[nodiscard]] bool test_identity_visibility_batches_every_eligible_instance() {
    using namespace vice;
    using namespace vice::runtime;

    auto device = render::Device::create();
    if (!device) {
        std::cerr << "Could not create Tier 1 device for identity visibility smoke.\n";
        return false;
    }

    RuntimeGpuIdentityVisibility visibility;
    RuntimeGpuDrawBatcher batcher;
    std::string diagnostics;
    if (!visibility.initialize(*device, &diagnostics) ||
        !batcher.initialize(*device, &diagnostics)) {
        std::cerr << "Could not initialize identity visibility/batching: " << diagnostics << '\n';
        return false;
    }

    const std::array<std::uint32_t, 5U> assignments{
        0U,
        1U,
        0U,
        runtime_gpu_draw_batch_unassigned,
        1U,
    };
    const std::array batches{
        RuntimeGpuDrawBatch{.index_count = 12U, .first_index = 0U, .vertex_offset = 0},
        RuntimeGpuDrawBatch{.index_count = 6U, .first_index = 8U, .vertex_offset = -2},
    };

    auto commands = device->create_command_list();
    if (!commands ||
        !visibility.prepare(*commands, static_cast<std::uint32_t>(assignments.size()), &diagnostics) ||
        !batcher.build(*commands, visibility.result(), assignments, batches, &diagnostics)) {
        std::cerr << "Could not encode identity visibility -> GPU batches: " << diagnostics << '\n';
        return false;
    }

    const auto result = batcher.result();
    if (result.compacted_instance_indices == nullptr || result.indirect_arguments == nullptr ||
        result.batch_count != 2U) {
        std::cerr << "Identity visibility batching exposed an invalid result.\n";
        return false;
    }

    const std::uint64_t compacted_size =
        static_cast<std::uint64_t>(assignments.size()) * sizeof(std::uint32_t);
    const std::uint64_t argument_size =
        static_cast<std::uint64_t>(result.batch_count) * sizeof(render::IndexedIndirectDrawArguments);
    auto compacted_readback = make_readback(*device, compacted_size, "Identity visibility compacted readback");
    auto arguments_readback = make_readback(*device, argument_size, "Identity visibility arguments readback");
    if (!compacted_readback || !arguments_readback ||
        !commands->copy_buffer(
            *result.compacted_instance_indices, 0U, *compacted_readback, 0U, compacted_size
        ) ||
        !commands->copy_buffer(
            *result.indirect_arguments, 0U, *arguments_readback, 0U, argument_size
        )) {
        std::cerr << "Could not encode identity visibility readback.\n";
        return false;
    }

    const auto submission = device->submit(std::move(commands));
    if (submission == render::invalid_submission || !device->wait_for_submission(submission)) {
        std::cerr << "Identity visibility GPU submission failed.\n";
        return false;
    }

    std::vector<std::uint32_t> compacted(assignments.size(), 0U);
    std::array<render::IndexedIndirectDrawArguments, 2U> arguments{};
    if (!compacted_readback->read(0U, std::as_writable_bytes(std::span{compacted})) ||
        !arguments_readback->read(0U, std::as_writable_bytes(std::span{arguments}))) {
        std::cerr << "Could not read identity visibility GPU output.\n";
        return false;
    }

    if (arguments[0].instance_count != 2U || arguments[0].first_instance != 0U ||
        arguments[1].instance_count != 2U || arguments[1].first_instance != 2U) {
        std::cerr << "Identity visibility generated unexpected indirect instance ranges.\n";
        return false;
    }

    std::vector<std::uint32_t> batch0(
        compacted.begin(),
        compacted.begin() + arguments[0].instance_count
    );
    std::vector<std::uint32_t> batch1(
        compacted.begin() + arguments[1].first_instance,
        compacted.begin() + arguments[1].first_instance + arguments[1].instance_count
    );
    std::sort(batch0.begin(), batch0.end());
    std::sort(batch1.begin(), batch1.end());
    if (batch0 != std::vector<std::uint32_t>{0U, 2U} ||
        batch1 != std::vector<std::uint32_t>{1U, 4U}) {
        std::cerr << "Identity visibility did not preserve authoritative scene-instance IDs.\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    return test_identity_visibility_batches_every_eligible_instance() ? 0 : 1;
}
