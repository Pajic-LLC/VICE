#pragma once

#include <vice/render/ray_tracing.hpp>
#include <vice/runtime/ray_tracing_scene.hpp>
#include <vice/runtime/ray_tracing_telemetry.hpp>

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
    [[nodiscard]] std::span<const std::uint32_t> active_scene_instance_indices() const noexcept {
        return active_scene_instance_indices_;
    }
    [[nodiscard]] render::Buffer* active_scene_instance_index_buffer() noexcept {
        return active_scene_instance_index_buffer_.get();
    }
    [[nodiscard]] const render::Buffer* active_scene_instance_index_buffer() const noexcept {
        return active_scene_instance_index_buffer_.get();
    }
    [[nodiscard]] std::uint32_t active_scene_instance_index(
        const std::uint32_t tlas_instance_index
    ) const noexcept {
        return tlas_ != nullptr && tlas_instance_index < active_scene_instance_indices_.size()
            ? active_scene_instance_indices_[tlas_instance_index]
            : runtime_ray_tracing_invalid_scene_instance;
    }
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
    // This mapping is committed only together with a successful active TLAS
    // state. It may change while the TLAS itself is reused because authoritative
    // Runtime scene-array order is not part of TLAS topology or identity.
    std::vector<std::uint32_t> active_scene_instance_indices_;
    // The upload buffer is retained with the GPU-only map so recorded copy work
    // cannot outlive the staging resource. Both buffers are replaced only after
    // the new mapping has been recorded successfully.
    std::unique_ptr<render::Buffer> active_scene_instance_index_upload_;
    std::unique_ptr<render::Buffer> active_scene_instance_index_buffer_;
    RuntimeRayTracingTelemetry telemetry_{};
};

} // namespace vice::runtime