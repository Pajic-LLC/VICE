#pragma once

#include <vice/render/ray_tracing.hpp>
#include <vice/runtime/ray_tracing_scene.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeRayTracingGeometryBinding {
    render::Buffer* vertex_buffer = nullptr;
    std::uint64_t vertex_offset = 0U;
    std::uint32_t vertex_stride = 0U;
    render::VertexFormat vertex_format = render::VertexFormat::float32x3;
    render::Buffer* index_buffer = nullptr;
    std::uint64_t index_offset = 0U;
    render::IndexFormat index_format = render::IndexFormat::uint32;
    bool opaque = true;
};

using RuntimeRayTracingGeometryResolver = std::function<std::optional<RuntimeRayTracingGeometryBinding>(
    const RuntimeRayTracingBlasKey&
)>;

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

    friend bool operator==(const RuntimeRayTracingTelemetry&, const RuntimeRayTracingTelemetry&) = default;
};

struct RuntimeRayTracingExecutionResult {
    RuntimeRayTracingExecutionCounters counters{};
    bool active = false;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

class RuntimeRayTracingSceneExecutor {
public:
    [[nodiscard]] RuntimeRayTracingExecutionResult execute(
        render::Device& device,
        render::CommandList& commands,
        const RuntimeRayTracingScenePlan& plan,
        const RuntimeRayTracingGeometryResolver& geometry_resolver
    );

    void reset() noexcept;

    [[nodiscard]] std::size_t cached_blas_count() const noexcept { return blases_.size(); }
    [[nodiscard]] bool has_tlas() const noexcept { return tlas_ != nullptr; }
    [[nodiscard]] render::AccelerationStructure* active_tlas() noexcept { return tlas_.get(); }
    [[nodiscard]] const render::AccelerationStructure* active_tlas() const noexcept { return tlas_.get(); }
    [[nodiscard]] const RuntimeRayTracingTelemetry& telemetry() const noexcept { return telemetry_; }

private:
    struct CachedBlas {
        RuntimeRayTracingBlasKey key{};
        std::shared_ptr<render::AccelerationStructure> structure;
    };

    void refresh_telemetry(
        bool supported,
        const RuntimeRayTracingExecutionCounters& last_successful_execution
    ) noexcept;

    std::vector<CachedBlas> blases_;
    std::shared_ptr<render::AccelerationStructure> tlas_;
    std::vector<RuntimeRayTracingBlasKey> previous_blas_keys_;
    std::vector<RuntimeRayTracingInstancePlan> previous_instances_;
    RuntimeRayTracingTelemetry telemetry_{};
};

} // namespace vice::runtime
