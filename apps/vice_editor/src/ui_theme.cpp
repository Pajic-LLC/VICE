#include "ui_theme.hpp"

#include <imgui.h>

#include <algorithm>

namespace vice::editor::detail {
namespace {

[[nodiscard]] float scaled(const float value, const float scale) noexcept {
    return value * std::max(0.75F, scale);
}

} // namespace

void apply_editor_theme(const float scale_factor) {
    const float scale = std::max(0.75F, scale_factor);

    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();

    style.WindowPadding = {scaled(10.0F, scale), scaled(10.0F, scale)};
    style.FramePadding = {scaled(9.0F, scale), scaled(5.0F, scale)};
    style.CellPadding = {scaled(8.0F, scale), scaled(5.0F, scale)};
    style.ItemSpacing = {scaled(8.0F, scale), scaled(7.0F, scale)};
    style.ItemInnerSpacing = {scaled(7.0F, scale), scaled(5.0F, scale)};
    style.IndentSpacing = scaled(18.0F, scale);
    style.ScrollbarSize = scaled(11.0F, scale);
    style.GrabMinSize = scaled(10.0F, scale);

    style.WindowRounding = scaled(7.0F, scale);
    style.ChildRounding = scaled(6.0F, scale);
    style.FrameRounding = scaled(5.0F, scale);
    style.PopupRounding = scaled(7.0F, scale);
    style.ScrollbarRounding = scaled(9.0F, scale);
    style.GrabRounding = scaled(5.0F, scale);
    style.TabRounding = scaled(5.0F, scale);

    style.WindowBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.TabBorderSize = 0.0F;
    style.TabBarBorderSize = 1.0F;

    style.AntiAliasedLines = true;
    style.AntiAliasedLinesUseTex = true;
    style.AntiAliasedFill = true;

    auto* colors = style.Colors;
    colors[ImGuiCol_Text] = {0.91F, 0.93F, 0.96F, 1.00F};
    colors[ImGuiCol_TextDisabled] = {0.47F, 0.51F, 0.58F, 1.00F};
    colors[ImGuiCol_WindowBg] = {0.050F, 0.057F, 0.072F, 1.00F};
    colors[ImGuiCol_ChildBg] = {0.061F, 0.069F, 0.086F, 1.00F};
    colors[ImGuiCol_PopupBg] = {0.064F, 0.073F, 0.091F, 0.99F};
    colors[ImGuiCol_Border] = {0.16F, 0.18F, 0.22F, 1.00F};
    colors[ImGuiCol_BorderShadow] = {0.00F, 0.00F, 0.00F, 0.00F};
    colors[ImGuiCol_FrameBg] = {0.086F, 0.098F, 0.122F, 1.00F};
    colors[ImGuiCol_FrameBgHovered] = {0.112F, 0.129F, 0.161F, 1.00F};
    colors[ImGuiCol_FrameBgActive] = {0.132F, 0.154F, 0.193F, 1.00F};
    colors[ImGuiCol_TitleBg] = {0.047F, 0.054F, 0.068F, 1.00F};
    colors[ImGuiCol_TitleBgActive] = {0.061F, 0.070F, 0.087F, 1.00F};
    colors[ImGuiCol_TitleBgCollapsed] = {0.047F, 0.054F, 0.068F, 1.00F};
    colors[ImGuiCol_MenuBarBg] = {0.049F, 0.056F, 0.070F, 1.00F};
    colors[ImGuiCol_ScrollbarBg] = {0.043F, 0.049F, 0.062F, 1.00F};
    colors[ImGuiCol_ScrollbarGrab] = {0.19F, 0.22F, 0.27F, 1.00F};
    colors[ImGuiCol_ScrollbarGrabHovered] = {0.25F, 0.29F, 0.35F, 1.00F};
    colors[ImGuiCol_ScrollbarGrabActive] = {0.31F, 0.36F, 0.43F, 1.00F};
    colors[ImGuiCol_CheckMark] = {0.27F, 0.68F, 0.98F, 1.00F};
    colors[ImGuiCol_SliderGrab] = {0.24F, 0.61F, 0.91F, 1.00F};
    colors[ImGuiCol_SliderGrabActive] = {0.31F, 0.73F, 1.00F, 1.00F};
    colors[ImGuiCol_Button] = {0.100F, 0.116F, 0.145F, 1.00F};
    colors[ImGuiCol_ButtonHovered] = {0.142F, 0.174F, 0.220F, 1.00F};
    colors[ImGuiCol_ButtonActive] = {0.176F, 0.225F, 0.290F, 1.00F};
    colors[ImGuiCol_Header] = {0.105F, 0.126F, 0.159F, 1.00F};
    colors[ImGuiCol_HeaderHovered] = {0.137F, 0.177F, 0.226F, 1.00F};
    colors[ImGuiCol_HeaderActive] = {0.170F, 0.230F, 0.300F, 1.00F};
    colors[ImGuiCol_Separator] = {0.15F, 0.17F, 0.21F, 1.00F};
    colors[ImGuiCol_SeparatorHovered] = {0.28F, 0.61F, 0.88F, 0.78F};
    colors[ImGuiCol_SeparatorActive] = {0.31F, 0.70F, 1.00F, 1.00F};
    colors[ImGuiCol_ResizeGrip] = {0.27F, 0.58F, 0.82F, 0.18F};
    colors[ImGuiCol_ResizeGripHovered] = {0.30F, 0.67F, 0.96F, 0.55F};
    colors[ImGuiCol_ResizeGripActive] = {0.31F, 0.72F, 1.00F, 0.85F};
    colors[ImGuiCol_Tab] = {0.070F, 0.081F, 0.101F, 1.00F};
    colors[ImGuiCol_TabHovered] = {0.118F, 0.160F, 0.208F, 1.00F};
    colors[ImGuiCol_TabSelected] = {0.096F, 0.125F, 0.163F, 1.00F};
    colors[ImGuiCol_TabSelectedOverline] = {0.28F, 0.68F, 0.98F, 1.00F};
    colors[ImGuiCol_TabDimmed] = {0.054F, 0.061F, 0.076F, 1.00F};
    colors[ImGuiCol_TabDimmedSelected] = {0.071F, 0.082F, 0.103F, 1.00F};
    colors[ImGuiCol_DockingPreview] = {0.25F, 0.65F, 0.96F, 0.55F};
    colors[ImGuiCol_DockingEmptyBg] = {0.038F, 0.044F, 0.056F, 1.00F};
    colors[ImGuiCol_TextSelectedBg] = {0.20F, 0.52F, 0.82F, 0.34F};
    colors[ImGuiCol_NavCursor] = {0.31F, 0.73F, 1.00F, 0.90F};
    colors[ImGuiCol_NavWindowingHighlight] = {0.72F, 0.84F, 0.95F, 0.70F};
    colors[ImGuiCol_NavWindowingDimBg] = {0.08F, 0.09F, 0.11F, 0.78F};
    colors[ImGuiCol_ModalWindowDimBg] = {0.02F, 0.025F, 0.035F, 0.82F};
}

} // namespace vice::editor::detail
