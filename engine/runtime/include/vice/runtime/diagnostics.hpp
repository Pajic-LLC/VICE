#pragma once

#include <vice/jobs/jobs.hpp>
#include <vice/render/telemetry.hpp>
#include <vice/runtime/directional_shadow.hpp>
#include <vice/runtime/ray_tracing_telemetry.hpp>
#include <vice/simulation/simulation.hpp>
#include <vice/world/world.hpp>

#include <cstddef>
#include <optional>

namespace vice::runtime {

class RuntimeSceneRenderer;

struct RuntimeWorldDiagnostics {
    std::size_t entities = 0U;
    std::size_t resources = 0U;
    std::size_t relations = 0U;
    std::size_t regions = 0U;
    std::size_t full_regions = 0U;
    std::size_t reduced_regions = 0U;
    std::size_t inactive_regions = 0U;

    friend bool operator==(const RuntimeWorldDiagnostics&, const RuntimeWorldDiagnostics&) = default;
};

struct RuntimeRendererDiagnostics {
    std::optional<RuntimeDirectionalShadowTelemetry> directional_shadow;
    std::optional<RuntimeRayTracingTelemetry> ray_tracing;

    [[nodiscard]] bool available() const noexcept {
        return directional_shadow.has_value() || ray_tracing.has_value();
    }
};

struct RuntimeDiagnosticsSnapshot {
    double frame_seconds = 0.0;
    double frames_per_second = 0.0;
    jobs::SchedulerStats jobs{};
    std::optional<simulation::CoordinatorStats> simulation;
    render::DeviceStats render{};
    RuntimeWorldDiagnostics world{};
    RuntimeRendererDiagnostics renderer{};
};

[[nodiscard]] RuntimeWorldDiagnostics summarize_runtime_world(const world::World& world);

[[nodiscard]] RuntimeRendererDiagnostics runtime_renderer_diagnostics(
    const RuntimeSceneRenderer* scene_renderer
) noexcept;

[[nodiscard]] RuntimeDiagnosticsSnapshot sample_runtime_diagnostics(
    double frame_seconds,
    const jobs::Scheduler& jobs,
    const simulation::Coordinator* simulation,
    const render::InstrumentedDevice& render_device,
    const world::World& world,
    const RuntimeSceneRenderer* scene_renderer = nullptr
);

} // namespace vice::runtime
