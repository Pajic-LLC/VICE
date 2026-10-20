#include <vice/runtime/gpu_instance_materials.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace {

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] std::unique_ptr<vice::render::Buffer> make_readback(
    vice::render::Device& device,
    const std::uint64_t size
) {
    vice::render::BufferDesc desc;
    desc.size = size;
    desc.usage = vice::render::BufferUsage::copy_destination;
    desc.memory = vice::render::MemoryLocation::gpu_to_cpu;
    desc.debug_name = "GPU instance material readback";
    return device.create_buffer(desc);
}

} // namespace

int main() {
    using namespace vice;
    using namespace vice::runtime;

    const world::ResourceId material_a{
        required_id("70000000-0000-4000-8000-000000000001")
    };
    const world::ResourceId material_b{
        required_id("70000000-0000-4000-8000-000000000002")
    };
    const world::ResourceId material_missing{
        required_id("70000000-0000-4000-8000-000000000099")
    };

    RuntimeGpuMaterialTablePlan materials;
    materials.material_ids = {material_a, material_b};
    materials.records.resize(2U);

    std::vector<RenderMeshInstance> instances(4U);
    instances[0].material = material_b;
    instances[1].material.reset();
    instances[2].material = material_a;
    instances[3].material = material_b;

    const auto built = build_runtime_gpu_instance_material_records(instances, materials);
    assert(built);
    assert(built.records.size() == instances.size());
    assert(built.records[0].material_index == 1U);
    assert(built.records[1].material_index == runtime_gpu_resource_unassigned);
    assert(built.records[2].material_index == 0U);
    assert(built.records[3].material_index == 1U);

    auto invalid_instances = instances;
    invalid_instances[2].material = material_missing;
    const auto invalid = build_runtime_gpu_instance_material_records(invalid_instances, materials);
    assert(!invalid);
    assert(invalid.records.empty());
    assert(invalid.diagnostics.find(material_missing.value.to_string()) != std::string::npos);

    const auto empty = build_runtime_gpu_instance_material_records({}, materials);
    assert(empty);
    assert(empty.records.empty());

    auto device = render::Device::create();
    assert(device);

    RuntimeGpuInstanceMaterialTable table;
    std::string diagnostics;
    assert(table.initialize(*device, &diagnostics));
    assert(diagnostics.empty());

    auto commands = device->create_command_list();
    const std::uint64_t byte_size = static_cast<std::uint64_t>(built.records.size()) *
        sizeof(RuntimeGpuInstanceMaterialRecord);
    auto readback = make_readback(*device, byte_size);
    assert(commands);
    assert(readback);
    assert(table.upload(*commands, built.records, &diagnostics));
    assert(diagnostics.empty());

    const auto gpu = table.result();
    assert(gpu.records != nullptr);
    assert(gpu.instance_count == built.records.size());
    assert(gpu.capacity >= built.records.size());
    assert(commands->copy_buffer(*gpu.records, 0U, *readback, 0U, byte_size));

    const auto submission = device->submit(std::move(commands));
    assert(submission != render::invalid_submission);
    assert(device->wait_for_submission(submission));

    std::vector<RuntimeGpuInstanceMaterialRecord> actual(built.records.size());
    assert(readback->read(0U, std::as_writable_bytes(std::span{actual})));
    assert(actual.size() == built.records.size());
    assert(std::memcmp(
        actual.data(), built.records.data(),
        built.records.size() * sizeof(RuntimeGpuInstanceMaterialRecord)
    ) == 0);

    auto empty_commands = device->create_command_list();
    assert(empty_commands);
    assert(table.upload(*empty_commands, {}, &diagnostics));
    assert(table.result().instance_count == 0U);

    return 0;
}
