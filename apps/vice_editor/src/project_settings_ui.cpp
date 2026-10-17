#include <vice/editor/project_settings_ui.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>

namespace vice::editor {
namespace {

void copy_text(std::array<char, 256>& buffer, const std::string& text) {
    const std::size_t count = std::min(buffer.size() - 1U, text.size());
    std::memset(buffer.data(), 0, buffer.size());
    std::memcpy(buffer.data(), text.data(), count);
}

[[nodiscard]] std::string buffer_text(const std::array<char, 256>& buffer) {
    return std::string{buffer.data()};
}

} // namespace

struct ProjectSettingsUi::State {
    bool open = false;
    bool awaiting_save = false;
    std::array<char, 256> project_name{};
    std::array<char, 256> game_name{};
    std::filesystem::path startup_world;
    std::string diagnostics;
    std::optional<ProjectSettingsUpdate> save_request;
};

ProjectSettingsUi::ProjectSettingsUi() : state_{std::make_unique<State>()} {}
ProjectSettingsUi::~ProjectSettingsUi() = default;
ProjectSettingsUi::ProjectSettingsUi(ProjectSettingsUi&&) noexcept = default;
ProjectSettingsUi& ProjectSettingsUi::operator=(ProjectSettingsUi&&) noexcept = default;

void ProjectSettingsUi::draw(const ProjectSettingsStatus& status, const AssetIndex& assets) {
    if (state_ == nullptr) return;

    if (ImGui::Begin("Project###VICE.ProjectWorkflow")) {
        ImGui::Separator();
        if (ImGui::Button("Project Settings")) {
            copy_text(state_->project_name, status.project_name);
            copy_text(state_->game_name, status.game_name);
            state_->startup_world = status.startup_world;
            state_->diagnostics.clear();
            state_->awaiting_save = false;
            state_->open = true;
        }
    }
    ImGui::End();

    if (!state_->open) return;

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2{540.0F, 360.0F}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Project Settings###VICE.ProjectSettings", &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled("Project");
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputText("##ProjectName", state_->project_name.data(), state_->project_name.size());

        ImGui::Spacing();
        ImGui::TextDisabled("Game Display Name");
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputText("##GameName", state_->game_name.data(), state_->game_name.size());

        ImGui::Spacing();
        ImGui::TextDisabled("Startup World");
        const std::string startup_label = state_->startup_world.empty()
            ? std::string{"Select a World"}
            : state_->startup_world.generic_string();
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo("##StartupWorld", startup_label.c_str())) {
            for (const auto& asset : assets.assets()) {
                if (asset.kind != AssetKind::world || asset.source_path.empty()) continue;
                const bool selected = asset.relative_path == state_->startup_world;
                const std::string label = asset.relative_path.generic_string();
                if (ImGui::Selectable(label.c_str(), selected)) {
                    state_->startup_world = asset.relative_path;
                    state_->diagnostics.clear();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::BeginTable(
                "##ProjectSettingsIdentity",
                2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings
            )) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Project ID");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", status.project_id.to_string().c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Asset Root");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", status.asset_root.generic_string().c_str());
            ImGui::EndTable();
        }

        const std::string project_name = buffer_text(state_->project_name);
        const std::string game_name = buffer_text(state_->game_name);
        const bool dirty = project_name != status.project_name ||
            game_name != status.game_name || state_->startup_world != status.startup_world;

        if (!state_->diagnostics.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.96F, 0.42F, 0.40F, 1.0F});
            ImGui::TextWrapped("%s", state_->diagnostics.c_str());
            ImGui::PopStyleColor();
        } else if (dirty) {
            ImGui::Spacing();
            ImGui::TextDisabled("Unsaved changes");
        }

        ImGui::Spacing();
        const bool valid_input = !project_name.empty() && !game_name.empty() && !state_->startup_world.empty();
        ImGui::BeginDisabled(state_->awaiting_save || !dirty || !valid_input);
        if (ImGui::Button("Save")) {
            state_->save_request = ProjectSettingsUpdate{
                .project_name = project_name,
                .game_name = game_name,
                .startup_world = state_->startup_world,
            };
            state_->awaiting_save = true;
            state_->diagnostics.clear();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(state_->awaiting_save);
        if (ImGui::Button("Cancel")) open = false;
        ImGui::EndDisabled();
    }
    ImGui::End();

    if (!open && !state_->awaiting_save) {
        state_->open = false;
        state_->diagnostics.clear();
    }
}

std::optional<ProjectSettingsUpdate> ProjectSettingsUi::consume_save_request() {
    if (state_ == nullptr) return std::nullopt;
    auto request = std::move(state_->save_request);
    state_->save_request.reset();
    return request;
}

void ProjectSettingsUi::complete_save(const bool success, std::string diagnostics) {
    if (state_ == nullptr) return;
    state_->awaiting_save = false;
    state_->diagnostics = std::move(diagnostics);
    if (success) {
        state_->open = false;
        state_->diagnostics.clear();
    }
}

} // namespace vice::editor
