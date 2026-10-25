#pragma once

#include <cstddef>
#include <cstdint>

namespace vice::runtime {

struct RuntimeRayTracingExecutionCounters {
    std::uint32_t blas_creates = 0U;
    std::uint32_t blas_reuses = 0U;
    std::uint32_t blas_rebuilds = 0U;
    std::uint32_t blas_evictions = 0U;
    std::uint32_t tlas_builds = 0U;
    std::uint32_t tlas_refits = 0U;
    std::uint32_t tlas_reuses = 0U;

    friend bool operator==(
        const RuntimeRayTracingExecutionCounters&,
        const RuntimeRayTracingExecutionCounters&
    ) = default;
};

struct RuntimeRayTracingTelemetry {
    bool supported = false;
    bool scene_active = false;
    std::size_t blas_count = 0U;
    std::size_t tlas_instance_count = 0U;
    std::uint64_t blas_bytes = 0U;
    std::uint64_t tlas_bytes = 0U;
    std::uint64_t total_bytes = 0U;
    RuntimeRayTracingExecutionCounters last_successful_execution{};
    RuntimeRayTracingExecutionCounters cumulative{};

    friend bool operator==(
        const RuntimeRayTracingTelemetry&,
        const RuntimeRayTracingTelemetry&
    ) = default;
};

} // namespace vice::runtime
