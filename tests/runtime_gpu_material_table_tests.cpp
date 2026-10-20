#include <vice/runtime/gpu_material_table.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    if (!parsed.has_value()) {
        std::cerr << "Invalid test UUID: " << text << '\n';
        std::terminate();
    }
    return *parsed;
}

template <std::size_t Size>
void copy_id(const vice::world::PersistentId id, std::array<std::uint8_t, Size>& destination) {
    static_assert(Size == vice::world::persistent_id_size);
    std::copy(id.bytes().begin(), id.bytes().end(), destination.begin());
}

[[nodiscard]] vice::runtime::CookedRenderMaterialRecord material_record(
    const vice::world::PersistentId id,
    const vice::world::PersistentId base_texture,
    const vice::world::PersistentId normal_texture,
    const float metallic,
    const float roughness,
    const bool alternate_sampler
) {
    using namespace vice;
    runtime::CookedRenderMaterialRecord record;
    record.resource_id = world::ResourceId{id};
    copy_id(id, record.asset.stable_id);
    copy_id(base_texture, record.asset.base_color_texture);
    copy_id(normal_texture, record.asset.normal_texture);
    record.asset.base_color = {0.2F + metallic, 0.4F, 0.8F, 1.0F};
    record.asset.emissive_color = {0.1F, 0.2F, 0.3F, 1.0F};
    record.asset.metallic = metallic;
    record.asset.roughness = roughness;
    record.asset.normal_scale = 0.75F;
    record.asset.emissive_intensity = 2.5F;
    if (alternate_sampler) {
        record.asset.address_u = render::AddressMode::clamp_to_edge;
        record.asset.address_v = render::AddressMode::mirrored_repeat;
        record.asset.max_anisotropy = 1U;
    }
    return record;
}

[[nodiscard]] vice::runtime::CookedTextureRecord texture_record(
    const vice::world::PersistentId id
) {
    vice::runtime::CookedTextureRecord record;
    record.resource_id = vice::world::ResourceId{id};
    copy_id(id, record.asset.stable_id);
    return record;
}

[[nodiscard]] bool same_records(
    const std::span<const vice::runtime::RuntimeGpuMaterialRecord> left,
    const std::span<const vice::runtime::RuntimeGpuMaterialRecord> right
) noexcept {
    return left.size() == right.size() &&
        std::memcmp(
            left.data(),
            right.data(),
            left.size_bytes()
        ) == 0;
}

[[nodiscard]] std::unique_ptr<vice::render::Buffer> make_readback(
    vice::render::Device& device,
    const std::uint64_t size
) {
    vice::render::BufferDesc desc;
    desc.size = size;
    desc.usage = vice::render::BufferUsage::copy_destination;
    desc.memory = vice::render::MemoryLocation::gpu_to_cpu;
    desc.debug_name = "GPU material table readback";
    return device.create_buffer(desc);
}

[[nodiscard]] bool test_plan() {
    using namespace vice;
    using namespace vice::runtime;

    const auto material_a_id = required_id("70000000-0000-4000-8000-000000000001");
    const auto material_b_id = required_id("70000000-0000-4000-8000-000000000002");
    const auto material_c_id = required_id("70000000-0000-4000-8000-000000000003");
    const auto texture_a_id = required_id("71000000-0000-4000-8000-000000000001");
    const auto texture_b_id = required_id("71000000-0000-4000-8000-000000000002");
    const auto missing_texture_id = required_id("71000000-0000-4000-8000-000000000099");

    const auto material_a = material_record(
        material_a_id, texture_a_id, texture_b_id, 0.25F, 0.45F, false
    );
    const auto material_b = material_record(
        material_b_id, texture_a_id, texture_b_id, 0.75F, 0.20F, false
    );
    const auto material_c = material_record(
        material_c_id, texture_b_id, texture_a_id, 0.10F, 0.90F, true
    );
    const auto texture_a = texture_record(texture_a_id);
    const auto texture_b = texture_record(texture_b_id);

    const std::array materials_forward{material_a, material_b, material_c};
    const std::array materials_reverse{material_c, material_b, material_a};
    const std::array textures_forward{texture_a, texture_b};
    const std::array textures_reverse{texture_b, texture_a};

    const auto forward = build_runtime_gpu_material_table_plan({
        .materials = materials_forward,
        .textures = textures_forward,
        .validate_texture_references = true,
    });
    const auto reverse = build_runtime_gpu_material_table_plan({
        .materials = materials_reverse,
        .textures = textures_reverse,
        .validate_texture_references = true,
    });
    if (!forward || !reverse) {
        std::cerr << "Could not build deterministic GPU material plans: "
                  << forward.diagnostics << ' ' << reverse.diagnostics << '\n';
        return false;
    }

    if (forward.plan.material_ids != reverse.plan.material_ids ||
        forward.plan.texture_ids != reverse.plan.texture_ids ||
        forward.plan.samplers != reverse.plan.samplers ||
        !same_records(forward.plan.records, reverse.plan.records)) {
        std::cerr << "GPU material plan depends on input enumeration order.\n";
        return false;
    }
    if (forward.plan.records.size() != 3U ||
        forward.plan.texture_ids.size() != 2U ||
        forward.plan.samplers.size() != 2U) {
        std::cerr << "GPU material plan did not deduplicate resources as expected.\n";
        return false;
    }

    const auto index_a = forward.plan.material_index(world::ResourceId{material_a_id});
    const auto index_b = forward.plan.material_index(world::ResourceId{material_b_id});
    const auto index_c = forward.plan.material_index(world::ResourceId{material_c_id});
    if (index_a != std::optional<std::uint32_t>{0U} ||
        index_b != std::optional<std::uint32_t>{1U} ||
        index_c != std::optional<std::uint32_t>{2U}) {
        std::cerr << "GPU material stable-ID lookup produced unexpected indices.\n";
        return false;
    }

    const auto& record_a = forward.plan.records[*index_a];
    const auto& record_b = forward.plan.records[*index_b];
    const auto& record_c = forward.plan.records[*index_c];
    if (record_a.base_color_texture != record_b.base_color_texture ||
        record_a.normal_texture != record_b.normal_texture ||
        record_a.sampler != record_b.sampler ||
        record_c.sampler == record_a.sampler ||
        record_a.base_color_texture == runtime_gpu_resource_unassigned ||
        record_a.normal_texture == runtime_gpu_resource_unassigned ||
        record_a.metallic != 0.25F || record_a.roughness != 0.45F ||
        record_a.normal_scale != 0.75F || record_a.emissive_intensity != 2.5F) {
        std::cerr << "GPU material record did not preserve or deduplicate PBR data.\n";
        return false;
    }
    if (record_a.orm_texture != runtime_gpu_resource_unassigned ||
        record_a.emissive_texture != runtime_gpu_resource_unassigned) {
        std::cerr << "Missing material textures did not use the explicit sentinel.\n";
        return false;
    }

    auto missing_material = material_a;
    copy_id(missing_texture_id, missing_material.asset.orm_texture);
    const std::array missing_materials{missing_material};
    const auto missing = build_runtime_gpu_material_table_plan({
        .materials = missing_materials,
        .textures = textures_forward,
        .validate_texture_references = true,
    });
    if (missing || missing.diagnostics.find(missing_texture_id.to_string()) == std::string::npos) {
        std::cerr << "GPU material plan accepted a missing cooked texture reference.\n";
        return false;
    }

    const RuntimeGpuMaterialTableBuildResult empty = build_runtime_gpu_material_table_plan({});
    if (!empty || !empty.plan.records.empty() || !empty.plan.texture_ids.empty() ||
        !empty.plan.samplers.empty()) {
        std::cerr << "Empty GPU material plan is not safe.\n";
        return false;
    }

    return true;
}

[[nodiscard]] bool test_gpu_upload() {
    using namespace vice;
    using namespace vice::runtime;

    auto device = render::Device::create();
    if (!device) {
        std::cerr << "Could not create Tier 1 device for GPU material table smoke.\n";
        return false;
    }

    RuntimeGpuMaterialTable table;
    std::string diagnostics;
    if (!table.initialize(*device, &diagnostics)) {
        std::cerr << "Could not initialize GPU material table: " << diagnostics << '\n';
        return false;
    }

    std::array<RuntimeGpuMaterialRecord, 3U> source{};
    source[0].base_color[0] = 0.125F;
    source[0].metallic = 0.75F;
    source[0].base_color_texture = 4U;
    source[0].sampler = 1U;
    source[1].base_color[1] = 0.5F;
    source[1].roughness = 0.2F;
    source[1].normal_texture = 3U;
    source[1].sampler = 0U;
    source[2].emissive_color[2] = 0.9F;
    source[2].emissive_intensity = 8.0F;
    source[2].emissive_texture = 2U;
    source[2].sampler = 2U;

    auto commands = device->create_command_list();
    const std::uint64_t bytes = sizeof(source);
    auto readback = make_readback(*device, bytes);
    if (!commands || !readback ||
        !table.upload(*commands, source, &diagnostics)) {
        std::cerr << "Could not encode GPU material upload: " << diagnostics << '\n';
        return false;
    }
    const auto gpu = table.result();
    if (gpu.records == nullptr || gpu.material_count != source.size() ||
        gpu.capacity < source.size() ||
        !commands->copy_buffer(*gpu.records, 0U, *readback, 0U, bytes)) {
        std::cerr << "GPU material table exposed invalid upload state.\n";
        return false;
    }
    const auto submission = device->submit(std::move(commands));
    if (submission == render::invalid_submission || !device->wait_for_submission(submission)) {
        std::cerr << "GPU material upload submission failed.\n";
        return false;
    }

    std::array<RuntimeGpuMaterialRecord, 3U> actual{};
    if (!readback->read(0U, std::as_writable_bytes(std::span{actual})) ||
        std::memcmp(source.data(), actual.data(), sizeof(source)) != 0) {
        std::cerr << "GPU material readback did not match source records.\n";
        return false;
    }

    auto empty_commands = device->create_command_list();
    if (!empty_commands || !table.upload(*empty_commands, {}, &diagnostics) ||
        table.result().material_count != 0U) {
        std::cerr << "Empty GPU material upload did not reset the logical count.\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    if (!test_plan()) return 1;
    if (!test_gpu_upload()) return 2;
    return 0;
}
