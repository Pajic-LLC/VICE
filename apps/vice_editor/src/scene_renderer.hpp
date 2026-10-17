#pragma once

#include <vice/editor/state.hpp>
#include <vice/editor/ui.hpp>
#include <vice/platform/event.hpp>
#include <vice/platform/types.hpp>
#include <vice/render/rhi.hpp>
#include <vice/world/world.hpp>

#include <filesystem>
#include <memory>

namespace vice::editor::detail {

class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    [[nodiscard]] bool initialize(
        render::Device& device,
        render::TextureFormat color_format,
        const std::filesystem::path& authored_mesh_path
    );

    void handle_event(const platform::Event& event, const SceneViewport& viewport);
    void update(float delta_seconds, const SceneViewport& viewport);

    [[nodiscard]] bool render(
        const world::World& world,
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
