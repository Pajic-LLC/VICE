#include <vice/runtime/ray_tracing_scene_executor.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

[[nodiscard]] bool same_tlas_topology(
    const std::span<const RuntimeRayTracingBlasKey> previous_blases,
    const std::span<const RuntimeRayTracingBlasKey> current_blases,
    const std::span<const RuntimeRayTracingInstancePlan> previous_instances,
    const std::span<const RuntimeRayTracingInstancePlan> current_instances
) noexcept {
    if (previous_blases.size() != current_blases.size() ||
        previous_instances.size() != current_instances.size() ||
        !std::equal(previous_blases.begin(), previous_blases.end(), current_blases.begin())) {
        return false;
    }
    for (std::size_t index = 0U; index < current_instances.size(); ++index) {
        const auto& previous = previous_instances[index];
        const auto& current = current_instances[index];
        if (previous.entity != current.entity ||
            previous.instance_id != current.instance_id ||
            previous.blas_index != current.blas_index ||
            previous.mask != current.mask) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<RuntimeRayTracingBlasKey> blas_keys(
    const std::span<const RuntimeRayTracingBlasPlan> blases
) {
    std::vector<RuntimeRayTracingBlasKey> result;
    result.reserve(blases.size());
    for (const auto& blas : blases) result.push_back(blas.key);
    return result;
}

[[nodiscard]] constexpr std::uint32_t saturating_add(
    const std::uint32_t left,
    const std::uint32_t right
) noexcept {
    return right > std::numeric_limits<std::uint32_t>::max() - left
        ? std::numeric_limits<std::uint32_t>::max()
        : left + right;
}

[[nodiscard]] constexpr std::uint64_t saturating_add(
    const std::uint64_t left,
    const std::uint64_t right
) noexcept {
    return right > std::numeric_limits<std::uint64_t>::max() - left
        ? std::numeric_limits<std::uint64_t>::max()
        : left + right;
}

void accumulate_counters(
    RuntimeRayTracingExecutionCounters& total,
    const RuntimeRayTracingExecutionCounters& delta
) noexcept {
    total.blas_creates = saturating_add(total.blas_creates, delta.blas_creates);
    total.blas_reuses = saturating_add(total.blas_reuses, delta.blas_reuses);
    total.blas_rebuilds = saturating_add(total.blas_rebuilds, delta.blas_rebuilds);
    total.blas_evictions = saturating_add(total.blas_evictions, delta.blas_evictions);
    total.tlas_builds = saturating_add(total.tlas_builds, delta.tlas_builds);
    total.tlas_refits = saturating_add(total.tlas_refits, delta.tlas_refits);
    total.tlas_reuses = saturating_add(total.tlas_reuses, delta.tlas_reuses);
}

} // namespace

RuntimeRayTracingExecutionResult RuntimeRayTracingSceneExecutor::execute(
    render::Device& device,
    render::CommandList& commands,
    const RuntimeRayTracingScenePlan& plan,
    const RuntimeRayTracingGeometryResolver& geometry_resolver
) {
    RuntimeRayTracingExecutionResult result;
    if (!plan) {
        result.diagnostics = plan.diagnostics.empty()
            ? "Runtime ray tracing scene plan is invalid."
            : plan.diagnostics;
        return result;
    }

    if (!render::supports_acceleration_structures(device.capabilities())) {
        reset();
        return result;
    }
    result.active = true;

    if (!plan.blases.empty() && !geometry_resolver) {
        result.diagnostics = "Runtime ray tracing execution requires a geometry resolver.";
        return result;
    }

    for (const auto& instance : plan.instances) {
        if (instance.blas_index >= plan.blases.size() ||
            instance.instance_id > render::acceleration_structure_instance_id_max ||
            instance.mask > render::acceleration_structure_instance_mask_max) {
            result.diagnostics = "Runtime ray tracing scene plan contains an invalid TLAS instance.";
            return result;
        }
    }

    std::vector<CachedBlas> next_blases;
    next_blases.reserve(plan.blases.size());

    for (const auto& planned_blas : plan.blases) {
        const auto exact = std::find_if(blases_.begin(), blases_.end(), [&](const CachedBlas& cached) {
            return cached.key == planned_blas.key && cached.structure != nullptr;
        });
        if (exact != blases_.end()) {
            next_blases.push_back(*exact);
            ++result.counters.blas_reuses;
            continue;
        }

        const bool replaces_mesh_generation = std::any_of(
            blases_.begin(),
            blases_.end(),
            [&](const CachedBlas& cached) {
                return cached.key.mesh == planned_blas.key.mesh && cached.key != planned_blas.key;
            }
        );

        const auto binding = geometry_resolver(planned_blas.key);
        if (!binding.has_value()) {
            result.diagnostics = "Runtime ray tracing could not resolve resident geometry for a BLAS plan.";
            return result;
        }

        const render::AccelerationStructureTriangleGeometryDesc geometry{
            .vertex_buffer = binding->vertex_buffer,
            .vertex_offset = binding->vertex_offset,
            .vertex_count = planned_blas.key.vertex_count,
            .vertex_stride = binding->vertex_stride,
            .vertex_format = binding->vertex_format,
            .index_buffer = binding->index_buffer,
            .index_offset = binding->index_offset,
            .index_count = planned_blas.key.index_count,
            .index_format = binding->index_format,
            .opaque = binding->opaque,
        };
        const std::array geometries{geometry};
        const render::AccelerationStructureBuildDesc build_desc{
            .type = render::AccelerationStructureType::bottom_level,
            .flags = render::AccelerationStructureBuildFlags::allow_update |
                render::AccelerationStructureBuildFlags::prefer_fast_trace,
            .geometries = geometries,
            .instances = {},
        };
        if (!render::is_valid(build_desc)) {
            result.diagnostics = "Runtime ray tracing resolved invalid BLAS geometry.";
            return result;
        }

        render::AccelerationStructureBuildSizes sizes{};
        if (!render::query_acceleration_structure_build_sizes(device, build_desc, sizes)) {
            result.diagnostics = "Runtime ray tracing could not query BLAS build sizes.";
            return result;
        }
        auto native = render::create_acceleration_structure(device, build_desc, sizes);
        if (native == nullptr) {
            result.diagnostics = "Runtime ray tracing could not allocate a BLAS.";
            return result;
        }
        if (!render::build_acceleration_structure(commands, *native, build_desc)) {
            result.diagnostics = "Runtime ray tracing could not record a BLAS build.";
            return result;
        }

        std::shared_ptr<render::AccelerationStructure> structure{std::move(native)};
        next_blases.push_back(CachedBlas{
            .key = planned_blas.key,
            .structure = std::move(structure),
        });
        if (replaces_mesh_generation) {
            ++result.counters.blas_rebuilds;
        } else {
            ++result.counters.blas_creates;
        }
    }

    for (const auto& cached : blases_) {
        const bool retained = std::any_of(
            next_blases.begin(),
            next_blases.end(),
            [&](const CachedBlas& next) { return next.key == cached.key; }
        );
        if (!retained) ++result.counters.blas_evictions;
    }

    const std::vector<RuntimeRayTracingBlasKey> current_blas_keys = blas_keys(plan.blases);
    if (plan.instances.empty()) {
        blases_ = std::move(next_blases);
        tlas_.reset();
        previous_blas_keys_ = current_blas_keys;
        previous_instances_.clear();
        refresh_telemetry(true, result.counters);
        return result;
    }

    std::vector<render::AccelerationStructureInstanceDesc> native_instances;
    native_instances.reserve(plan.instances.size());
    for (const auto& instance : plan.instances) {
        if (instance.blas_index >= next_blases.size() ||
            next_blases[instance.blas_index].structure == nullptr) {
            result.diagnostics = "Runtime ray tracing TLAS instance lost its planned BLAS.";
            return result;
        }
        native_instances.push_back(render::AccelerationStructureInstanceDesc{
            .bottom_level = next_blases[instance.blas_index].structure.get(),
            .transform = instance.transform,
            .instance_id = instance.instance_id,
            .mask = instance.mask,
        });
    }

    const render::AccelerationStructureBuildDesc tlas_desc{
        .type = render::AccelerationStructureType::top_level,
        .flags = render::AccelerationStructureBuildFlags::allow_update |
            render::AccelerationStructureBuildFlags::prefer_fast_trace,
        .geometries = {},
        .instances = native_instances,
    };
    if (!render::is_valid(tlas_desc)) {
        result.diagnostics = "Runtime ray tracing produced an invalid TLAS description.";
        return result;
    }

    const bool topology_matches = tlas_ != nullptr && same_tlas_topology(
        previous_blas_keys_,
        current_blas_keys,
        previous_instances_,
        plan.instances
    );
    if (topology_matches && previous_instances_ == plan.instances) {
        blases_ = std::move(next_blases);
        previous_blas_keys_ = current_blas_keys;
        previous_instances_ = plan.instances;
        ++result.counters.tlas_reuses;
        refresh_telemetry(true, result.counters);
        return result;
    }

    render::AccelerationStructureBuildSizes tlas_sizes{};
    if (!render::query_acceleration_structure_build_sizes(device, tlas_desc, tlas_sizes)) {
        result.diagnostics = "Runtime ray tracing could not query TLAS build sizes.";
        return result;
    }
    auto next_tlas_native = render::create_acceleration_structure(device, tlas_desc, tlas_sizes);
    if (next_tlas_native == nullptr) {
        result.diagnostics = "Runtime ray tracing could not allocate a TLAS.";
        return result;
    }

    render::AccelerationStructure* source = topology_matches ? tlas_.get() : nullptr;
    if (!render::build_acceleration_structure(commands, *next_tlas_native, tlas_desc, source)) {
        result.diagnostics = topology_matches
            ? "Runtime ray tracing could not record a TLAS refit."
            : "Runtime ray tracing could not record a TLAS build.";
        return result;
    }

    std::shared_ptr<render::AccelerationStructure> next_tlas{std::move(next_tlas_native)};
    blases_ = std::move(next_blases);
    tlas_ = std::move(next_tlas);
    previous_blas_keys_ = current_blas_keys;
    previous_instances_ = plan.instances;
    if (topology_matches) {
        ++result.counters.tlas_refits;
    } else {
        ++result.counters.tlas_builds;
    }
    refresh_telemetry(true, result.counters);
    return result;
}

void RuntimeRayTracingSceneExecutor::reset() noexcept {
    blases_.clear();
    tlas_.reset();
    previous_blas_keys_.clear();
    previous_instances_.clear();
    telemetry_ = {};
}

void RuntimeRayTracingSceneExecutor::refresh_telemetry(
    const bool supported,
    const RuntimeRayTracingExecutionCounters& last_successful_execution
) noexcept {
    telemetry_.supported = supported;
    telemetry_.scene_active = tlas_ != nullptr;
    telemetry_.blas_count = blases_.size();
    telemetry_.tlas_instance_count = tlas_ == nullptr ? 0U : previous_instances_.size();
    telemetry_.blas_bytes = 0U;
    for (const auto& cached : blases_) {
        if (cached.structure != nullptr) {
            telemetry_.blas_bytes = saturating_add(
                telemetry_.blas_bytes,
                cached.structure->size()
            );
        }
    }
    telemetry_.tlas_bytes = tlas_ == nullptr ? 0U : tlas_->size();
    telemetry_.total_bytes = saturating_add(telemetry_.blas_bytes, telemetry_.tlas_bytes);
    telemetry_.last_successful_execution = last_successful_execution;
    accumulate_counters(telemetry_.cumulative, last_successful_execution);
}

} // namespace vice::runtime
