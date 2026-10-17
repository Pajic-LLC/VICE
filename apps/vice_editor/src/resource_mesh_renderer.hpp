#pragma once

#include <vice/editor/asset_index.hpp>
#include <vice/editor/state.hpp>
#include <vice/editor/ui.hpp>
#include <vice/platform/event.hpp>
#include <vice/platform/window.hpp>
#include <vice/render/rhi.hpp>
#include <vice/world/spatial.hpp>
#include <vice/world/world.hpp>

#include <memory>
#include <optional>

namespace vice::editor::detail {

class ResourceMeshRenderer {
public:
    ResourceMeshRenderer();
    ~ResourceMeshRenderer();

    ResourceMeshRenderer(const ResourceMeshRenderer&) = delete;
    ResourceMeshRenderer& operator=(const ResourceMeshRenderer&) = delete;
    ResourceMeshRenderer(ResourceMeshRenderer&&) noexcept;
    ResourceMeshRenderer& operator=(ResourceMeshRenderer&&) noexcept;

    [[nodiscard]] bool initialize(render::Device& device, render::TextureFormat color_format);
    void handle_event(
        const platform::Event& event,
        const SceneViewport& viewport,
        const world::World& world,
        SelectionModel& selection,
        const AssetIndex& assets,
        std::optional<world::PersistentId> selection_before_event
    );
    void update(float delta_seconds, const SceneViewport& viewport);

    [[nodiscard]] world::GlobalPosition suggested_spawn_position(const world::World& world) const noexcept;

    [[nodiscard]] bool render(
        const world::World& world,
        const SelectionModel& selection,
        const AssetIndex& assets,
        render::CommandList& commands,
        const SceneViewport& viewport,
        platform::WindowMetrics metrics
    );

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::editor::detail
