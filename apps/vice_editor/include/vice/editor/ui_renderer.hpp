#pragma once

#include <vice/platform/types.hpp>
#include <vice/render/rhi.hpp>

#include <memory>

namespace vice::editor {

class UiRhiRenderer {
public:
    UiRhiRenderer();
    ~UiRhiRenderer();

    UiRhiRenderer(const UiRhiRenderer&) = delete;
    UiRhiRenderer& operator=(const UiRhiRenderer&) = delete;
    UiRhiRenderer(UiRhiRenderer&&) = delete;
    UiRhiRenderer& operator=(UiRhiRenderer&&) = delete;

    [[nodiscard]] bool initialize(render::Device& device, render::TextureFormat color_format);
    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] bool render(
        render::Device& device,
        render::CommandList& commands,
        platform::WindowMetrics metrics
    );

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::editor
