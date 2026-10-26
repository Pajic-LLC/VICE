#pragma once

#include <vice/platform/types.hpp>
#include <vice/render/rhi.hpp>

#include <cstdint>

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
    platform::WindowMetrics metrics{};
    const RuntimeSceneRenderer* scene_renderer = nullptr;
};

class RuntimeObserver {
public:
    virtual ~RuntimeObserver() = default;
    virtual void on_runtime_observation(const RuntimeFrameObservation& observation) noexcept = 0;
};

} // namespace vice::runtime
