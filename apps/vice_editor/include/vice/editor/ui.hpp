#pragma once

#include <vice/editor/state.hpp>
#include <vice/platform/event.hpp>
#include <vice/platform/types.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace vice::editor {

struct UiStatus {
    std::string world_name = "No World";
    std::string render_backend = "Renderer unavailable";
    bool world_loaded = false;
};

struct SceneViewport {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float framebuffer_scale = 1.0F;
    bool hovered = false;
    bool focused = false;

    [[nodiscard]] bool drawable() const noexcept {
        return width > 1.0F && height > 1.0F;
    }
};

class EditorUi {
public:
    explicit EditorUi(std::filesystem::path layout_path = {});
    ~EditorUi();

    EditorUi(const EditorUi&) = delete;
    EditorUi& operator=(const EditorUi&) = delete;
    EditorUi(EditorUi&&) = delete;
    EditorUi& operator=(EditorUi&&) = delete;

    [[nodiscard]] bool valid() const noexcept;

    void handle_event(const platform::Event& event, bool allow_shortcuts = true);
    [[nodiscard]] bool begin_frame(platform::WindowMetrics metrics, float delta_seconds);
    void draw(
        world::World& world,
        SelectionModel& selection,
        TransformHistory& history,
        ConsoleHistory& console,
        const UiStatus& status
    );
    void end_frame();

    [[nodiscard]] SceneViewport scene_viewport() const noexcept;
    [[nodiscard]] TransformTool transform_tool() const noexcept;

    void request_reset_layout() noexcept;
    [[nodiscard]] bool consume_quit_request() noexcept;
    [[nodiscard]] bool consume_save_request() noexcept;
    [[nodiscard]] bool consume_undo_request() noexcept;
    [[nodiscard]] bool consume_redo_request() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::editor
