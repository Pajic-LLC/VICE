#include <vice/editor/resource_browser_ui.hpp>
#include <vice/editor/project_workflow_ui.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

namespace vice::editor {
namespace {

constexpr const char* resource_browser_window_name = "Resources###VICE.Resources";
constexpr const char* inspector_window_name = "Inspector###VICE.Inspector";

[[nodiscard]] std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] bool matches_filter(const AssetRecord& asset, const char* filter) {
    if (filter == nullptr || *filter == '\0') return true;
    const std::string needle = lower_copy(filter);
    const std::string name = lower_copy(asset.name);
    const std::string path = lower_copy(asset.relative_path.generic_string());
    return name.find(needle) != std::string::npos || path.find(needle) != std::string::npos;
}

[[nodiscard]] const char* status_label(const AssetRecord& asset) noexcept {
    if (asset.kind == AssetKind::shader_source && !asset.cooked_available) return "Source";
    if (!asset.cooked_available) return "Not cooked";
    return asset.cooked_valid ? "Ready" : "Invalid";
}

[[nodiscard]] ImVec4 status_color(const AssetRecord& asset) noexcept {
    if (asset.kind == AssetKind::shader_source && !asset.cooked_available) {
        return {0.56F, 0.69F, 0.86F, 1.0F};
    }
    if (!asset.cooked_available) return {0.70F, 0.62F, 0.48F, 1.0F};
    if (asset.cooked_valid) return {0.45F, 0.82F, 0.60F, 1.0F};
    return {0.92F, 0.40F, 0.40F, 1.0F};
}

[[nodiscard]] const char* resource_role_name(const world::ResourceRole role) noexcept {
    switch (role) {
    case world::ResourceRole::render_mesh: return "Render mesh";
    }
    return "Resource";
}

void inspector_property(const char* name, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", name);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextWrapped("%s", value.c_str());
}

[[nodiscard]] std::string display_path(
    const std::filesystem::path& path,
    const std::filesystem::path& root
) {
    if (path.empty()) return "None";
    std::error_code error;
    if (!root.empty()) {
        const auto relative = std::filesystem::relative(path, root, error);
        if (!error && !relative.empty()) {
            const std::string relative_text = relative.generic_string();
            if (!relative_text.starts_with("..")) return relative_text;
        }
    }
    return path.generic_string();
}

void copy_path(std::array<char, 512>& buffer, const std::filesystem::path& path) {
    const std::string text = path.generic_string();
    const std::size_t count = std::min(buffer.size() - 1U, text.size());
    std::memset(buffer.data(), 0, buffer.size());
    std::memcpy(buffer.data(), text.data(), count);
}

void draw_error_text(const std::string& diagnostics) {
    if (diagnostics.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.96F, 0.42F, 0.40F, 1.0F});
    ImGui::TextWrapped("%s", diagnostics.c_str());
    ImGui::PopStyleColor();
}

} // namespace

struct ResourceBrowserUi::State {
    std::array<char, 160> filter{};
    int kind_filter = -1;
    std::filesystem::path selected_path;
    bool has_selection = false;
    std::optional<world::ResourceId> add_mesh_request;
};

ResourceBrowserUi::ResourceBrowserUi() : state_{std::make_unique<State>()} {}
ResourceBrowserUi::~ResourceBrowserUi() = default;
ResourceBrowserUi::ResourceBrowserUi(ResourceBrowserUi&&) noexcept = default;
ResourceBrowserUi& ResourceBrowserUi::operator=(ResourceBrowserUi&&) noexcept = default;

void ResourceBrowserUi::draw(AssetIndex& assets) {
    if (state_ == nullptr) return;

    ImGui::SetNextWindowSize(ImVec2{720.0F, 330.0F}, ImGuiCond_FirstUseEver);
    const bool visible = ImGui::Begin(resource_browser_window_name);
    if (!visible) {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(std::min(320.0F, ImGui::GetContentRegionAvail().x * 0.52F));
    ImGui::InputTextWithHint("##ResourceFilter", "Search assets", state_->filter.data(), state_->filter.size());
    ImGui::SameLine();

    static constexpr std::array<const char*, 7> kind_names{
        "All",
        "World",
        "Mesh",
        "Collision",
        "Rigid Material",
        "Shader",
        "Shader Manifest",
    };
    ImGui::SetNextItemWidth(150.0F);
    const char* preview = state_->kind_filter < 0
        ? kind_names[0]
        : kind_names[static_cast<std::size_t>(state_->kind_filter + 1)];
    if (ImGui::BeginCombo("##ResourceKind", preview)) {
        if (ImGui::Selectable("All", state_->kind_filter < 0)) state_->kind_filter = -1;
        for (int index = 0; index < 6; ++index) {
            if (ImGui::Selectable(kind_names[static_cast<std::size_t>(index + 1)], state_->kind_filter == index)) {
                state_->kind_filter = index;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        (void)assets.refresh(assets.source_root(), assets.cooked_root());
    }

    ImGui::Separator();

    const float details_height = 118.0F;
    const float table_height = std::max(110.0F, ImGui::GetContentRegionAvail().y - details_height);
    if (ImGui::BeginTable(
            "##ResourceTable",
            3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp,
            ImVec2{0.0F, table_height}
        )) {
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 118.0F);
        ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 92.0F);
        ImGui::TableHeadersRow();

        for (const auto& asset : assets.assets()) {
            if (state_->kind_filter >= 0 && static_cast<int>(asset.kind) != state_->kind_filter) continue;
            if (!matches_filter(asset, state_->filter.data())) continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", asset_kind_name(asset.kind));

            ImGui::TableSetColumnIndex(1);
            const bool selected = state_->has_selection && state_->selected_path == asset.relative_path;
            const std::string label = asset.name + "##" + asset.relative_path.generic_string();
            if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                state_->selected_path = asset.relative_path;
                state_->has_selection = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", asset.relative_path.generic_string().c_str());
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(status_color(asset), "%s", status_label(asset));
        }
        ImGui::EndTable();
    }

    const AssetRecord* selected_asset = nullptr;
    if (state_->has_selection) {
        for (const auto& asset : assets.assets()) {
            if (asset.relative_path == state_->selected_path) {
                selected_asset = &asset;
                break;
            }
        }
    }

    ImGui::Separator();
    if (selected_asset == nullptr) {
        ImGui::TextDisabled("Select an asset to inspect it.");
    } else {
        ImGui::TextUnformatted(selected_asset->name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", asset_kind_name(selected_asset->kind));

        if (!selected_asset->summary.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("  %s", selected_asset->summary.c_str());
        }

        if (selected_asset->resource_id.has_value()) {
            ImGui::TextDisabled("ID  %s", selected_asset->resource_id->value.to_string().c_str());
        }
        ImGui::TextDisabled("Source  %s", selected_asset->source_path.empty()
            ? "None"
            : selected_asset->source_path.generic_string().c_str());
        ImGui::TextDisabled("Cooked  %s", selected_asset->cooked_path.empty()
            ? "None"
            : selected_asset->cooked_path.generic_string().c_str());

        if (!selected_asset->diagnostics.empty()) {
            ImGui::TextWrapped("%s", selected_asset->diagnostics.c_str());
        }

        if (selected_asset->instantiable_mesh()) {
            if (ImGui::Button("Add to Scene")) {
                state_->add_mesh_request = selected_asset->resource_id;
            }
        } else if (selected_asset->kind == AssetKind::mesh && selected_asset->cooked_valid &&
            !selected_asset->resource_id.has_value()) {
            ImGui::TextDisabled("Mesh has no stable asset ID.");
        }
    }

    ImGui::End();
}

void ResourceBrowserUi::draw_inspector_resources(
    const world::World& world,
    const SelectionModel& selection,
    const AssetIndex& assets
) {
    const auto selected = selection.selected();
    if (!selected.has_value()) return;

    ImGuiWindow* inspector_window = ImGui::FindWindowByName(inspector_window_name);
    if (inspector_window == nullptr || !inspector_window->Active) return;

    const auto inspector = project_inspector(world, *selected);
    if (!inspector.has_value() || inspector->resource_references.empty()) return;

    const bool visible = ImGui::Begin(inspector_window_name);
    if (visible && ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto reference : inspector->resource_references) {
            const std::string id = reference.resource.value.to_string();
            ImGui::PushID(id.c_str());
            ImGui::TextUnformatted(resource_role_name(reference.role));
            if (ImGui::BeginTable(
                    "##InspectorResource",
                    2,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings
                )) {
                inspector_property("Resource ID", id);
                if (const AssetRecord* asset = assets.find_resource(reference.resource); asset != nullptr) {
                    inspector_property("Asset", std::string{asset_kind_name(asset->kind)} + " · " + asset->name);
                    inspector_property("Source", display_path(asset->source_path, assets.source_root()));
                    inspector_property("Cooked", display_path(asset->cooked_path, assets.cooked_root()));
                } else {
                    inspector_property("Asset", "Unresolved");
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

std::optional<world::ResourceId> ResourceBrowserUi::consume_add_mesh_request() noexcept {
    if (state_ == nullptr) return std::nullopt;
    const auto request = state_->add_mesh_request;
    state_->add_mesh_request.reset();
    return request;
}

struct ProjectWorkflowUi::State {
    std::array<char, 128> project_name{};
    std::array<char, 512> project_parent{};
    std::array<char, 512> open_project_path{};
    std::array<char, 512> open_world_path{};
    std::array<char, 512> save_world_path{};
    bool open_new_project = false;
    bool open_project = false;
    bool open_world = false;
    bool save_world_as = false;
    bool open_unsaved_prompt = false;
    std::optional<ReplacementKind> pending_replacement;
    std::filesystem::path pending_path;
    std::optional<ProjectCreateRequest> new_project_request;
    std::optional<std::filesystem::path> open_project_request;
    bool open_development_project_request = false;
    bool new_world_request = false;
    std::optional<std::filesystem::path> open_world_request;
    bool save_world_request = false;
    std::optional<std::filesystem::path> save_world_as_request;
    std::optional<ConfirmedReplacement> confirmed_replacement;
};

ProjectWorkflowUi::ProjectWorkflowUi() : state_{std::make_unique<State>()} {}
ProjectWorkflowUi::~ProjectWorkflowUi() = default;
ProjectWorkflowUi::ProjectWorkflowUi(ProjectWorkflowUi&&) noexcept = default;
ProjectWorkflowUi& ProjectWorkflowUi::operator=(ProjectWorkflowUi&&) noexcept = default;

void ProjectWorkflowUi::draw(const ProjectWorkflowStatus& status) {
    if (state_ == nullptr) return;

    const auto queue_replacement = [&](const ReplacementKind kind, std::filesystem::path path = {}) {
        if (!status.world_dirty) {
            if (kind == ReplacementKind::new_world) state_->new_world_request = true;
            else if (kind == ReplacementKind::open_world) state_->open_world_request = std::move(path);
            else state_->open_project_request = std::move(path);
            return;
        }
        state_->pending_replacement = kind;
        state_->pending_path = std::move(path);
        state_->open_unsaved_prompt = true;
    };

    if (!status.project_loaded) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 size{680.0F, 470.0F};
        ImGui::SetNextWindowPos(
            ImVec2{viewport->WorkPos.x + viewport->WorkSize.x * 0.5F, viewport->WorkPos.y + viewport->WorkSize.y * 0.5F},
            ImGuiCond_Always,
            ImVec2{0.5F, 0.5F}
        );
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;
        ImGui::Begin("###VICE.ProjectStart", nullptr, flags);
        ImGui::Dummy(ImVec2{0.0F, 30.0F});
        const float center_x = ImGui::GetWindowWidth() * 0.5F;
        const char* title = "VICE";
        const ImVec2 title_size = ImGui::CalcTextSize(title);
        ImGui::SetCursorPosX(center_x - title_size.x * 0.5F);
        ImGui::TextUnformatted(title);
        const char* subtitle = "Projects";
        const ImVec2 subtitle_size = ImGui::CalcTextSize(subtitle);
        ImGui::SetCursorPosX(center_x - subtitle_size.x * 0.5F);
        ImGui::TextDisabled("%s", subtitle);
        ImGui::Dummy(ImVec2{0.0F, 28.0F});

        const float button_width = 240.0F;
        ImGui::SetCursorPosX(center_x - button_width - 8.0F);
        if (ImGui::Button("New Project", ImVec2{button_width, 48.0F})) state_->open_new_project = true;
        ImGui::SameLine(0.0F, 16.0F);
        if (ImGui::Button("Open Project", ImVec2{button_width, 48.0F})) state_->open_project = true;

        if (!status.recent_projects.empty()) {
            ImGui::Dummy(ImVec2{0.0F, 24.0F});
            ImGui::TextDisabled("Recent");
            const std::size_t visible_count = std::min<std::size_t>(status.recent_projects.size(), 4U);
            for (std::size_t index = 0; index < visible_count; ++index) {
                const auto& recent = status.recent_projects[index];
                ImGui::PushID(static_cast<int>(index));
                const std::string label = recent.name + "##RecentProject";
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None, ImVec2{0.0F, 28.0F})) {
                    state_->open_project_request = recent.manifest_path;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", recent.manifest_path.generic_string().c_str());
                }
                ImGui::PopID();
            }
        }

        if (!status.development_project.empty()) {
            ImGui::Dummy(ImVec2{0.0F, 18.0F});
            const char* dev_label = "Open VICE Lab development project";
            const ImVec2 dev_size = ImGui::CalcTextSize(dev_label);
            ImGui::SetCursorPosX(center_x - (dev_size.x + 28.0F) * 0.5F);
            if (ImGui::SmallButton(dev_label)) state_->open_development_project_request = true;
        }
        ImGui::Dummy(ImVec2{0.0F, 14.0F});
        draw_error_text(status.diagnostics);
        ImGui::End();
    } else {
        ImGui::SetNextWindowSize(ImVec2{360.0F, 190.0F}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Project###VICE.ProjectWorkflow")) {
            ImGui::TextUnformatted(status.project_name.c_str());
            ImGui::TextDisabled("%s", status.project_manifest.generic_string().c_str());
            ImGui::Separator();
            ImGui::TextDisabled("World");
            ImGui::SameLine();
            ImGui::TextUnformatted(status.world_name.c_str());
            if (status.world_dirty) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4{0.96F, 0.70F, 0.28F, 1.0F}, "Unsaved");
            }

            if (ImGui::Button("New World")) queue_replacement(ReplacementKind::new_world);
            ImGui::SameLine();
            if (ImGui::Button("Open World")) state_->open_world = true;
            ImGui::SameLine();
            if (ImGui::Button("Switch Project")) state_->open_project = true;

            if (ImGui::Button("Save")) state_->save_world_request = true;
            ImGui::SameLine();
            if (ImGui::Button("Save As")) {
                if (status.world_has_path) copy_path(state_->save_world_path, status.world_path);
                state_->save_world_as = true;
            }
            draw_error_text(status.diagnostics);
        }
        ImGui::End();
    }

    if (state_->open_new_project) ImGui::OpenPopup("New Project###VICE.NewProject");
    state_->open_new_project = false;
    if (ImGui::BeginPopupModal("New Project###VICE.NewProject", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("New Project");
        ImGui::Separator();
        ImGui::SetNextItemWidth(430.0F);
        ImGui::InputTextWithHint("##ProjectName", "Project name", state_->project_name.data(), state_->project_name.size());
        ImGui::SetNextItemWidth(430.0F);
        ImGui::InputTextWithHint("##ProjectParent", "Parent folder", state_->project_parent.data(), state_->project_parent.size());
        ImGui::Dummy(ImVec2{0.0F, 6.0F});
        const bool ready = state_->project_name[0] != '\0' && state_->project_parent[0] != '\0';
        if (!ready) ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2{110.0F, 0.0F})) {
            ProjectCreateRequest request;
            request.name = state_->project_name.data();
            request.parent_directory = std::filesystem::path{state_->project_parent.data()};
            state_->new_project_request = std::move(request);
            ImGui::CloseCurrentPopup();
        }
        if (!ready) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2{110.0F, 0.0F})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (state_->open_project) ImGui::OpenPopup("Open Project###VICE.OpenProject");
    state_->open_project = false;
    if (ImGui::BeginPopupModal("Open Project###VICE.OpenProject", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Open Project");
        ImGui::Separator();
        ImGui::SetNextItemWidth(520.0F);
        ImGui::InputTextWithHint(
            "##OpenProjectPath",
            "Path to vice.viceproj",
            state_->open_project_path.data(),
            state_->open_project_path.size()
        );
        if (ImGui::Button("Open", ImVec2{110.0F, 0.0F}) && state_->open_project_path[0] != '\0') {
            queue_replacement(
                ReplacementKind::switch_project,
                std::filesystem::path{state_->open_project_path.data()}
            );
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2{110.0F, 0.0F})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (state_->open_world) ImGui::OpenPopup("Open World###VICE.OpenWorld");
    state_->open_world = false;
    if (ImGui::BeginPopupModal("Open World###VICE.OpenWorld", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Open World");
        ImGui::Separator();
        ImGui::SetNextItemWidth(520.0F);
        ImGui::InputTextWithHint(
            "##OpenWorldPath",
            "Path to .vworldc",
            state_->open_world_path.data(),
            state_->open_world_path.size()
        );
        if (ImGui::Button("Open", ImVec2{110.0F, 0.0F}) && state_->open_world_path[0] != '\0') {
            queue_replacement(
                ReplacementKind::open_world,
                std::filesystem::path{state_->open_world_path.data()}
            );
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2{110.0F, 0.0F})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (state_->save_world_as) ImGui::OpenPopup("Save World As###VICE.SaveWorldAs");
    state_->save_world_as = false;
    if (ImGui::BeginPopupModal("Save World As###VICE.SaveWorldAs", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save World As");
        ImGui::Separator();
        ImGui::SetNextItemWidth(520.0F);
        ImGui::InputTextWithHint(
            "##SaveWorldPath",
            "Path ending in .vworldc",
            state_->save_world_path.data(),
            state_->save_world_path.size()
        );
        if (ImGui::Button("Save", ImVec2{110.0F, 0.0F}) && state_->save_world_path[0] != '\0') {
            state_->save_world_as_request = std::filesystem::path{state_->save_world_path.data()};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2{110.0F, 0.0F})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (state_->open_unsaved_prompt) ImGui::OpenPopup("Unsaved Changes###VICE.UnsavedChanges");
    state_->open_unsaved_prompt = false;
    if (ImGui::BeginPopupModal("Unsaved Changes###VICE.UnsavedChanges", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(status.world_name.c_str());
        ImGui::TextDisabled("Save changes before continuing?");
        ImGui::Dummy(ImVec2{0.0F, 6.0F});
        if (ImGui::Button("Save", ImVec2{110.0F, 0.0F})) {
            if (state_->pending_replacement.has_value()) {
                state_->confirmed_replacement = ConfirmedReplacement{
                    .kind = *state_->pending_replacement,
                    .resolution = DirtyResolution::save,
                    .path = state_->pending_path,
                };
            }
            state_->pending_replacement.reset();
            state_->pending_path.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2{110.0F, 0.0F})) {
            if (state_->pending_replacement.has_value()) {
                state_->confirmed_replacement = ConfirmedReplacement{
                    .kind = *state_->pending_replacement,
                    .resolution = DirtyResolution::discard,
                    .path = state_->pending_path,
                };
            }
            state_->pending_replacement.reset();
            state_->pending_path.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2{110.0F, 0.0F})) {
            state_->pending_replacement.reset();
            state_->pending_path.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

std::optional<ProjectCreateRequest> ProjectWorkflowUi::consume_new_project_request() {
    if (state_ == nullptr) return std::nullopt;
    auto request = std::move(state_->new_project_request);
    state_->new_project_request.reset();
    return request;
}

std::optional<std::filesystem::path> ProjectWorkflowUi::consume_open_project_request() {
    if (state_ == nullptr) return std::nullopt;
    auto request = std::move(state_->open_project_request);
    state_->open_project_request.reset();
    return request;
}

bool ProjectWorkflowUi::consume_open_development_project_request() noexcept {
    if (state_ == nullptr) return false;
    const bool request = state_->open_development_project_request;
    state_->open_development_project_request = false;
    return request;
}

bool ProjectWorkflowUi::consume_new_world_request() noexcept {
    if (state_ == nullptr) return false;
    const bool request = state_->new_world_request;
    state_->new_world_request = false;
    return request;
}

std::optional<std::filesystem::path> ProjectWorkflowUi::consume_open_world_request() {
    if (state_ == nullptr) return std::nullopt;
    auto request = std::move(state_->open_world_request);
    state_->open_world_request.reset();
    return request;
}

bool ProjectWorkflowUi::consume_save_world_request() noexcept {
    if (state_ == nullptr) return false;
    const bool request = state_->save_world_request;
    state_->save_world_request = false;
    return request;
}

std::optional<std::filesystem::path> ProjectWorkflowUi::consume_save_world_as_request() {
    if (state_ == nullptr) return std::nullopt;
    auto request = std::move(state_->save_world_as_request);
    state_->save_world_as_request.reset();
    return request;
}

std::optional<ConfirmedReplacement> ProjectWorkflowUi::consume_confirmed_replacement() {
    if (state_ == nullptr) return std::nullopt;
    auto request = std::move(state_->confirmed_replacement);
    state_->confirmed_replacement.reset();
    return request;
}

} // namespace vice::editor
