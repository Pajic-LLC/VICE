#include <vice/editor/project_launcher_ui.hpp>

#include "ui_theme.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace vice::editor {
namespace {

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

[[nodiscard]] std::string compact_path(const std::filesystem::path& path) {
    const std::string value = path.generic_string();
    constexpr std::size_t maximum = 72U;
    if (value.size() <= maximum) return value;
    return "…" + value.substr(value.size() - (maximum - 3U));
}

void primary_button_style_begin() {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.10F, 0.38F, 0.64F, 1.00F});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.13F, 0.47F, 0.76F, 1.00F});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.08F, 0.31F, 0.55F, 1.00F});
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.97F, 0.99F, 1.00F, 1.00F});
}

void primary_button_style_end() {
    ImGui::PopStyleColor(4);
}

} // namespace

struct ProjectLauncherUi::State {
    ImGuiContext* context = nullptr;
    platform::WindowMetrics metrics{};
    float style_scale = 1.0F;
    bool frame_active = false;
    bool open_project_requested = false;
    bool choose_parent_requested = false;
    bool development_project_requested = false;
    bool quit_requested = false;
    bool open_new_project_modal = false;
    std::optional<NewProjectRequest> new_project_request;
    std::optional<std::filesystem::path> recent_project_request;
    std::filesystem::path new_project_parent;
    std::array<char, 128> new_project_name{};
    std::string error;
};

ProjectLauncherUi::ProjectLauncherUi() : state_{std::make_unique<State>()} {
    IMGUI_CHECKVERSION();
    state_->context = ImGui::CreateContext();
    if (state_->context == nullptr) return;

    ImGui::SetCurrentContext(state_->context);
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigDpiScaleFonts = true;
    io.IniFilename = nullptr;
    io.BackendPlatformName = "VICE Platform";
    detail::apply_editor_theme(1.0F);
}

ProjectLauncherUi::~ProjectLauncherUi() {
    if (!valid()) return;
    ImGui::SetCurrentContext(state_->context);
    ImGui::DestroyContext(state_->context);
    state_->context = nullptr;
}

bool ProjectLauncherUi::valid() const noexcept {
    return state_ != nullptr && state_->context != nullptr;
}

void ProjectLauncherUi::handle_event(const platform::Event& event) {
    if (!valid()) return;
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

bool ProjectLauncherUi::begin_frame(
    const platform::WindowMetrics metrics,
    const float delta_seconds
) {
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
    ImGui::NewFrame();
    state_->frame_active = true;
    return true;
}

void ProjectLauncherUi::draw(
    const std::span<const LauncherProjectEntry> recent_projects,
    const bool development_project_available
) {
    if (!valid() || !state_->frame_active) return;
    ImGui::SetCurrentContext(state_->context);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.027F, 0.033F, 0.044F, 1.0F});
    ImGui::Begin("###VICE.ProjectLauncher", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    const ImVec2 content = ImGui::GetContentRegionAvail();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    draw->AddCircleFilled(
        ImVec2{origin.x + content.x * 0.18F, origin.y + content.y * 0.14F},
        content.x * 0.28F,
        IM_COL32(25, 91, 142, 18),
        96
    );
    draw->AddCircleFilled(
        ImVec2{origin.x + content.x * 0.88F, origin.y + content.y * 0.82F},
        content.x * 0.34F,
        IM_COL32(24, 54, 94, 16),
        96
    );

    const float card_width = std::clamp(content.x - 64.0F, 560.0F, 820.0F);
    const float card_height = std::clamp(content.y - 64.0F, 520.0F, 720.0F);
    ImGui::SetCursorPos(ImVec2{
        std::max(24.0F, (content.x - card_width) * 0.5F),
        std::max(24.0F, (content.y - card_height) * 0.5F),
    });

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0F * state_->style_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{36.0F, 32.0F});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.043F, 0.052F, 0.067F, 0.985F});
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.20F, 0.25F, 0.32F, 0.72F});
    ImGui::BeginChild("###VICE.LauncherCard", ImVec2{card_width, card_height}, true);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    ImGui::SetWindowFontScale(1.65F);
    ImGui::TextUnformatted("VICE");
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextDisabled("Visual Interactive Computational Engine");
    ImGui::Dummy(ImVec2{0.0F, 22.0F});

    constexpr float action_height = 44.0F;
    const float action_width = std::min(260.0F, ImGui::GetContentRegionAvail().x);
    primary_button_style_begin();
    if (ImGui::Button("New Project", ImVec2{action_width, action_height})) {
        state_->open_new_project_modal = true;
        state_->error.clear();
    }
    primary_button_style_end();
    ImGui::SameLine(0.0F, 12.0F);
    if (ImGui::Button("Open Project", ImVec2{action_width, action_height})) {
        state_->open_project_requested = true;
        state_->error.clear();
    }

    if (!state_->error.empty()) {
        ImGui::Dummy(ImVec2{0.0F, 10.0F});
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImVec4{0.96F, 0.45F, 0.42F, 1.0F}, "%s", state_->error.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::Dummy(ImVec2{0.0F, 28.0F});
    ImGui::TextDisabled("RECENT PROJECTS");
    ImGui::Dummy(ImVec2{0.0F, 7.0F});

    if (recent_projects.empty()) {
        ImGui::TextDisabled("No recent projects");
    } else {
        const std::size_t shown = std::min<std::size_t>(recent_projects.size(), 6U);
        for (std::size_t index = 0; index < shown; ++index) {
            const auto& entry = recent_projects[index];
            ImGui::PushID(static_cast<int>(index));
            const ImVec2 row_start = ImGui::GetCursorScreenPos();
            const float row_width = ImGui::GetContentRegionAvail().x;
            const float row_height = 48.0F * state_->style_scale;
            const bool hovered = ImGui::InvisibleButton("recent", ImVec2{row_width, row_height});
            if (hovered) {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    row_start,
                    ImVec2{row_start.x + row_width, row_start.y + row_height},
                    ImGui::GetColorU32(ImVec4{0.11F, 0.16F, 0.22F, 0.78F}),
                    7.0F * state_->style_scale
                );
            }
            if (ImGui::IsItemActivated()) {
                state_->recent_project_request = entry.manifest_path;
                state_->error.clear();
            }
            ImGui::GetWindowDrawList()->AddText(
                ImVec2{row_start.x + 12.0F, row_start.y + 7.0F},
                ImGui::GetColorU32(ImVec4{0.90F, 0.94F, 0.98F, 1.0F}),
                entry.name.c_str()
            );
            const std::string path = compact_path(entry.manifest_path.parent_path());
            ImGui::GetWindowDrawList()->AddText(
                ImVec2{row_start.x + 12.0F, row_start.y + 26.0F},
                ImGui::GetColorU32(ImVec4{0.49F, 0.55F, 0.63F, 1.0F}),
                path.c_str()
            );
            ImGui::PopID();
        }
    }

    const float footer_height = ImGui::GetFrameHeight() + 8.0F;
    if (ImGui::GetContentRegionAvail().y > footer_height) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y - footer_height);
    }
    ImGui::Separator();
    if (development_project_available) {
        if (ImGui::SmallButton("Open VICE Lab")) {
            state_->development_project_requested = true;
            state_->error.clear();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Development project");
    }
    ImGui::EndChild();
    ImGui::End();

    if (state_->open_new_project_modal) {
        ImGui::OpenPopup("New Project###VICE.NewProject");
        state_->open_new_project_modal = false;
    }

    ImGui::SetNextWindowSize(ImVec2{520.0F * state_->style_scale, 0.0F}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(
            "New Project###VICE.NewProject",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
        )) {
        ImGui::TextUnformatted("New Project");
        ImGui::TextDisabled("Name and location");
        ImGui::Dummy(ImVec2{0.0F, 12.0F});

        ImGui::TextDisabled("Name");
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint(
            "###VICE.NewProject.Name",
            "My Game",
            state_->new_project_name.data(),
            state_->new_project_name.size()
        );

        ImGui::Dummy(ImVec2{0.0F, 8.0F});
        ImGui::TextDisabled("Location");
        const std::string parent = state_->new_project_parent.empty()
            ? std::string{"Choose a folder"}
            : compact_path(state_->new_project_parent);
        ImGui::SetNextItemWidth(-105.0F * state_->style_scale);
        ImGui::BeginDisabled();
        ImGui::InputText(
            "###VICE.NewProject.Parent",
            const_cast<char*>(parent.c_str()),
            parent.size() + 1U,
            ImGuiInputTextFlags_ReadOnly
        );
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Choose…", ImVec2{96.0F * state_->style_scale, 0.0F})) {
            state_->choose_parent_requested = true;
        }

        if (!state_->error.empty()) {
            ImGui::Dummy(ImVec2{0.0F, 8.0F});
            ImGui::PushTextWrapPos();
            ImGui::TextColored(ImVec4{0.96F, 0.45F, 0.42F, 1.0F}, "%s", state_->error.c_str());
            ImGui::PopTextWrapPos();
        }

        ImGui::Dummy(ImVec2{0.0F, 16.0F});
        const bool can_create = state_->new_project_name[0] != '\0' && !state_->new_project_parent.empty();
        ImGui::BeginDisabled(!can_create);
        primary_button_style_begin();
        if (ImGui::Button("Create Project", ImVec2{150.0F * state_->style_scale, 38.0F * state_->style_scale})) {
            state_->new_project_request = NewProjectRequest{
                .name = state_->new_project_name.data(),
                .parent_directory = state_->new_project_parent,
            };
            state_->error.clear();
        }
        primary_button_style_end();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2{90.0F * state_->style_scale, 38.0F * state_->style_scale})) {
            state_->error.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ProjectLauncherUi::end_frame() {
    if (!valid() || !state_->frame_active) return;
    ImGui::SetCurrentContext(state_->context);
    ImGui::Render();
    state_->frame_active = false;
}

void ProjectLauncherUi::set_new_project_parent(std::filesystem::path parent_directory) {
    if (state_ == nullptr) return;
    state_->new_project_parent = std::move(parent_directory);
    state_->error.clear();
}

void ProjectLauncherUi::set_error(std::string message) {
    if (state_ == nullptr) return;
    state_->error = std::move(message);
}

void ProjectLauncherUi::clear_error() {
    if (state_ == nullptr) return;
    state_->error.clear();
}

bool ProjectLauncherUi::consume_open_project_request() noexcept {
    if (state_ == nullptr) return false;
    return std::exchange(state_->open_project_requested, false);
}

bool ProjectLauncherUi::consume_choose_project_parent_request() noexcept {
    if (state_ == nullptr) return false;
    return std::exchange(state_->choose_parent_requested, false);
}

bool ProjectLauncherUi::consume_development_project_request() noexcept {
    if (state_ == nullptr) return false;
    return std::exchange(state_->development_project_requested, false);
}

bool ProjectLauncherUi::consume_quit_request() noexcept {
    if (state_ == nullptr) return false;
    return std::exchange(state_->quit_requested, false);
}

std::optional<NewProjectRequest> ProjectLauncherUi::consume_new_project_request() {
    if (state_ == nullptr) return std::nullopt;
    return std::exchange(state_->new_project_request, std::nullopt);
}

std::optional<std::filesystem::path> ProjectLauncherUi::consume_recent_project_request() {
    if (state_ == nullptr) return std::nullopt;
    return std::exchange(state_->recent_project_request, std::nullopt);
}

} // namespace vice::editor
