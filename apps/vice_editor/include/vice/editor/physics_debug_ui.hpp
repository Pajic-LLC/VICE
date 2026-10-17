#pragma once

#include <vice/editor/preview_runtime.hpp>
#include <vice/editor/state.hpp>

#include <memory>

namespace vice::editor {

struct PhysicsDebugLayers {
    bool physics = true;
    bool regions = true;
    bool contacts = false;
    bool velocities = true;
    bool characters = true;
    bool constraints = false;
    bool handoffs = false;
};

class PhysicsDebugUi {
public:
    PhysicsDebugUi();
    ~PhysicsDebugUi();

    PhysicsDebugUi(const PhysicsDebugUi&) = delete;
    PhysicsDebugUi& operator=(const PhysicsDebugUi&) = delete;

    void draw(const PreviewSnapshot& preview, bool authored_world_dirty);
    void draw_inspector_live(const PreviewSnapshot& preview, const SelectionModel& selection);

    [[nodiscard]] PhysicsDebugLayers layers() const noexcept;
    [[nodiscard]] bool consume_start_request() noexcept;
    [[nodiscard]] bool consume_reset_request() noexcept;
    [[nodiscard]] bool consume_stop_request() noexcept;
    [[nodiscard]] bool consume_step_request() noexcept;
    [[nodiscard]] bool consume_play_pause_request() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::editor
