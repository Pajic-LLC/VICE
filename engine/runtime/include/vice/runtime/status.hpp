#pragma once

#include <vice/platform/window.hpp>
#include <vice/render/rhi.hpp>
#include <vice/runtime/diagnostics.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/spatial.hpp>
#include <vice/world/world.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vice::runtime {

class RuntimeSceneRenderer;

enum class RuntimeExecutionState : std::uint8_t {
    starting,
    running,
    stopping,
};

struct RuntimeCameraDiagnostics {
    std::optional<world::PersistentId> entity;
    std::optional<world::GlobalPosition> global_position;
    std::optional<world::RegionId> region;

    friend bool operator==(const RuntimeCameraDiagnostics&, const RuntimeCameraDiagnostics&) = default;
};

struct RuntimePresentationDiagnostics {
    render::Backend backend = render::Backend::metal;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    bool drawable = false;

    friend bool operator==(
        const RuntimePresentationDiagnostics&,
        const RuntimePresentationDiagnostics&
    ) = default;
};

struct RuntimeStatusSnapshot {
    std::string project_id;
    std::string game_target;
    std::string startup_world;
    RuntimeExecutionState execution_state = RuntimeExecutionState::starting;
    std::uint64_t frame_index = 0U;
    double frame_seconds = 0.0;
    double frames_per_second = 0.0;
    RuntimeWorldDiagnostics world{};
    RuntimeCameraDiagnostics camera{};
    RuntimePresentationDiagnostics presentation{};
    RuntimeRendererDiagnostics renderer{};
};

[[nodiscard]] RuntimeCameraDiagnostics runtime_camera_diagnostics(
    const world::World& world,
    float aspect_ratio
) noexcept;

[[nodiscard]] RuntimeStatusSnapshot make_runtime_status_snapshot(
    std::string project_id,
    std::string game_target,
    std::string startup_world,
    RuntimeExecutionState execution_state,
    std::uint64_t frame_index,
    double frame_seconds,
    render::Backend backend,
    platform::WindowMetrics metrics,
    const world::World& world,
    const RuntimeSceneRenderer* scene_renderer = nullptr
);

[[nodiscard]] std::string serialize_runtime_status_snapshot(
    const RuntimeStatusSnapshot& snapshot,
    std::string_view session_id
);

} // namespace vice::runtime
