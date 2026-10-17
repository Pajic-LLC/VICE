#pragma once

#include <vice/editor/asset_index.hpp>
#include <vice/editor/state.hpp>
#include <vice/world/resource.hpp>
#include <vice/world/world.hpp>

#include <memory>
#include <optional>

namespace vice::editor {

class ResourceBrowserUi {
public:
    ResourceBrowserUi();
    ~ResourceBrowserUi();

    ResourceBrowserUi(const ResourceBrowserUi&) = delete;
    ResourceBrowserUi& operator=(const ResourceBrowserUi&) = delete;
    ResourceBrowserUi(ResourceBrowserUi&&) noexcept;
    ResourceBrowserUi& operator=(ResourceBrowserUi&&) noexcept;

    void draw(AssetIndex& assets);
    void draw_inspector_resources(
        const world::World& world,
        const SelectionModel& selection,
        const AssetIndex& assets
    );
    [[nodiscard]] std::optional<world::ResourceId> consume_add_mesh_request() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::editor
