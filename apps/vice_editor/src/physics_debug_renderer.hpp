#pragma once

#include <vice/editor/physics_debug_ui.hpp>
#include <vice/editor/preview_runtime.hpp>
#include <vice/editor/state.hpp>
#include <vice/editor/ui.hpp>
#include <vice/platform/event.hpp>
#include <vice/platform/window.hpp>
#include <vice/render/rhi.hpp>

#include <memory>

namespace vice::editor::detail {

class PhysicsDebugRenderer {
public:
    PhysicsDebugRenderer();
    ~PhysicsDebugRenderer();

    PhysicsDebugRenderer(const PhysicsDebugRenderer&) = delete;
    PhysicsDebugRenderer& operator=(const PhysicsDebugRenderer&) = delete;

    [[nodiscard]] bool initialize(render::Device& device, render::TextureFormat color_format);
    void handle_event(
        const platform::Event& event,
        const SceneViewport& viewport,
        const PreviewSnapshot& preview,
        SelectionModel& selection
    );
    void update(float delta_seconds, const SceneViewport& viewport);

    [[nodiscard]] bool render(
        const PreviewSnapshot& preview,
        const PhysicsDebugLayers& layers,
        const SelectionModel& selection,
        render::CommandList& commands,
        const SceneViewport& viewport,
        platform::WindowMetrics metrics
    );

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::editor::detail
