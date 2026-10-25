#include <vice/runtime/ray_tracing_scene_executor.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace {

using namespace vice;
using namespace vice::render;
using namespace vice::runtime;

class TestBuffer final : public Buffer {
public:
    TestBuffer(const BufferDesc& desc, const bool writable = true)
        : size_{desc.size}, usage_{desc.usage}, memory_{desc.memory}, writable_{writable}, bytes_(desc.size) {}

    TestBuffer(const std::uint64_t size, const BufferUsage usage)
        : size_{size}, usage_{usage}, memory_{MemoryLocation::gpu_only}, bytes_(size) {}

    [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }
    [[nodiscard]] BufferUsage usage() const noexcept override { return usage_; }
    [[nodiscard]] MemoryLocation memory_location() const noexcept override { return memory_; }

    bool write(const std::uint64_t offset, const std::span<const std::byte> bytes) override {
        if (!writable_ || offset > bytes_.size() || bytes.size() > bytes_.size() - offset) return false;
        std::memcpy(bytes_.data() + offset, bytes.data(), bytes.size());
        return true;
    }

    bool read(const std::uint64_t offset, const std::span<std::byte> bytes) override {
        if (offset > bytes_.size() || bytes.size() > bytes_.size() - offset) return false;
        std::memcpy(bytes.data(), bytes_.data() + offset, bytes.size());
        return true;
    }

    bool copy_from(
        const TestBuffer& source,
        const std::uint64_t source_offset,
        const std::uint64_t destination_offset,
        const std::uint64_t size
    ) {
        if (source_offset > source.bytes_.size() || size > source.bytes_.size() - source_offset ||
            destination_offset > bytes_.size() || size > bytes_.size() - destination_offset) {
            return false;
        }
        std::memcpy(
            bytes_.data() + destination_offset,
            source.bytes_.data() + source_offset,
            static_cast<std::size_t>(size)
        );
        return true;
    }

private:
    std::uint64_t size_ = 0U;
    BufferUsage usage_ = BufferUsage::none;
    MemoryLocation memory_ = MemoryLocation::gpu_only;
    bool writable_ = true;
    std::vector<std::byte> bytes_;
};

class TestAccelerationStructure final : public AccelerationStructure {
public:
    TestAccelerationStructure(const AccelerationStructureType type, const std::uint64_t size)
        : type_{type}, size_{size} {}

    [[nodiscard]] AccelerationStructureType type() const noexcept override { return type_; }
    [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }

private:
    AccelerationStructureType type_ = AccelerationStructureType::bottom_level;
    std::uint64_t size_ = 0U;
};

class TestCommandList : public CommandList {
public:
    bool copy_buffer(
        Buffer& source,
        const std::uint64_t source_offset,
        Buffer& destination,
        const std::uint64_t destination_offset,
        const std::uint64_t size
    ) override {
        if (fail_next_copy) {
            fail_next_copy = false;
            return false;
        }
        auto* const test_source = dynamic_cast<TestBuffer*>(&source);
        auto* const test_destination = dynamic_cast<TestBuffer*>(&destination);
        if (test_source == nullptr || test_destination == nullptr ||
            !test_destination->copy_from(*test_source, source_offset, destination_offset, size)) {
            return false;
        }
        ++copy_count;
        return true;
    }
    bool begin_render_pass(Swapchain&, const RenderPassDesc&) override { return false; }
    bool begin_render_pass(Texture&, const RenderPassDesc&) override { return false; }
    bool begin_depth_render_pass(Texture&, float, bool) override { return false; }
    bool set_viewport(const Viewport&) override { return false; }
    bool set_scissor_rect(const ScissorRect&) override { return false; }
    bool set_graphics_pipeline(GraphicsPipeline&) override { return false; }
    bool set_graphics_constant_buffer(const ShaderBindingKey&, Buffer&, std::uint64_t, std::uint64_t) override {
        return false;
    }
    bool set_graphics_buffer(const ShaderBindingKey&, Buffer&, const BufferViewDesc&) override {
        return false;
    }
    bool set_graphics_storage_buffer(const ShaderBindingKey&, Buffer&, const BufferViewDesc&) override {
        return false;
    }
    bool set_graphics_texture(const ShaderBindingKey&, Texture&) override { return false; }
    bool set_graphics_storage_texture(const ShaderBindingKey&, Texture&) override { return false; }
    bool set_graphics_sampler(const ShaderBindingKey&, Sampler&) override { return false; }
    bool draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { return false; }
    bool end_render_pass() override { return false; }
    bool present(Swapchain&) override { return false; }

    bool fail_next_copy = false;
    std::uint32_t copy_count = 0U;
};

class TestDevice : public Device {
public:
    [[nodiscard]] Backend backend() const noexcept override { return Backend::direct3d12; }
    [[nodiscard]] DeviceCapabilities capabilities() const noexcept override { return {}; }
    [[nodiscard]] std::unique_ptr<Buffer> create_buffer(const BufferDesc& desc) override {
        if (fail_next_buffer_allocation) {
            fail_next_buffer_allocation = false;
            return nullptr;
        }
        const bool writable = !fail_next_buffer_write;
        fail_next_buffer_write = false;
        ++buffer_create_count;
        return std::make_unique<TestBuffer>(desc, writable);
    }
    [[nodiscard]] std::unique_ptr<Texture> create_texture(const TextureDesc&) override { return nullptr; }
    [[nodiscard]] std::unique_ptr<Sampler> create_sampler(const SamplerDesc&) override { return nullptr; }
    [[nodiscard]] std::unique_ptr<ShaderModule> create_shader_module(const ShaderModuleDesc&) override {
        return nullptr;
    }
    [[nodiscard]] std::unique_ptr<ShaderModule> create_shader_module(const ShaderBinaryDesc&) override {
        return nullptr;
    }
    [[nodiscard]] std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(const GraphicsPipelineDesc&) override {
        return nullptr;
    }
    [[nodiscard]] std::unique_ptr<CommandList> create_command_list() override { return nullptr; }
    SubmissionId submit(std::unique_ptr<CommandList>) override { return invalid_submission; }
    [[nodiscard]] SubmissionId completed_submission() const noexcept override { return invalid_submission; }
    bool wait_for_submission(SubmissionId) override { return false; }
    bool wait_idle() override { return false; }
    [[nodiscard]] std::unique_ptr<Swapchain> create_swapchain(platform::Window&) override { return nullptr; }

    bool fail_next_buffer_allocation = false;
    bool fail_next_buffer_write = false;
    std::uint32_t buffer_create_count = 0U;
};

class TestRayDevice final : public TestDevice, public RayTracingDevice {
public:
    [[nodiscard]] DeviceCapabilities capabilities() const noexcept override {
        DeviceCapabilities result{};
        result.ray_tracing = true;
        return result;
    }

    bool query_acceleration_structure_build_sizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureBuildSizes& sizes
    ) override {
        if (!is_valid(desc)) return false;
        ++query_count;
        sizes = {
            .result_size = desc.type == AccelerationStructureType::bottom_level ? 4096U : 8192U,
            .build_scratch_size = 2048U,
            .update_scratch_size = 1024U,
        };
        return true;
    }

    [[nodiscard]] std::unique_ptr<AccelerationStructure> create_acceleration_structure(
        const AccelerationStructureBuildDesc& desc,
        const AccelerationStructureBuildSizes& sizes
    ) override {
        if (!is_valid(desc) || !is_valid(sizes)) return nullptr;
        ++create_count;
        return std::make_unique<TestAccelerationStructure>(desc.type, sizes.result_size);
    }

    std::uint32_t query_count = 0U;
    std::uint32_t create_count = 0U;
};

struct RecordedBuild {
    AccelerationStructureType type = AccelerationStructureType::bottom_level;
    bool update = false;
    std::vector<std::uint32_t> instance_ids;
    std::vector<std::uint32_t> masks;
    std::vector<std::array<float, 12>> transforms;
};

class TestRayCommandList final : public TestCommandList, public RayTracingCommandList {
public:
    bool build_acceleration_structure(
        AccelerationStructure& destination,
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructure* source
    ) override {
        if (!is_valid(desc) || destination.type() != desc.type) return false;
        RecordedBuild recorded{
            .type = desc.type,
            .update = source != nullptr,
        };
        for (const auto& instance : desc.instances) {
            recorded.instance_ids.push_back(instance.instance_id);
            recorded.masks.push_back(instance.mask);
            recorded.transforms.push_back(instance.transform);
        }
        builds.push_back(std::move(recorded));
        return true;
    }

    std::vector<RecordedBuild> builds;
};

[[nodiscard]] world::PersistentId persistent_id(const std::uint8_t value) {
    world::PersistentId::Bytes bytes{};
    bytes.back() = value;
    return world::PersistentId{bytes};
}

[[nodiscard]] world::ResourceId resource_id(const std::uint8_t value) {
    return world::ResourceId{.value = persistent_id(value)};
}

[[nodiscard]] RuntimeRayTracingBlasKey blas_key(
    const std::uint8_t resource,
    const std::uint8_t stable,
    const std::uint64_t fingerprint
) {
    RuntimeRayTracingBlasKey result{
        .mesh = resource_id(resource),
        .source_fingerprint = fingerprint,
        .vertex_count = 24U,
        .index_count = 36U,
    };
    result.stable_id.back() = stable;
    return result;
}

[[nodiscard]] RuntimeRayTracingInstancePlan planned_instance(
    const std::uint8_t entity,
    const std::uint32_t instance_id,
    const std::uint32_t blas_index,
    const float x,
    const std::uint32_t mask
) {
    RuntimeRayTracingInstancePlan result{
        .entity = persistent_id(entity),
        .instance_id = instance_id,
        .blas_index = blas_index,
        .mask = mask,
    };
    result.transform[3] = x;
    return result;
}

[[nodiscard]] std::vector<std::uint32_t> mapped_indices(Buffer* const buffer, const std::size_t count) {
    assert(buffer != nullptr);
    std::vector<std::uint32_t> result(count);
    assert(buffer->read(0U, std::as_writable_bytes(std::span{result})));
    return result;
}

} // namespace

int main() {
    TestBuffer vertices{4096U, BufferUsage::vertex};
    TestBuffer indices{2048U, BufferUsage::index};
    const RuntimeRayTracingGeometryResolver geometry = [&](const RuntimeRayTracingBlasKey&) {
        return std::optional<RuntimeRayTracingGeometryBinding>{RuntimeRayTracingGeometryBinding{
            .vertex_buffer = &vertices,
            .vertex_offset = 0U,
            .vertex_stride = 32U,
            .vertex_format = VertexFormat::float32x3,
            .index_buffer = &indices,
            .index_offset = 0U,
            .index_format = IndexFormat::uint32,
            .opaque = true,
        }};
    };

    RuntimeRayTracingScenePlan plan;
    plan.blases.push_back(RuntimeRayTracingBlasPlan{.key = blas_key(1U, 11U, 101U)});
    plan.instances.push_back(planned_instance(1U, 17U, 0U, 4.0F, 0xA5U));
    plan.instances.push_back(planned_instance(2U, 18U, 0U, 8.0F, 0xFFU));
    plan.scene_instance_indices = {5U, 9U};

    RuntimeRayTracingSceneExecutor executor;
    assert(executor.active_tlas() == nullptr);
    assert(executor.active_scene_instance_indices().empty());
    assert(executor.active_scene_instance_index_buffer() == nullptr);
    assert(executor.telemetry() == RuntimeRayTracingTelemetry{});
    const RuntimeRayTracingSceneExecutor& const_executor = executor;
    assert(const_executor.active_tlas() == nullptr);
    assert(const_executor.active_scene_instance_index_buffer() == nullptr);
    assert(const_executor.telemetry() == RuntimeRayTracingTelemetry{});
    TestRayCommandList commands;
    TestDevice unsupported;
    const auto unsupported_result = executor.execute(unsupported, commands, plan, geometry);
    assert(unsupported_result);
    assert(!unsupported_result.active);
    assert(commands.builds.empty());
    assert(executor.cached_blas_count() == 0U);
    assert(!executor.has_tlas());
    assert(executor.active_tlas() == nullptr);
    assert(executor.active_scene_instance_indices().empty());
    assert(executor.active_scene_instance_index_buffer() == nullptr);
    assert(executor.telemetry() == RuntimeRayTracingTelemetry{});

    TestRayDevice device;
    const auto first = executor.execute(device, commands, plan, geometry);
    assert(first);
    assert(first.active);
    assert(first.counters.blas_creates == 1U);
    assert(first.counters.blas_reuses == 0U);
    assert(first.counters.tlas_builds == 1U);
    assert(device.create_count == 2U);
    assert(device.buffer_create_count == 2U);
    assert(commands.copy_count == 1U);
    assert(commands.builds.size() == 2U);
    assert(commands.builds[0].type == AccelerationStructureType::bottom_level);
    assert(commands.builds[1].type == AccelerationStructureType::top_level);
    assert(!commands.builds[1].update);
    assert((commands.builds[1].instance_ids == std::vector<std::uint32_t>{17U, 18U}));
    assert((commands.builds[1].masks == std::vector<std::uint32_t>{0xA5U, 0xFFU}));
    assert(commands.builds[1].transforms[0][3] == 4.0F);
    assert(commands.builds[1].transforms[1][3] == 8.0F);
    assert(executor.cached_blas_count() == 1U);
    assert(executor.has_tlas());
    assert(executor.active_tlas() != nullptr);
    assert(executor.active_tlas()->type() == AccelerationStructureType::top_level);
    assert((std::vector<std::uint32_t>{
        executor.active_scene_instance_indices().begin(),
        executor.active_scene_instance_indices().end()
    } == plan.scene_instance_indices));
    assert(executor.active_scene_instance_index_buffer() != nullptr);
    assert(executor.active_scene_instance_index_buffer()->usage() ==
        (BufferUsage::storage | BufferUsage::copy_destination));
    assert(executor.active_scene_instance_index_buffer()->memory_location() == MemoryLocation::gpu_only);
    assert(mapped_indices(executor.active_scene_instance_index_buffer(), 2U) == plan.scene_instance_indices);
    assert(const_executor.active_tlas() == executor.active_tlas());
    assert(const_executor.active_scene_instance_index_buffer() == executor.active_scene_instance_index_buffer());
    assert(const_executor.telemetry().supported);
    assert(const_executor.telemetry().scene_active);
    assert(const_executor.telemetry().blas_count == 1U);
    assert(const_executor.telemetry().tlas_instance_count == 2U);
    assert(const_executor.telemetry().blas_bytes == 4096U);
    assert(const_executor.telemetry().tlas_bytes == 8192U);
    assert(const_executor.telemetry().total_bytes == 12288U);
    assert(const_executor.telemetry().last_successful_execution == first.counters);
    assert(const_executor.telemetry().cumulative == first.counters);

    auto* const first_map = executor.active_scene_instance_index_buffer();
    const auto unchanged = executor.execute(device, commands, plan, geometry);
    assert(unchanged);
    assert(unchanged.counters.blas_reuses == 1U);
    assert(unchanged.counters.tlas_reuses == 1U);
    assert(device.create_count == 2U);
    assert(device.buffer_create_count == 2U);
    assert(commands.copy_count == 1U);
    assert(commands.builds.size() == 2U);
    assert(executor.active_tlas() != nullptr);
    assert(executor.active_scene_instance_index_buffer() == first_map);
    assert(executor.telemetry().last_successful_execution == unchanged.counters);
    assert(executor.telemetry().cumulative.blas_creates == 1U);
    assert(executor.telemetry().cumulative.blas_reuses == 1U);
    assert(executor.telemetry().cumulative.tlas_builds == 1U);
    assert(executor.telemetry().cumulative.tlas_reuses == 1U);
    assert(executor.telemetry().total_bytes == 12288U);

    auto remapped_plan = plan;
    remapped_plan.scene_instance_indices = {9U, 5U};
    const auto remapped = executor.execute(device, commands, remapped_plan, geometry);
    assert(remapped);
    assert(remapped.counters.blas_reuses == 1U);
    assert(remapped.counters.tlas_reuses == 1U);
    assert(device.create_count == 2U);
    assert(device.buffer_create_count == 4U);
    assert(commands.copy_count == 2U);
    assert(commands.builds.size() == 2U);
    assert(executor.active_scene_instance_index_buffer() != first_map);
    assert((std::vector<std::uint32_t>{
        executor.active_scene_instance_indices().begin(),
        executor.active_scene_instance_indices().end()
    } == remapped_plan.scene_instance_indices));
    assert(mapped_indices(executor.active_scene_instance_index_buffer(), 2U) == remapped_plan.scene_instance_indices);

    auto* const committed_map = executor.active_scene_instance_index_buffer();
    const auto committed_indices = std::vector<std::uint32_t>{
        executor.active_scene_instance_indices().begin(),
        executor.active_scene_instance_indices().end()
    };
    auto failed_map_plan = remapped_plan;
    failed_map_plan.scene_instance_indices = {11U, 13U};

    device.fail_next_buffer_allocation = true;
    const auto allocation_failed = executor.execute(device, commands, failed_map_plan, geometry);
    assert(!allocation_failed);
    assert(executor.active_scene_instance_index_buffer() == committed_map);
    assert((std::vector<std::uint32_t>{
        executor.active_scene_instance_indices().begin(),
        executor.active_scene_instance_indices().end()
    } == committed_indices));
    assert(mapped_indices(executor.active_scene_instance_index_buffer(), 2U) == committed_indices);
    assert(commands.copy_count == 2U);
    assert(commands.builds.size() == 2U);

    device.fail_next_buffer_write = true;
    const auto write_failed = executor.execute(device, commands, failed_map_plan, geometry);
    assert(!write_failed);
    assert(executor.active_scene_instance_index_buffer() == committed_map);
    assert((std::vector<std::uint32_t>{
        executor.active_scene_instance_indices().begin(),
        executor.active_scene_instance_indices().end()
    } == committed_indices));
    assert(mapped_indices(executor.active_scene_instance_index_buffer(), 2U) == committed_indices);
    assert(commands.copy_count == 2U);
    assert(commands.builds.size() == 2U);

    commands.fail_next_copy = true;
    const auto copy_failed = executor.execute(device, commands, failed_map_plan, geometry);
    assert(!copy_failed);
    assert(executor.active_scene_instance_index_buffer() == committed_map);
    assert((std::vector<std::uint32_t>{
        executor.active_scene_instance_indices().begin(),
        executor.active_scene_instance_indices().end()
    } == committed_indices));
    assert(mapped_indices(executor.active_scene_instance_index_buffer(), 2U) == committed_indices);
    assert(commands.copy_count == 2U);
    assert(commands.builds.size() == 2U);

    auto moved_plan = remapped_plan;
    moved_plan.instances[0].transform[3] = 12.0F;
    const auto moved = executor.execute(device, commands, moved_plan, geometry);
    assert(moved);
    assert(moved.counters.blas_reuses == 1U);
    assert(moved.counters.tlas_refits == 1U);
    assert(device.create_count == 3U);
    assert(commands.builds.size() == 3U);
    assert(commands.builds.back().type == AccelerationStructureType::top_level);
    assert(commands.builds.back().update);
    assert(commands.builds.back().transforms[0][3] == 12.0F);
    assert(executor.active_tlas() != nullptr);
    assert(executor.active_tlas()->type() == AccelerationStructureType::top_level);
    assert(executor.active_scene_instance_index_buffer() == committed_map);
    assert(executor.telemetry().last_successful_execution == moved.counters);
    assert(executor.telemetry().cumulative.tlas_refits == 1U);
    assert(executor.telemetry().tlas_instance_count == 2U);
    assert(executor.telemetry().total_bytes == 12288U);

    auto regenerated_plan = moved_plan;
    regenerated_plan.blases[0].key.source_fingerprint = 999U;
    const auto regenerated = executor.execute(device, commands, regenerated_plan, geometry);
    assert(regenerated);
    assert(regenerated.counters.blas_rebuilds == 1U);
    assert(regenerated.counters.blas_evictions == 1U);
    assert(regenerated.counters.tlas_builds == 1U);
    assert(regenerated.counters.tlas_refits == 0U);
    assert(executor.cached_blas_count() == 1U);
    assert(commands.builds.size() == 5U);
    assert(commands.builds[3].type == AccelerationStructureType::bottom_level);
    assert(commands.builds[4].type == AccelerationStructureType::top_level);
    assert(!commands.builds[4].update);
    assert(executor.active_tlas() != nullptr);
    assert(executor.active_scene_instance_index_buffer() == committed_map);
    assert(executor.telemetry().last_successful_execution == regenerated.counters);
    assert(executor.telemetry().blas_bytes == 4096U);
    assert(executor.telemetry().tlas_bytes == 8192U);

    const RuntimeRayTracingScenePlan empty_plan{};
    const auto emptied = executor.execute(device, commands, empty_plan, geometry);
    assert(emptied);
    assert(emptied.counters.blas_evictions == 1U);
    assert(executor.cached_blas_count() == 0U);
    assert(!executor.has_tlas());
    assert(executor.active_tlas() == nullptr);
    assert(executor.active_scene_instance_indices().empty());
    assert(executor.active_scene_instance_index_buffer() == nullptr);
    assert(executor.telemetry().supported);
    assert(!executor.telemetry().scene_active);
    assert(executor.telemetry().blas_count == 0U);
    assert(executor.telemetry().tlas_instance_count == 0U);
    assert(executor.telemetry().blas_bytes == 0U);
    assert(executor.telemetry().tlas_bytes == 0U);
    assert(executor.telemetry().total_bytes == 0U);
    assert(executor.telemetry().last_successful_execution == emptied.counters);

    const auto disabled_again = executor.execute(unsupported, commands, plan, geometry);
    assert(disabled_again);
    assert(!disabled_again.active);
    assert(executor.cached_blas_count() == 0U);
    assert(!executor.has_tlas());
    assert(executor.active_tlas() == nullptr);
    assert(executor.active_scene_instance_indices().empty());
    assert(executor.active_scene_instance_index_buffer() == nullptr);
    assert(executor.telemetry() == RuntimeRayTracingTelemetry{});

    TestRayCommandList reset_commands;
    const auto before_reset = executor.execute(device, reset_commands, plan, geometry);
    assert(before_reset);
    assert(before_reset.active);
    assert(executor.active_tlas() != nullptr);
    assert(executor.active_scene_instance_index_buffer() != nullptr);
    assert(executor.telemetry().supported);
    assert(executor.telemetry().scene_active);
    assert(executor.telemetry().cumulative == before_reset.counters);
    executor.reset();
    assert(executor.active_tlas() == nullptr);
    assert(executor.active_scene_instance_indices().empty());
    assert(executor.active_scene_instance_index_buffer() == nullptr);
    assert(!executor.has_tlas());
    assert(executor.telemetry() == RuntimeRayTracingTelemetry{});

    return 0;
}