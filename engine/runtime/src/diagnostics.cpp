#include <vice/runtime/diagnostics.hpp>

#include <vice/runtime/scene.hpp>

#include <cmath>

namespace vice::runtime {

RuntimeWorldDiagnostics summarize_runtime_world(const world::World& world) {
    const auto snapshot = world.snapshot();
    RuntimeWorldDiagnostics result;
    result.entities = snapshot.entities.size();
    result.resources = snapshot.resources.size();
    result.relations = snapshot.relations.size();
    result.regions = snapshot.regions.size();
    for (const auto& region : snapshot.regions) {
        switch (region.fidelity) {
        case world::RegionFidelity::full: ++result.full_regions; break;
        case world::RegionFidelity::reduced: ++result.reduced_regions; break;
        case world::RegionFidelity::inactive: ++result.inactive_regions; break;
        }
    }
    return result;
}

RuntimeDiagnosticsSnapshot sample_runtime_diagnostics(
    const double frame_seconds,
    const jobs::Scheduler& jobs,
    const simulation::Coordinator* simulation,
    const render::InstrumentedDevice& render_device,
    const world::World& world,
    const RuntimeSceneRenderer* scene_renderer
) {
    RuntimeDiagnosticsSnapshot result;
    if (std::isfinite(frame_seconds) && frame_seconds > 0.0) {
        result.frame_seconds = frame_seconds;
        result.frames_per_second = 1.0 / frame_seconds;
    }
    result.jobs = jobs.stats();
    if (simulation != nullptr) result.simulation = simulation->stats();
    result.render = render_device.stats();
    result.world = summarize_runtime_world(world);
    if (scene_renderer != nullptr) {
        result.directional_shadow = scene_renderer->directional_shadow_telemetry();
        result.ray_tracing = scene_renderer->ray_tracing_telemetry();
    }
    return result;
}

} // namespace vice::runtime
