#pragma once

#include <vice/editor/asset_index.hpp>
#include <vice/world/resource.hpp>

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
    [[nodiscard]] std::optional<world::ResourceId> consume_add_mesh_request() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::editor
