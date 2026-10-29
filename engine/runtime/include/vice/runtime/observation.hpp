#pragma once

#include <vice/platform/types.hpp>
#include <vice/render/telemetry.hpp>

#include <cstdint>

namespace vice::rigid_body { class AuthoredSimulation; }
namespace vice::presentation { class System; }

namespace vice::runtime {

class RuntimeSceneRenderer;

enum class RuntimeExecutionState : std::uint8_t {
    starting,
    running,
    stopping,
};

struct RuntimeFrameObservation {
    RuntimeExecutionState execution_state = RuntimeExecutionState::starting;
    std::uint64_t frame_index = 0U;
    double frame_seconds = 0.0;
    double update_seconds = 0.0;
    double render_seconds = 0.0;
    render::Backend backend = render::Backend::metal;
    render::DeviceCapabilities device_capabilities{};
    render::DeviceStats device_stats{};
    platform::WindowMetrics metrics{};
    const RuntimeSceneRenderer* scene_renderer = nullptr;
    const rigid_body::AuthoredSimulation* physics = nullptr;
    const presentation::System* presentation = nullptr;
};

class RuntimeObserver {
public:
    virtual ~RuntimeObserver() = default;
    virtual void on_runtime_observation(const RuntimeFrameObservation& observation) noexcept = 0;
};

} // namespace vice::runtime
