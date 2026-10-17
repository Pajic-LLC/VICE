#pragma once

#include <vice/editor/asset_index.hpp>
#include <vice/editor/state.hpp>
#include <vice/jobs/jobs.hpp>
#include <vice/rigid_body/debug.hpp>
#include <vice/rigid_body/world_bridge.hpp>
#include <vice/simulation/entity_bindings.hpp>
#include <vice/simulation/simulation.hpp>
#include <vice/world/world.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::editor {

enum class PreviewPlayback : std::uint8_t {
    stopped,
    paused,
    running,
};

struct PreviewBodySnapshot {
    world::PersistentId owner{};
    world::RegionId region{};
    world::GlobalPosition global_position{};
    world::LocalPosition local_position{};
    rigid_body::BodyDebugSnapshot body{};
};

struct PreviewCharacterSnapshot {
    world::PersistentId owner{};
    world::RegionId region{};
    world::GlobalPosition global_position{};
    world::LocalPosition local_position{};
    rigid_body::CharacterDebugSnapshot character{};
};

struct PreviewContactSnapshot {
    world::RegionId region{};
    world::GlobalPosition global_point{};
    rigid_body::ContactEvent contact{};
};

struct PreviewRegionSnapshot {
    world::RegionDesc region{};
    std::optional<world::GlobalPosition> global_minimum;
    std::optional<world::GlobalPosition> global_maximum;
};

struct PreviewSnapshot {
    PreviewPlayback playback = PreviewPlayback::stopped;
    std::uint64_t step_index = 0;
    std::vector<PreviewBodySnapshot> bodies;
    std::vector<PreviewCharacterSnapshot> characters;
    std::vector<PreviewContactSnapshot> contacts;
    std::vector<rigid_body::CharacterRegionEvent> character_region_events;
    std::vector<rigid_body::BoxRegionEvent> box_region_events;
    std::vector<rigid_body::RegionActivationRequest> activation_requests;
    std::vector<PreviewRegionSnapshot> regions;
};

class PreviewRuntime {
public:
    PreviewRuntime();
    ~PreviewRuntime();

    PreviewRuntime(const PreviewRuntime&) = delete;
    PreviewRuntime& operator=(const PreviewRuntime&) = delete;
    PreviewRuntime(PreviewRuntime&&) noexcept;
    PreviewRuntime& operator=(PreviewRuntime&&) noexcept;

    [[nodiscard]] bool reset(
        const world::World& authored_world,
        jobs::Scheduler& jobs,
        const AssetIndex& assets,
        std::string* diagnostics = nullptr
    );

    void stop() noexcept;
    void play() noexcept;
    void pause() noexcept;
    [[nodiscard]] bool step_once();
    [[nodiscard]] bool update(double delta_seconds);

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] PreviewPlayback playback() const noexcept;
    [[nodiscard]] const simulation::Coordinator* coordinator() const noexcept;
    [[nodiscard]] const world::World* world() const noexcept;
    [[nodiscard]] PreviewSnapshot snapshot() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

void reset_document_runtime(
    SelectionModel& selection,
    TransformHistory& history,
    PreviewRuntime& preview
) noexcept;

} // namespace vice::editor
