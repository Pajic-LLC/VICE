#include <vice/editor/ui.hpp>

#include "ui_theme.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace vice::editor {
namespace {

constexpr const char* hierarchy_window_name = "Hierarchy###VICE.Hierarchy";
constexpr const char* scene_window_name = "Scene###VICE.Scene";
constexpr const char* inspector_window_name = "Inspector###VICE.Inspector";
constexpr const char* console_window_name = "Console###VICE.Console";
constexpr const char* host_window_name = "###VICE.EditorHost";
constexpr const char* dockspace_name = "VICE.EditorDockspace";

[[nodiscard]] ImGuiKey imgui_key(const platform::Key key) noexcept {
    using platform::Key;
    switch (key) {
    case Key::escape: return ImGuiKey_Escape;
    case Key::enter: return ImGuiKey_Enter;
    case Key::tab: return ImGuiKey_Tab;
    case Key::backspace: return ImGuiKey_Backspace;
    case Key::space: return ImGuiKey_Space;
    case Key::left: return ImGuiKey_LeftArrow;
    case Key::right: return ImGuiKey_RightArrow;
    case Key::up: return ImGuiKey_UpArrow;
    case Key::down: return ImGuiKey_DownArrow;
    case Key::home: return ImGuiKey_Home;
    case Key::end: return ImGuiKey_End;
    case Key::page_up: return ImGuiKey_PageUp;
    case Key::page_down: return ImGuiKey_PageDown;
    case Key::insert: return ImGuiKey_Insert;
    case Key::delete_key: return ImGuiKey_Delete;
    case Key::a: return ImGuiKey_A;
    case Key::b: return ImGuiKey_B;
    case Key::c: return ImGuiKey_C;
    case Key::d: return ImGuiKey_D;
    case Key::e: return ImGuiKey_E;
    case Key::f: return ImGuiKey_F;
    case Key::g: return ImGuiKey_G;
    case Key::h: return ImGuiKey_H;
    case Key::i: return ImGuiKey_I;
    case Key::j: return ImGuiKey_J;
    case Key::k: return ImGuiKey_K;
    case Key::l: return ImGuiKey_L;
    case Key::m: return ImGuiKey_M;
    case Key::n: return ImGuiKey_N;
    case Key::o: return ImGuiKey_O;
    case Key::p: return ImGuiKey_P;
    case Key::q: return ImGuiKey_Q;
    case Key::r: return ImGuiKey_R;
    case Key::s: return ImGuiKey_S;
    case Key::t: return ImGuiKey_T;
    case Key::u: return ImGuiKey_U;
    case Key::v: return ImGuiKey_V;
    case Key::w: return ImGuiKey_W;
    case Key::x: return ImGuiKey_X;
    case Key::y: return ImGuiKey_Y;
    case Key::z: return ImGuiKey_Z;
    case Key::digit_0: return ImGuiKey_0;
    case Key::digit_1: return ImGuiKey_1;
    case Key::digit_2: return ImGuiKey_2;
    case Key::digit_3: return ImGuiKey_3;
    case Key::digit_4: return ImGuiKey_4;
    case Key::digit_5: return ImGuiKey_5;
    case Key::digit_6: return ImGuiKey_6;
    case Key::digit_7: return ImGuiKey_7;
    case Key::digit_8: return ImGuiKey_8;
    case Key::digit_9: return ImGuiKey_9;
    case Key::f1: return ImGuiKey_F1;
    case Key::f2: return ImGuiKey_F2;
    case Key::f3: return ImGuiKey_F3;
    case Key::f4: return ImGuiKey_F4;
    case Key::f5: return ImGuiKey_F5;
    case Key::f6: return ImGuiKey_F6;
    case Key::f7: return ImGuiKey_F7;
    case Key::f8: return ImGuiKey_F8;
    case Key::f9: return ImGuiKey_F9;
    case Key::f10: return ImGuiKey_F10;
    case Key::f11: return ImGuiKey_F11;
    case Key::f12: return ImGuiKey_F12;
    case Key::unknown: return ImGuiKey_None;
    }
    return ImGuiKey_None;
}

[[nodiscard]] int imgui_mouse_button(const platform::MouseButton button) noexcept {
    using platform::MouseButton;
    switch (button) {
    case MouseButton::left: return ImGuiMouseButton_Left;
    case MouseButton::right: return ImGuiMouseButton_Right;
    case MouseButton::middle: return ImGuiMouseButton_Middle;
    case MouseButton::button_4: return 3;
    case MouseButton::button_5: return 4;
    case MouseButton::other: return -1;
    }
    return -1;
}

void push_modifiers(ImGuiIO& io, const platform::ModifierMask modifiers) {
    io.AddKeyEvent(ImGuiMod_Ctrl, platform::has_modifier(modifiers, platform::Modifier::control));
    io.AddKeyEvent(ImGuiMod_Shift, platform::has_modifier(modifiers, platform::Modifier::shift));
    io.AddKeyEvent(ImGuiMod_Alt, platform::has_modifier(modifiers, platform::Modifier::alt));
    io.AddKeyEvent(ImGuiMod_Super, platform::has_modifier(modifiers, platform::Modifier::super));
}

[[nodiscard]] std::string short_id(const world::PersistentId id) {
    const auto full = id.to_string();
    if (full.size() <= 8U) {
        return full;
    }
    return full.substr(0, 8);
}

[[nodiscard]] bool contains_case_insensitive(
    const std::string_view haystack,
    const std::string_view needle
) {
    if (needle.empty()) {
        return true;
    }
    const auto found = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](const char left, const char right) {
            const auto lower = [](const char value) {
                if (value >= 'A' && value <= 'Z') {
                    return static_cast<char>(value - 'A' + 'a');
                }
                return value;
            };
            return lower(left) == lower(right);
        }
    );
    return found != haystack.end();
}

void property_row(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value.c_str());
}

[[nodiscard]] std::string format_position(const world::GlobalPosition position) {
    char text[128]{};
    std::snprintf(
        text,
        sizeof(text),
        "%.3f, %.3f, %.3f",
        position.x,
        position.y,
        position.z
    );
    return text;
}

[[nodiscard]] std::string format_cell(const world::StreamingCellId cell) {
    std::ostringstream stream;
    stream << cell.x << ", " << cell.y << ", " << cell.z;
    return stream.str();
}

[[nodiscard]] ImVec4 level_color(const core::LogLevel level) noexcept {
    switch (level) {
    case core::LogLevel::trace: return {0.48F, 0.52F, 0.58F, 1.00F};
    case core::LogLevel::debug: return {0.56F, 0.62F, 0.69F, 1.00F};
    case core::LogLevel::info: return {0.70F, 0.79F, 0.89F, 1.00F};
    case core::LogLevel::warning: return {0.96F, 0.72F, 0.30F, 1.00F};
    case core::LogLevel::error: return {0.96F, 0.39F, 0.37F, 1.00F};
    case core::LogLevel::fatal: return {1.00F, 0.28F, 0.45F, 1.00F};
    }
    return {0.80F, 0.80F, 0.80F, 1.00F};
}

void draw_filter_button(
    const char* label,
    const core::LogLevel level,
    core::LogLevel& current
) {
    const bool selected = current == level;
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.15F, 0.36F, 0.53F, 1.00F});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.18F, 0.43F, 0.63F, 1.00F});
    }
    if (ImGui::SmallButton(label)) {
        current = level;
    }
    if (selected) {
        ImGui::PopStyleColor(2);
    }
}

} // namespace

struct EditorUi::State {
    ImGuiContext* context = nullptr;
    std::string ini_path;
    SceneViewport scene_viewport{};
    platform::WindowMetrics metrics{};
    float style_scale = 0.0F;
    bool frame_active = false;
    bool reset_layout = false;
    bool quit_requested = false;
    bool show_hierarchy = true;
    bool show_scene = true;
    bool show_inspector = true;
    bool show_console = true;
    std::array<char, 128> hierarchy_filter{};
    core::LogLevel console_minimum = core::LogLevel::trace;
    std::uint64_t last_console_sequence = 0;
};

EditorUi::EditorUi(std::filesystem::path layout_path)
    : state_{std::make_unique<State>()} {
    IMGUI_CHECKVERSION();
    state_->context = ImGui::CreateContext();
    if (state_->context == nullptr) {
        return;
    }

    ImGui::SetCurrentContext(state_->context);
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigDpiScaleFonts = true;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.BackendPlatformName = "VICE Platform";

    if (!layout_path.empty()) {
        state_->ini_path = layout_path.string();
        io.IniFilename = state_->ini_path.c_str();
    } else {
        io.IniFilename = nullptr;
    }

    detail::apply_editor_theme(1.0F);
    state_->style_scale = 1.0F;
}

EditorUi::~EditorUi() {
    if (state_ == nullptr || state_->context == nullptr) {
        return;
    }
    ImGui::SetCurrentContext(state_->context);
    ImGui::DestroyContext(state_->context);
    state_->context = nullptr;
}

bool EditorUi::valid() const noexcept {
    return state_ != nullptr && state_->context != nullptr;
}

void EditorUi::handle_event(const platform::Event& event) {
    if (!valid()) {
        return;
    }
    ImGui::SetCurrentContext(state_->context);
    auto& io = ImGui::GetIO();

    std::visit(
        [&io](const auto& payload) {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, platform::WindowFocusChanged>) {
                io.AddFocusEvent(payload.focused);
            } else if constexpr (std::is_same_v<Payload, platform::KeyEvent>) {
                push_modifiers(io, payload.modifiers);
                const ImGuiKey key = imgui_key(payload.key);
                if (key != ImGuiKey_None) {
                    io.AddKeyEvent(key, payload.pressed);
                    io.SetKeyEventNativeData(
                        key,
                        static_cast<int>(payload.native_code),
                        static_cast<int>(payload.native_code)
                    );
                }
            } else if constexpr (std::is_same_v<Payload, platform::TextInputEvent>) {
                if (payload.codepoint != U'\0') {
                    io.AddInputCharacter(static_cast<unsigned int>(payload.codepoint));
                }
            } else if constexpr (std::is_same_v<Payload, platform::MouseMoveEvent>) {
                io.AddMousePosEvent(static_cast<float>(payload.x), static_cast<float>(payload.y));
            } else if constexpr (std::is_same_v<Payload, platform::MouseButtonEvent>) {
                io.AddMousePosEvent(static_cast<float>(payload.x), static_cast<float>(payload.y));
                const int button = imgui_mouse_button(payload.button);
                if (button >= 0 && button < ImGuiMouseButton_COUNT) {
                    io.AddMouseButtonEvent(button, payload.pressed);
                }
            } else if constexpr (std::is_same_v<Payload, platform::MouseWheelEvent>) {
                const float divisor = payload.unit == platform::ScrollUnit::pixels ? 50.0F : 1.0F;
                io.AddMouseWheelEvent(
                    static_cast<float>(payload.delta_x) / divisor,
                    static_cast<float>(payload.delta_y) / divisor
                );
            }
        },
        event.payload
    );
}

bool EditorUi::begin_frame(const platform::WindowMetrics metrics, const float delta_seconds) {
    if (!valid() || state_->frame_active || metrics.width == 0U || metrics.height == 0U) {
        return false;
    }

    ImGui::SetCurrentContext(state_->context);
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2{static_cast<float>(metrics.width), static_cast<float>(metrics.height)};

    const float scale_x = metrics.width > 0U && metrics.drawable_width > 0U
        ? static_cast<float>(metrics.drawable_width) / static_cast<float>(metrics.width)
        : metrics.scale_factor;
    const float scale_y = metrics.height > 0U && metrics.drawable_height > 0U
        ? static_cast<float>(metrics.drawable_height) / static_cast<float>(metrics.height)
        : metrics.scale_factor;
    io.DisplayFramebufferScale = ImVec2{std::max(0.5F, scale_x), std::max(0.5F, scale_y)};
    io.DeltaTime = std::clamp(delta_seconds, 1.0F / 1000.0F, 0.25F);

    const float style_scale = std::max(0.75F, metrics.scale_factor);
    if (std::abs(style_scale - state_->style_scale) > 0.01F) {
        detail::apply_editor_theme(style_scale);
        state_->style_scale = style_scale;
    }

    state_->metrics = metrics;
    state_->scene_viewport = {};
    ImGui::NewFrame();
    state_->frame_active = true;
    return true;
}

void EditorUi::draw(
    const world::World& world,
    SelectionModel& selection,
    ConsoleHistory& console,
    const UiStatus& status
) {
    if (!valid() || !state_->frame_active) {
        return;
    }
    ImGui::SetCurrentContext(state_->context);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    ImGui::Begin(host_window_name, nullptr, host_flags);
    ImGui::PopStyleVar(3);

    if (ImGui::BeginMenuBar()) {
        ImGui::TextUnformatted("VICE");
        ImGui::SameLine();
        ImGui::TextDisabled("Editor");
        ImGui::Separator();

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Quit")) {
                state_->quit_requested = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Hierarchy", nullptr, &state_->show_hierarchy);
            ImGui::MenuItem("Scene", nullptr, &state_->show_scene);
            ImGui::MenuItem("Inspector", nullptr, &state_->show_inspector);
            ImGui::MenuItem("Console", nullptr, &state_->show_console);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) {
                state_->reset_layout = true;
                state_->show_hierarchy = true;
                state_->show_scene = true;
                state_->show_inspector = true;
                state_->show_console = true;
            }
            ImGui::EndMenu();
        }

        const std::string world_label = status.world_loaded ? status.world_name : "No World loaded";
        const float label_width = ImGui::CalcTextSize(world_label.c_str()).x;
        const float right_edge = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        if (right_edge - label_width > ImGui::GetCursorPosX() + 20.0F) {
            ImGui::SetCursorPosX(right_edge - label_width);
        }
        ImGui::TextDisabled("%s", world_label.c_str());
        ImGui::EndMenuBar();
    }

    const ImGuiID dockspace_id = ImGui::GetID(dockspace_name);
    const float status_height = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    ImVec2 dock_size = ImGui::GetContentRegionAvail();
    dock_size.y = std::max(1.0F, dock_size.y - status_height);

    if (state_->reset_layout || ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, dock_size);

        ImGuiID center_id = dockspace_id;
        ImGuiID left_id = 0;
        ImGuiID right_id = 0;
        ImGuiID bottom_id = 0;
        ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Left, 0.18F, &left_id, &center_id);
        ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Right, 0.24F, &right_id, &center_id);
        ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Down, 0.22F, &bottom_id, &center_id);

        ImGui::DockBuilderDockWindow(hierarchy_window_name, left_id);
        ImGui::DockBuilderDockWindow(scene_window_name, center_id);
        ImGui::DockBuilderDockWindow(inspector_window_name, right_id);
        ImGui::DockBuilderDockWindow(console_window_name, bottom_id);
        ImGui::DockBuilderFinish(dockspace_id);
        state_->reset_layout = false;
    }

    ImGui::DockSpace(dockspace_id, dock_size, ImGuiDockNodeFlags_None);

    ImGui::Separator();
    const ImU32 status_color = ImGui::GetColorU32(
        status.world_loaded
            ? ImVec4{0.27F, 0.78F, 0.52F, 1.00F}
            : ImVec4{0.48F, 0.52F, 0.58F, 1.00F}
    );
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2{cursor.x + 6.0F, cursor.y + ImGui::GetTextLineHeight() * 0.5F},
        3.0F,
        status_color
    );
    ImGui::Dummy(ImVec2{15.0F, 1.0F});
    ImGui::SameLine();
    ImGui::TextDisabled("%s", status.world_loaded ? status.world_name.c_str() : "No World loaded");
    ImGui::SameLine();
    ImGui::TextDisabled("  |  %s  |  %zu entities", status.render_backend.c_str(), world.entity_count());

    ImGui::End();

    if (state_->show_hierarchy && ImGui::Begin(hierarchy_window_name, &state_->show_hierarchy)) {
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint(
            "##HierarchyFilter",
            "Search entities",
            state_->hierarchy_filter.data(),
            state_->hierarchy_filter.size()
        );

        const auto entries = build_hierarchy(world);
        ImGui::TextDisabled("%zu %s", entries.size(), entries.size() == 1U ? "entity" : "entities");
        ImGui::Separator();

        std::size_t visible_count = 0;
        for (const auto& entry : entries) {
            const std::string id = entry.persistent_id.to_string();
            if (!contains_case_insensitive(id, state_->hierarchy_filter.data())) {
                continue;
            }
            ++visible_count;

            const std::string short_name = short_id(entry.persistent_id);
            std::string label = "Entity  " + short_name;
            if (entry.spatial_state.has_value()) {
                label += "   R" + std::to_string(entry.spatial_state->region.value);
            }
            label += "###" + id;

            const bool selected = selection.selected() == std::optional{entry.persistent_id};
            if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                selection.select(entry.persistent_id);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("%s", id.c_str());
            }
        }

        if (entries.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("No entities in this World.");
        } else if (visible_count == 0U) {
            ImGui::Spacing();
            ImGui::TextDisabled("No entities match your search.");
        }
    }
    if (state_->show_hierarchy) {
        ImGui::End();
    }

    if (state_->show_scene && ImGui::Begin(
        scene_window_name,
        &state_->show_scene,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    )) {
        ImGui::TextDisabled("%s", status.render_backend.c_str());
        ImGui::SameLine();
        const float controls_width = ImGui::CalcTextSize("Controls").x + ImGui::GetStyle().FramePadding.x * 2.0F;
        const float available_x = ImGui::GetContentRegionAvail().x;
        if (available_x > controls_width) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available_x - controls_width);
        }
        if (ImGui::SmallButton("Controls")) {
            ImGui::OpenPopup("SceneControls");
        }
        if (ImGui::BeginPopup("SceneControls")) {
            ImGui::TextUnformatted("Orbit   Right drag");
            ImGui::TextUnformatted("Pan     Middle drag");
            ImGui::TextUnformatted("Zoom    Scroll");
            ImGui::EndPopup();
        }
        ImGui::Separator();

        const ImVec2 scene_pos = ImGui::GetCursorScreenPos();
        ImVec2 scene_size = ImGui::GetContentRegionAvail();
        scene_size.x = std::max(1.0F, scene_size.x);
        scene_size.y = std::max(1.0F, scene_size.y);

        auto* draw_list = ImGui::GetWindowDrawList();
        const ImVec2 scene_max{scene_pos.x + scene_size.x, scene_pos.y + scene_size.y};
        draw_list->AddRectFilled(scene_pos, scene_max, IM_COL32(11, 14, 19, 255), 5.0F);

        constexpr float grid_step = 32.0F;
        const ImU32 grid_color = IM_COL32(61, 70, 84, 42);
        for (float x = scene_pos.x; x < scene_max.x; x += grid_step) {
            draw_list->AddLine(ImVec2{x, scene_pos.y}, ImVec2{x, scene_max.y}, grid_color);
        }
        for (float y = scene_pos.y; y < scene_max.y; y += grid_step) {
            draw_list->AddLine(ImVec2{scene_pos.x, y}, ImVec2{scene_max.x, y}, grid_color);
        }
        draw_list->AddRect(scene_pos, scene_max, IM_COL32(51, 61, 74, 255), 5.0F);

        ImGui::InvisibleButton(
            "##SceneViewport",
            scene_size,
            ImGuiButtonFlags_MouseButtonLeft |
                ImGuiButtonFlags_MouseButtonRight |
                ImGuiButtonFlags_MouseButtonMiddle
        );

        state_->scene_viewport = SceneViewport{
            .x = scene_pos.x,
            .y = scene_pos.y,
            .width = scene_size.x,
            .height = scene_size.y,
            .framebuffer_scale = state_->metrics.scale_factor,
            .hovered = ImGui::IsItemHovered(),
            .focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows),
        };

        if (!status.world_loaded) {
            const char* message = "Open a World to start.";
            const ImVec2 text_size = ImGui::CalcTextSize(message);
            draw_list->AddText(
                ImVec2{
                    scene_pos.x + (scene_size.x - text_size.x) * 0.5F,
                    scene_pos.y + (scene_size.y - text_size.y) * 0.5F,
                },
                IM_COL32(150, 160, 174, 255),
                message
            );
        }
    }
    if (state_->show_scene) {
        ImGui::End();
    }

    if (state_->show_inspector && ImGui::Begin(inspector_window_name, &state_->show_inspector)) {
        const auto selected_id = selection.selected();
        if (!selected_id.has_value()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Select an entity to inspect it.");
        } else if (const auto inspector = project_inspector(world, *selected_id)) {
            const std::string short_name = short_id(inspector->persistent_id);
            ImGui::Text("Entity  %s", short_name.c_str());
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Position", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable(
                    "##InspectorPosition",
                    2,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings
                )) {
                    if (inspector->spatial_state.has_value()) {
                        property_row("World", format_position(inspector->spatial_state->global_position));
                        property_row("Region", std::to_string(inspector->spatial_state->region.value));
                    } else {
                        property_row("World", "Not spatial");
                    }
                    ImGui::EndTable();
                }
            }

            if (ImGui::CollapsingHeader("Runtime", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable(
                    "##InspectorRuntime",
                    2,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings
                )) {
                    property_row(
                        "Simulation island",
                        inspector->simulation_island.has_value()
                            ? std::to_string(inspector->simulation_island->value)
                            : "None"
                    );
                    property_row(
                        "Streaming cell",
                        inspector->streaming_cell.has_value()
                            ? format_cell(*inspector->streaming_cell)
                            : "None"
                    );
                    property_row("Relations", std::to_string(inspector->relations.size()));
                    ImGui::EndTable();
                }
            }

            if (ImGui::CollapsingHeader("Identity")) {
                if (ImGui::BeginTable(
                    "##InspectorIdentity",
                    2,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings
                )) {
                    property_row("Persistent ID", inspector->persistent_id.to_string());
                    if (inspector->entity.has_value()) {
                        property_row("Live index", std::to_string(inspector->entity->index));
                        property_row("Generation", std::to_string(inspector->entity->generation));
                    }
                    ImGui::EndTable();
                }
            }
        } else {
            ImGui::TextDisabled("The selected entity is not currently loaded.");
        }
    }
    if (state_->show_inspector) {
        ImGui::End();
    }

    if (state_->show_console && ImGui::Begin(console_window_name, &state_->show_console)) {
        draw_filter_button("All", core::LogLevel::trace, state_->console_minimum);
        ImGui::SameLine();
        draw_filter_button("Warnings", core::LogLevel::warning, state_->console_minimum);
        ImGui::SameLine();
        draw_filter_button("Errors", core::LogLevel::error, state_->console_minimum);

        const float clear_width = ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2.0F;
        const float console_available = ImGui::GetContentRegionAvail().x;
        if (console_available > clear_width) {
            ImGui::SameLine(ImGui::GetCursorPosX() + console_available - clear_width);
        }
        if (ImGui::SmallButton("Clear")) {
            console.clear();
            state_->last_console_sequence = 0;
        }
        ImGui::Separator();

        const auto entries = console.entries(state_->console_minimum);
        const bool was_at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0F;
        const std::uint64_t newest_sequence = entries.empty() ? 0 : entries.back().sequence;
        const bool new_entries = newest_sequence > state_->last_console_sequence;

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(entries.size()));
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                const auto& entry = entries[static_cast<std::size_t>(index)];
                ImGui::PushID(static_cast<int>(entry.sequence & 0x7fffffffU));
                ImGui::TextColored(level_color(entry.level), "%s", core::log_level_name(entry.level).data());
                ImGui::SameLine(58.0F * state_->style_scale);
                ImGui::TextUnformatted(entry.message.c_str());
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !entry.source_file.empty()) {
                    ImGui::SetTooltip("%s:%u", entry.source_file.c_str(), entry.source_line);
                }
                ImGui::PopID();
            }
        }
        if (new_entries && was_at_bottom) {
            ImGui::SetScrollHereY(1.0F);
        }
        state_->last_console_sequence = newest_sequence;
    }
    if (state_->show_console) {
        ImGui::End();
    }
}

void EditorUi::end_frame() {
    if (!valid() || !state_->frame_active) {
        return;
    }
    ImGui::SetCurrentContext(state_->context);
    ImGui::Render();
    state_->frame_active = false;
}

SceneViewport EditorUi::scene_viewport() const noexcept {
    if (!valid()) {
        return {};
    }
    return state_->scene_viewport;
}

void EditorUi::request_reset_layout() noexcept {
    if (state_ != nullptr) {
        state_->reset_layout = true;
    }
}

bool EditorUi::consume_quit_request() noexcept {
    if (state_ == nullptr) {
        return false;
    }
    const bool requested = state_->quit_requested;
    state_->quit_requested = false;
    return requested;
}

} // namespace vice::editor
