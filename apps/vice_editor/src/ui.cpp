#include <vice/editor/ui.hpp>

#include "ui_theme.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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
constexpr float pi = 3.14159265358979323846F;

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
    const std::string full = id.to_string();
    return full.size() > 8U ? full.substr(0, 8) : full;
}

[[nodiscard]] char ascii_lower(const char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

[[nodiscard]] bool contains_case_insensitive(
    const std::string_view haystack,
    const std::string_view needle
) {
    if (needle.empty()) {
        return true;
    }
    return std::search(
        haystack.begin(),
        haystack.end(),
        needle.begin(),
        needle.end(),
        [](const char left, const char right) {
            return ascii_lower(left) == ascii_lower(right);
        }
    ) != haystack.end();
}

void property_row(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value.c_str());
}

[[nodiscard]] std::string format_cell(const world::StreamingCellId cell) {
    std::ostringstream stream;
    stream << cell.x << ", " << cell.y << ", " << cell.z;
    return stream.str();
}

[[nodiscard]] world::AuthoredTransform authored_or_identity(
    const std::optional<world::AuthoredTransform>& transform
) noexcept {
    return transform.value_or(world::AuthoredTransform{});
}

[[nodiscard]] std::array<float, 3> quaternion_to_euler_degrees(
    const world::AuthoredRotation rotation
) noexcept {
    const float sin_roll = 2.0F * (rotation.w * rotation.x + rotation.y * rotation.z);
    const float cos_roll = 1.0F - 2.0F * (rotation.x * rotation.x + rotation.y * rotation.y);
    const float roll = std::atan2(sin_roll, cos_roll);

    const float sin_pitch = 2.0F * (rotation.w * rotation.y - rotation.z * rotation.x);
    const float pitch = std::abs(sin_pitch) >= 1.0F
        ? std::copysign(pi * 0.5F, sin_pitch)
        : std::asin(sin_pitch);

    const float sin_yaw = 2.0F * (rotation.w * rotation.z + rotation.x * rotation.y);
    const float cos_yaw = 1.0F - 2.0F * (rotation.y * rotation.y + rotation.z * rotation.z);
    const float yaw = std::atan2(sin_yaw, cos_yaw);

    constexpr float radians_to_degrees = 180.0F / pi;
    return {roll * radians_to_degrees, pitch * radians_to_degrees, yaw * radians_to_degrees};
}

[[nodiscard]] world::AuthoredRotation euler_degrees_to_quaternion(
    const std::array<float, 3>& degrees
) noexcept {
    constexpr float degrees_to_radians = pi / 180.0F;
    const float half_x = degrees[0] * degrees_to_radians * 0.5F;
    const float half_y = degrees[1] * degrees_to_radians * 0.5F;
    const float half_z = degrees[2] * degrees_to_radians * 0.5F;
    const float sx = std::sin(half_x);
    const float cx = std::cos(half_x);
    const float sy = std::sin(half_y);
    const float cy = std::cos(half_y);
    const float sz = std::sin(half_z);
    const float cz = std::cos(half_z);

    world::AuthoredRotation result{
        .x = sx * cy * cz - cx * sy * sz,
        .y = cx * sy * cz + sx * cy * sz,
        .z = cx * cy * sz - sx * sy * cz,
        .w = cx * cy * cz + sx * sy * sz,
    };
    const float length = std::sqrt(
        result.x * result.x + result.y * result.y +
        result.z * result.z + result.w * result.w
    );
    if (length > 0.0F) {
        result.x /= length;
        result.y /= length;
        result.z /= length;
        result.w /= length;
    }
    return result;
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

void filter_button(
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

[[nodiscard]] bool tool_button(
    const char* label,
    const TransformTool value,
    TransformTool& current
) {
    const bool selected = current == value;
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.16F, 0.43F, 0.65F, 1.00F});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.20F, 0.51F, 0.76F, 1.00F});
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.96F, 0.98F, 1.00F, 1.00F});
    }
    const bool clicked = ImGui::SmallButton(label);
    if (selected) {
        ImGui::PopStyleColor(3);
    }
    if (clicked) {
        current = value;
    }
    return clicked;
}

void build_default_dock_layout(const ImGuiID dockspace_id, const ImVec2 dock_size) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, dock_size);

    ImGuiID center = dockspace_id;
    ImGuiID left = 0;
    ImGuiID right = 0;
    ImGuiID bottom = 0;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18F, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24F, &right, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22F, &bottom, &center);

    ImGui::DockBuilderDockWindow(hierarchy_window_name, left);
    ImGui::DockBuilderDockWindow(scene_window_name, center);
    ImGui::DockBuilderDockWindow(inspector_window_name, right);
    ImGui::DockBuilderDockWindow(console_window_name, bottom);
    ImGui::DockBuilderFinish(dockspace_id);
}

} // namespace

struct EditorUi::State {
    ImGuiContext* context = nullptr;
    std::string ini_path;
    SceneViewport scene_viewport{};
    platform::WindowMetrics metrics{};
    float style_scale = 1.0F;
    bool frame_active = false;
    bool reset_layout = false;
    bool quit_requested = false;
    bool save_requested = false;
    bool undo_requested = false;
    bool redo_requested = false;
    bool show_hierarchy = true;
    bool show_scene = true;
    bool show_inspector = true;
    bool show_console = true;
    TransformTool transform_tool = TransformTool::move;
    std::array<char, 128> hierarchy_filter{};
    core::LogLevel console_minimum = core::LogLevel::trace;
    std::uint64_t last_console_sequence = 0;
    std::optional<world::PersistentId> inspector_edit_id;
    std::optional<EntityTransformState> inspector_edit_before;
    std::string inspector_edit_label;
    std::optional<world::PersistentId> inspector_camera_edit_id;
    std::optional<world::Camera> inspector_camera_edit_before;
    std::string inspector_camera_edit_label;
    std::optional<world::PersistentId> inspector_light_edit_id;
    std::optional<world::Light> inspector_light_edit_before;
    std::string inspector_light_edit_label;
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
}

EditorUi::~EditorUi() {
    if (!valid()) {
        return;
    }
    ImGui::SetCurrentContext(state_->context);
    ImGui::DestroyContext(state_->context);
    state_->context = nullptr;
}

bool EditorUi::valid() const noexcept {
    return state_ != nullptr && state_->context != nullptr;
}

void EditorUi::handle_event(const platform::Event& event, const bool allow_shortcuts) {
    if (!valid()) {
        return;
    }
    ImGui::SetCurrentContext(state_->context);
    auto& io = ImGui::GetIO();

    std::visit(
        [&](const auto& payload) {
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

                if (allow_shortcuts && payload.pressed && !payload.repeat) {
                    const bool command =
                        platform::has_modifier(payload.modifiers, platform::Modifier::control) ||
                        platform::has_modifier(payload.modifiers, platform::Modifier::super);
                    const bool shift = platform::has_modifier(payload.modifiers, platform::Modifier::shift);
                    if (command && payload.key == platform::Key::s) {
                        state_->save_requested = true;
                    } else if (command && payload.key == platform::Key::z && !io.WantTextInput) {
                        if (shift) state_->redo_requested = true;
                        else state_->undo_requested = true;
                    } else if (!command && !shift && !io.WantTextInput && state_->scene_viewport.focused) {
                        if (payload.key == platform::Key::w) state_->transform_tool = TransformTool::move;
                        if (payload.key == platform::Key::e) state_->transform_tool = TransformTool::rotate;
                        if (payload.key == platform::Key::r) state_->transform_tool = TransformTool::scale;
                    }
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
    io.DisplaySize = {static_cast<float>(metrics.width), static_cast<float>(metrics.height)};

    const float scale_x = metrics.drawable_width > 0U
        ? static_cast<float>(metrics.drawable_width) / static_cast<float>(metrics.width)
        : metrics.scale_factor;
    const float scale_y = metrics.drawable_height > 0U
        ? static_cast<float>(metrics.drawable_height) / static_cast<float>(metrics.height)
        : metrics.scale_factor;
    io.DisplayFramebufferScale = {std::max(0.5F, scale_x), std::max(0.5F, scale_y)};
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
    world::World& world,
    SelectionModel& selection,
    TransformHistory& history,
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
            if (ImGui::MenuItem("Save", "Cmd/Ctrl+S", false, status.world_loaded)) {
                state_->save_requested = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit")) {
                state_->quit_requested = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            std::string undo_text = "Undo";
            if (!history.undo_label().empty()) undo_text += " " + history.undo_label();
            std::string redo_text = "Redo";
            if (!history.redo_label().empty()) redo_text += " " + history.redo_label();
            if (ImGui::MenuItem(undo_text.c_str(), "Cmd/Ctrl+Z", false, history.can_undo())) {
                state_->undo_requested = true;
            }
            if (ImGui::MenuItem(redo_text.c_str(), "Cmd/Ctrl+Shift+Z", false, history.can_redo())) {
                state_->redo_requested = true;
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

        std::string world_label = status.world_loaded ? status.world_name : "No World loaded";
        if (history.dirty()) world_label += "  •";
        const float label_width = ImGui::CalcTextSize(world_label.c_str()).x;
        const float right_edge = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        if (right_edge - label_width > ImGui::GetCursorPosX() + 20.0F) {
            ImGui::SetCursorPosX(right_edge - label_width);
        }
        if (history.dirty()) {
            ImGui::TextColored(ImVec4{0.96F, 0.72F, 0.30F, 1.0F}, "%s", world_label.c_str());
        } else {
            ImGui::TextDisabled("%s", world_label.c_str());
        }
        ImGui::EndMenuBar();
    }

    const ImGuiID dockspace_id = ImGui::GetID(dockspace_name);
    const float status_height = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    ImVec2 dock_size = ImGui::GetContentRegionAvail();
    dock_size.y = std::max(1.0F, dock_size.y - status_height);

    if (state_->reset_layout || ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
        build_default_dock_layout(dockspace_id, dock_size);
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
        {cursor.x + 6.0F, cursor.y + ImGui::GetTextLineHeight() * 0.5F},
        3.0F,
        status_color
    );
    ImGui::Dummy({15.0F, 1.0F});
    ImGui::SameLine();
    ImGui::TextDisabled("%s", status.world_loaded ? status.world_name.c_str() : "No World loaded");
    ImGui::SameLine();
    ImGui::TextDisabled("  ·  %s  ·  %zu entities", status.render_backend.c_str(), world.entity_count());
    if (history.dirty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{0.96F, 0.72F, 0.30F, 1.0F}, "  ·  Unsaved");
    }
    ImGui::End();

    if (state_->show_hierarchy) {
        const bool visible = ImGui::Begin(hierarchy_window_name, nullptr);
        if (visible) {
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
                const std::string full_id = entry.persistent_id.to_string();
                if (!contains_case_insensitive(full_id, state_->hierarchy_filter.data())) {
                    continue;
                }
                ++visible_count;

                std::string label = "Entity  " + short_id(entry.persistent_id);
                if (entry.spatial_state.has_value()) {
                    label += "   R" + std::to_string(entry.spatial_state->region.value);
                }
                if (entry.camera.has_value()) {
                    label += entry.camera->primary ? "   Primary Camera" : "   Camera";
                }
                if (entry.light.has_value()) {
                    label += entry.light->type == world::LightType::directional
                        ? "   Directional Light"
                        : "   Point Light";
                }
                label += "###" + full_id;

                const bool selected = selection.selected() == std::optional{entry.persistent_id};
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    selection.select(entry.persistent_id);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s", full_id.c_str());
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
        ImGui::End();
    }

    if (state_->show_scene) {
        constexpr ImGuiWindowFlags scene_flags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBackground;
        const bool visible = ImGui::Begin(scene_window_name, nullptr, scene_flags);
        if (visible) {
            const ImVec2 toolbar_pos = ImGui::GetCursorScreenPos();
            const float toolbar_height = ImGui::GetFrameHeight() + 4.0F * state_->style_scale;
            const float toolbar_width = ImGui::GetContentRegionAvail().x;
            auto* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(
                toolbar_pos,
                {toolbar_pos.x + toolbar_width, toolbar_pos.y + toolbar_height},
                IM_COL32(16, 19, 25, 242),
                5.0F
            );
            draw_list->AddLine(
                {toolbar_pos.x, toolbar_pos.y + toolbar_height},
                {toolbar_pos.x + toolbar_width, toolbar_pos.y + toolbar_height},
                IM_COL32(48, 56, 69, 210)
            );

            ImGui::SetCursorScreenPos({
                toolbar_pos.x + 10.0F * state_->style_scale,
                toolbar_pos.y + 2.0F * state_->style_scale,
            });
            ImGui::TextDisabled("Perspective");
            ImGui::SameLine();
            ImGui::TextDisabled("·");
            ImGui::SameLine();
            ImGui::TextDisabled("Shaded");
            ImGui::SameLine(0.0F, 16.0F * state_->style_scale);
            tool_button("Move", TransformTool::move, state_->transform_tool);
            ImGui::SameLine(0.0F, 4.0F * state_->style_scale);
            tool_button("Rotate", TransformTool::rotate, state_->transform_tool);
            ImGui::SameLine(0.0F, 4.0F * state_->style_scale);
            tool_button("Scale", TransformTool::scale, state_->transform_tool);

            const float controls_width =
                ImGui::CalcTextSize("Controls").x + ImGui::GetStyle().FramePadding.x * 2.0F;
            ImGui::SetCursorScreenPos({
                toolbar_pos.x + toolbar_width - controls_width - 8.0F * state_->style_scale,
                toolbar_pos.y + 1.0F * state_->style_scale,
            });
            if (ImGui::SmallButton("Controls")) {
                ImGui::OpenPopup("SceneControls");
            }
            if (ImGui::BeginPopup("SceneControls")) {
                if (ImGui::BeginTable(
                    "##SceneControlsTable",
                    2,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings
                )) {
                    property_row("Select", "Left click");
                    property_row("Move tool", "W");
                    property_row("Rotate tool", "E");
                    property_row("Scale tool", "R");
                    property_row("Orbit", "Right drag");
                    property_row("Pan", "Middle drag");
                    property_row("Zoom", "Scroll");
                    property_row("Fly", "Shift + W A S D");
                    property_row("Elevate", "Shift + Q / E");
                    property_row("Frame", "F");
                    ImGui::EndTable();
                }
                ImGui::EndPopup();
            }

            ImGui::SetCursorScreenPos({toolbar_pos.x, toolbar_pos.y + toolbar_height + 5.0F * state_->style_scale});
            const ImVec2 scene_pos = ImGui::GetCursorScreenPos();
            ImVec2 scene_size = ImGui::GetContentRegionAvail();
            scene_size.x = std::max(1.0F, scene_size.x);
            scene_size.y = std::max(1.0F, scene_size.y);
            const ImVec2 scene_max{scene_pos.x + scene_size.x, scene_pos.y + scene_size.y};

            if (!status.world_loaded) {
                draw_list->AddRectFilled(scene_pos, scene_max, IM_COL32(11, 14, 19, 255), 6.0F);
                constexpr float grid_step = 32.0F;
                const ImU32 grid_color = IM_COL32(61, 70, 84, 42);
                for (float x = scene_pos.x; x < scene_max.x; x += grid_step) {
                    draw_list->AddLine({x, scene_pos.y}, {x, scene_max.y}, grid_color);
                }
                for (float y = scene_pos.y; y < scene_max.y; y += grid_step) {
                    draw_list->AddLine({scene_pos.x, y}, {scene_max.x, y}, grid_color);
                }
            }

            ImGui::InvisibleButton(
                "##SceneViewport",
                scene_size,
                ImGuiButtonFlags_MouseButtonLeft |
                    ImGuiButtonFlags_MouseButtonRight |
                    ImGuiButtonFlags_MouseButtonMiddle
            );

            const bool scene_hovered = ImGui::IsItemHovered();
            const bool scene_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            state_->scene_viewport = SceneViewport{
                .x = scene_pos.x,
                .y = scene_pos.y,
                .width = scene_size.x,
                .height = scene_size.y,
                .framebuffer_scale = state_->metrics.scale_factor,
                .hovered = scene_hovered,
                .focused = scene_focused,
            };

            const ImU32 border_color = scene_focused
                ? IM_COL32(73, 170, 246, 230)
                : scene_hovered
                    ? IM_COL32(73, 170, 246, 125)
                    : IM_COL32(48, 56, 69, 190);
            draw_list->AddRect(scene_pos, scene_max, border_color, 6.0F, 0, scene_focused ? 1.6F : 1.0F);

            if (!status.world_loaded) {
                const char* message = "Open a World to start.";
                const ImVec2 text_size = ImGui::CalcTextSize(message);
                draw_list->AddText(
                    {
                        scene_pos.x + (scene_size.x - text_size.x) * 0.5F,
                        scene_pos.y + (scene_size.y - text_size.y) * 0.5F,
                    },
                    IM_COL32(150, 160, 174, 255),
                    message
                );
            }
        }
        ImGui::End();
    }

    if (state_->show_inspector) {
        const bool visible = ImGui::Begin(inspector_window_name, nullptr);
        if (visible) {
            const auto selected_id = selection.selected();
            if (!selected_id.has_value()) {
                ImGui::Spacing();
                ImGui::TextDisabled("Select an entity to inspect it.");
            } else if (const auto inspector = project_inspector(world, *selected_id)) {
                ImGui::Text("Entity  %s", short_id(inspector->persistent_id).c_str());
                ImGui::Separator();

                if(inspector->character_attachment && ImGui::CollapsingHeader("Character carrier")) {
                    const auto& a=*inspector->character_attachment;
                    ImGui::Text("Carrier %s",a.carrier.to_string().c_str());
                    ImGui::Text("Local foot %.2f %.2f %.2f",static_cast<double>(a.offset.x),static_cast<double>(a.offset.y),static_cast<double>(a.offset.z));
                }
                if(inspector->vehicle && ImGui::CollapsingHeader("Vehicle")) {
                    const auto& v=*inspector->vehicle;
                    ImGui::Text("Wheelbase %.2f m   Track %.2f m",static_cast<double>(v.wheelbase),static_cast<double>(v.track));
                    ImGui::Text("Wheel radius %.2f m   Travel %.2f m",static_cast<double>(v.wheel_radius),static_cast<double>(v.suspension_travel));
                    ImGui::Text("Spring %.0f N/m   Damping %.0f Ns/m",static_cast<double>(v.spring),static_cast<double>(v.damping));
                    ImGui::Text("Drive %.0f N   Brake %.0f N",static_cast<double>(v.drive_force),static_cast<double>(v.brake_force));
                    ImGui::Text("Throttle %.2f   Steering %.2f   Brake %.2f",static_cast<double>(v.throttle),static_cast<double>(v.steering),static_cast<double>(v.brake));
                    ImGui::Text("Travelled %.2f m",v.distance);
                }
                if(inspector->material && ImGui::CollapsingHeader("Shared material")) {
                    const auto& material=*inspector->material;
                    const auto definition=world.material(material.id);
                    ImGui::TextUnformatted(definition?definition->name.c_str():"Legacy inline surface");
                    ImGui::Text("ID  %s",material.id.to_string().c_str());
                    ImGui::Text("Density %.3f kg/m3",static_cast<double>(material.density));
                    ImGui::Text("Friction %.3f   Bounce %.3f",static_cast<double>(material.friction),static_cast<double>(material.restitution));
                    ImGui::Text("Volume %.6f m3   Dynamic mass %.3f kg",material.volume,material.mass);
                }

                if (inspector->spatial_state.has_value() &&
                    ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const auto begin_transaction = [&](const char* label) {
                        if (ImGui::IsItemActivated()) {
                            state_->inspector_edit_id = inspector->persistent_id;
                            state_->inspector_edit_before = capture_entity_transform(world, inspector->persistent_id);
                            state_->inspector_edit_label = label;
                        }
                    };
                    const auto finish_transaction = [&]() {
                        if (!ImGui::IsItemDeactivatedAfterEdit() ||
                            !state_->inspector_edit_id.has_value() ||
                            !state_->inspector_edit_before.has_value() ||
                            *state_->inspector_edit_id != inspector->persistent_id) {
                            return;
                        }
                        const auto after = capture_entity_transform(world, inspector->persistent_id);
                        if (after.has_value()) {
                            history.record(TransformHistory::Entry{
                                .persistent_id = inspector->persistent_id,
                                .before = *state_->inspector_edit_before,
                                .after = *after,
                                .label = state_->inspector_edit_label,
                            });
                        }
                        state_->inspector_edit_id.reset();
                        state_->inspector_edit_before.reset();
                        state_->inspector_edit_label.clear();
                    };

                    auto current = capture_entity_transform(world, inspector->persistent_id);
                    if (current.has_value()) {
                        double position[3]{
                            current->spatial_state.global_position.x,
                            current->spatial_state.global_position.y,
                            current->spatial_state.global_position.z,
                        };
                        const bool position_changed = ImGui::DragScalarN(
                            "Position",
                            ImGuiDataType_Double,
                            position,
                            3,
                            0.05F,
                            nullptr,
                            nullptr,
                            "%.3f"
                        );
                        begin_transaction("Move Entity");
                        if (position_changed) {
                            current->spatial_state.global_position = {position[0], position[1], position[2]};
                            if (const auto region = world.region_containing(current->spatial_state.global_position)) {
                                current->spatial_state.region = *region;
                            }
                            if (!apply_entity_transform(world, inspector->persistent_id, *current)) {
                                core::log(core::LogLevel::error, "VICE Editor could not apply Inspector position edit.");
                            }
                        }
                        finish_transaction();

                        current = capture_entity_transform(world, inspector->persistent_id);
                        auto authored = authored_or_identity(current->authored_transform);
                        auto rotation = quaternion_to_euler_degrees(authored.rotation);
                        const bool rotation_changed = ImGui::DragFloat3(
                            "Rotation",
                            rotation.data(),
                            0.25F,
                            0.0F,
                            0.0F,
                            "%.1f°"
                        );
                        begin_transaction("Rotate Entity");
                        if (rotation_changed) {
                            authored.rotation = euler_degrees_to_quaternion(rotation);
                            current->authored_transform = authored;
                            if (!apply_entity_transform(world, inspector->persistent_id, *current)) {
                                core::log(core::LogLevel::error, "VICE Editor could not apply Inspector rotation edit.");
                            }
                        }
                        finish_transaction();

                        current = capture_entity_transform(world, inspector->persistent_id);
                        authored = authored_or_identity(current->authored_transform);
                        float scale[3]{authored.scale.x, authored.scale.y, authored.scale.z};
                        const float minimum_scale = 0.01F;
                        const bool scale_changed = ImGui::DragFloat3(
                            "Scale",
                            scale,
                            0.01F,
                            minimum_scale,
                            1000.0F,
                            "%.3f",
                            ImGuiSliderFlags_AlwaysClamp
                        );
                        begin_transaction("Scale Entity");
                        if (scale_changed) {
                            authored.scale = {scale[0], scale[1], scale[2]};
                            current->authored_transform = authored;
                            if (!apply_entity_transform(world, inspector->persistent_id, *current)) {
                                core::log(core::LogLevel::error, "VICE Editor could not apply Inspector scale edit.");
                            }
                        }
                        finish_transaction();
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Region %llu", static_cast<unsigned long long>(inspector->spatial_state->region.value));
                } else if (!inspector->spatial_state.has_value()) {
                    ImGui::TextDisabled("This entity has no spatial transform.");
                }

                if (inspector->camera.has_value() &&
                    ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const auto begin_camera_transaction = [&](const char* label) {
                        if (ImGui::IsItemActivated()) {
                            state_->inspector_camera_edit_id = inspector->persistent_id;
                            state_->inspector_camera_edit_before = world.camera(*inspector->entity);
                            state_->inspector_camera_edit_label = label;
                        }
                    };
                    const auto finish_camera_transaction = [&]() {
                        if (!ImGui::IsItemDeactivatedAfterEdit() ||
                            !state_->inspector_camera_edit_id.has_value() ||
                            !state_->inspector_camera_edit_before.has_value() ||
                            *state_->inspector_camera_edit_id != inspector->persistent_id) {
                            return;
                        }
                        const auto after = capture_entity_camera(world, inspector->persistent_id);
                        if (after.has_value()) {
                            history.record(CommandHistory::CameraEntry{
                                .before = {{
                                    .persistent_id = inspector->persistent_id,
                                    .camera = *state_->inspector_camera_edit_before,
                                }},
                                .after = {*after},
                                .label = state_->inspector_camera_edit_label,
                            });
                        }
                        state_->inspector_camera_edit_id.reset();
                        state_->inspector_camera_edit_before.reset();
                        state_->inspector_camera_edit_label.clear();
                    };

                    auto current_camera = world.camera(*inspector->entity);
                    if (current_camera.has_value()) {
                        constexpr float radians_to_degrees = 180.0F / pi;
                        constexpr float degrees_to_radians = pi / 180.0F;
                        float fov_degrees = current_camera->vertical_fov_radians * radians_to_degrees;
                        const bool fov_changed = ImGui::DragFloat(
                            "Vertical FOV",
                            &fov_degrees,
                            0.25F,
                            1.0F,
                            179.0F,
                            "%.1f°",
                            ImGuiSliderFlags_AlwaysClamp
                        );
                        begin_camera_transaction("Edit Camera FOV");
                        if (fov_changed) {
                            current_camera->vertical_fov_radians = fov_degrees * degrees_to_radians;
                            if (!world.set_camera(*inspector->entity, *current_camera)) {
                                core::log(core::LogLevel::error, "VICE Editor could not apply camera FOV edit.");
                            }
                        }
                        finish_camera_transaction();

                        current_camera = world.camera(*inspector->entity);
                        float near_plane = current_camera->near_plane;
                        const float near_max = std::max(0.001F, current_camera->far_plane - 0.001F);
                        const bool near_changed = ImGui::DragFloat(
                            "Near Plane",
                            &near_plane,
                            0.01F,
                            0.001F,
                            near_max,
                            "%.3f",
                            ImGuiSliderFlags_AlwaysClamp
                        );
                        begin_camera_transaction("Edit Camera Near Plane");
                        if (near_changed) {
                            current_camera->near_plane = near_plane;
                            if (!world.set_camera(*inspector->entity, *current_camera)) {
                                core::log(core::LogLevel::error, "VICE Editor could not apply camera near plane edit.");
                            }
                        }
                        finish_camera_transaction();

                        current_camera = world.camera(*inspector->entity);
                        float far_plane = current_camera->far_plane;
                        const float far_min = current_camera->near_plane + 0.001F;
                        const bool far_changed = ImGui::DragFloat(
                            "Far Plane",
                            &far_plane,
                            1.0F,
                            far_min,
                            1'000'000.0F,
                            "%.1f",
                            ImGuiSliderFlags_AlwaysClamp
                        );
                        begin_camera_transaction("Edit Camera Far Plane");
                        if (far_changed) {
                            current_camera->far_plane = far_plane;
                            if (!world.set_camera(*inspector->entity, *current_camera)) {
                                core::log(core::LogLevel::error, "VICE Editor could not apply camera far plane edit.");
                            }
                        }
                        finish_camera_transaction();

                        current_camera = world.camera(*inspector->entity);
                        bool primary = current_camera->primary;
                        if (ImGui::Checkbox("Primary", &primary) && primary != current_camera->primary) {
                            std::vector<EntityCameraState> before;
                            std::vector<EntityCameraState> after;
                            if (primary) {
                                if (const auto existing_primary = world.primary_camera();
                                    existing_primary.has_value() && *existing_primary != *inspector->entity) {
                                    const auto existing_id = world.persistent_id(*existing_primary);
                                    if (existing_id.has_value()) {
                                        const auto existing_before = capture_entity_camera(world, *existing_id);
                                        if (existing_before.has_value()) {
                                            before.push_back(*existing_before);
                                            auto demoted = *existing_before;
                                            demoted.camera->primary = false;
                                            after.push_back(std::move(demoted));
                                        }
                                    }
                                }
                            }
                            const auto target_before = capture_entity_camera(world, inspector->persistent_id);
                            if (target_before.has_value()) {
                                before.push_back(*target_before);
                                auto target_after = *target_before;
                                target_after.camera->primary = primary;
                                after.push_back(std::move(target_after));
                            }
                            if (!before.empty() && apply_camera_states(world, after)) {
                                history.record(CommandHistory::CameraEntry{
                                    .before = std::move(before),
                                    .after = std::move(after),
                                    .label = primary ? "Make Primary Camera" : "Clear Primary Camera",
                                });
                            } else {
                                core::log(core::LogLevel::error, "VICE Editor could not change the primary camera.");
                            }
                        }
                    }
                }

                if (inspector->light.has_value() &&
                    ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const auto begin_light_transaction = [&](const char* label) {
                        if (ImGui::IsItemActivated()) {
                            state_->inspector_light_edit_id = inspector->persistent_id;
                            state_->inspector_light_edit_before = world.light(*inspector->entity);
                            state_->inspector_light_edit_label = label;
                        }
                    };
                    const auto finish_light_transaction = [&]() {
                        if (!ImGui::IsItemDeactivatedAfterEdit() ||
                            !state_->inspector_light_edit_id.has_value() ||
                            !state_->inspector_light_edit_before.has_value() ||
                            *state_->inspector_light_edit_id != inspector->persistent_id) {
                            return;
                        }
                        const auto after = capture_entity_light(world, inspector->persistent_id);
                        if (after.has_value()) {
                            history.record(CommandHistory::LightEntry{
                                .before = {
                                    .persistent_id = inspector->persistent_id,
                                    .light = *state_->inspector_light_edit_before,
                                },
                                .after = *after,
                                .label = state_->inspector_light_edit_label,
                            });
                        }
                        state_->inspector_light_edit_id.reset();
                        state_->inspector_light_edit_before.reset();
                        state_->inspector_light_edit_label.clear();
                    };
                    const auto apply_light = [&](const world::Light& light, const char* error) {
                        if (!world.set_light(*inspector->entity, light)) {
                            core::log(core::LogLevel::error, error);
                            return false;
                        }
                        return true;
                    };

                    auto current_light = world.light(*inspector->entity);
                    if (current_light.has_value()) {
                        const auto before_type = capture_entity_light(world, inspector->persistent_id);
                        int type = current_light->type == world::LightType::directional ? 0 : 1;
                        const char* type_names[] = {"Directional", "Point"};
                        if (ImGui::Combo("Type", &type, type_names, 2)) {
                            auto changed = *current_light;
                            changed.type = type == 0 ? world::LightType::directional : world::LightType::point;
                            changed.range = changed.type == world::LightType::point
                                ? std::max(changed.range, 10.0F)
                                : 0.0F;
                            if (before_type.has_value() && apply_light(changed, "VICE Editor could not change light type.")) {
                                const auto after = capture_entity_light(world, inspector->persistent_id);
                                if (after.has_value()) {
                                    history.record(CommandHistory::LightEntry{
                                        .before = *before_type,
                                        .after = *after,
                                        .label = "Change Light Type",
                                    });
                                }
                            }
                        }

                        current_light = world.light(*inspector->entity);
                        const auto before_enabled = capture_entity_light(world, inspector->persistent_id);
                        bool enabled = current_light->enabled;
                        if (ImGui::Checkbox("Enabled", &enabled)) {
                            auto changed = *current_light;
                            changed.enabled = enabled;
                            if (before_enabled.has_value() && apply_light(changed, "VICE Editor could not change light enabled state.")) {
                                const auto after = capture_entity_light(world, inspector->persistent_id);
                                if (after.has_value()) {
                                    history.record(CommandHistory::LightEntry{
                                        .before = *before_enabled,
                                        .after = *after,
                                        .label = enabled ? "Enable Light" : "Disable Light",
                                    });
                                }
                            }
                        }

                        current_light = world.light(*inspector->entity);
                        float color[3]{
                            current_light->color.red,
                            current_light->color.green,
                            current_light->color.blue,
                        };
                        const bool color_changed = ImGui::ColorEdit3("Color", color);
                        begin_light_transaction("Edit Light Color");
                        if (color_changed) {
                            auto changed = *current_light;
                            changed.color = {color[0], color[1], color[2]};
                            (void)apply_light(changed, "VICE Editor could not apply light color edit.");
                        }
                        finish_light_transaction();

                        current_light = world.light(*inspector->entity);
                        float intensity = current_light->intensity;
                        const bool intensity_changed = ImGui::DragFloat(
                            "Intensity",
                            &intensity,
                            0.05F,
                            0.0F,
                            100000.0F,
                            "%.2f",
                            ImGuiSliderFlags_AlwaysClamp
                        );
                        begin_light_transaction("Edit Light Intensity");
                        if (intensity_changed) {
                            auto changed = *current_light;
                            changed.intensity = intensity;
                            (void)apply_light(changed, "VICE Editor could not apply light intensity edit.");
                        }
                        finish_light_transaction();

                        current_light = world.light(*inspector->entity);
                        if (current_light->type == world::LightType::point) {
                            float range = current_light->range;
                            const bool range_changed = ImGui::DragFloat(
                                "Range",
                                &range,
                                0.1F,
                                0.01F,
                                100000.0F,
                                "%.2f",
                                ImGuiSliderFlags_AlwaysClamp
                            );
                            begin_light_transaction("Edit Light Range");
                            if (range_changed) {
                                auto changed = *current_light;
                                changed.range = range;
                                (void)apply_light(changed, "VICE Editor could not apply light range edit.");
                            }
                            finish_light_transaction();
                        } else {
                            ImGui::TextDisabled("Direction follows the entity rotation (-Z forward).");
                        }
                    }
                }

                if (ImGui::CollapsingHeader("Runtime", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::BeginTable(
                        "##InspectorRuntime",
                        2,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings
                    )) {
                        property_row(
                            "Region",
                            inspector->spatial_state.has_value()
                                ? std::to_string(inspector->spatial_state->region.value)
                                : "None"
                        );
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
        ImGui::End();
    }

    if (state_->show_console) {
        const bool visible = ImGui::Begin(console_window_name, nullptr);
        if (visible) {
            filter_button("All", core::LogLevel::trace, state_->console_minimum);
            ImGui::SameLine();
            filter_button("Warnings", core::LogLevel::warning, state_->console_minimum);
            ImGui::SameLine();
            filter_button("Errors", core::LogLevel::error, state_->console_minimum);

            const float clear_width =
                ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2.0F;
            const float available = ImGui::GetContentRegionAvail().x;
            if (available > clear_width) {
                ImGui::SameLine(ImGui::GetCursorPosX() + available - clear_width);
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
                    ImGui::TextColored(
                        level_color(entry.level),
                        "%s",
                        core::log_level_name(entry.level).data()
                    );
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
    return valid() ? state_->scene_viewport : SceneViewport{};
}

TransformTool EditorUi::transform_tool() const noexcept {
    return valid() ? state_->transform_tool : TransformTool::move;
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

bool EditorUi::consume_save_request() noexcept {
    if (state_ == nullptr) {
        return false;
    }
    const bool requested = state_->save_requested;
    state_->save_requested = false;
    return requested;
}

bool EditorUi::consume_undo_request() noexcept {
    if (state_ == nullptr) {
        return false;
    }
    const bool requested = state_->undo_requested;
    state_->undo_requested = false;
    return requested;
}

bool EditorUi::consume_redo_request() noexcept {
    if (state_ == nullptr) {
        return false;
    }
    const bool requested = state_->redo_requested;
    state_->redo_requested = false;
    return requested;
}

} // namespace vice::editor
