#include <vice/editor/resource_browser_ui.hpp>

#include <imgui.h>

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

std::optional<world::ResourceId> ResourceBrowserUi::consume_add_mesh_request() noexcept {
    if (state_ == nullptr) return std::nullopt;
    const auto request = state_->add_mesh_request;
    state_->add_mesh_request.reset();
    return request;
}

} // namespace vice::editor
