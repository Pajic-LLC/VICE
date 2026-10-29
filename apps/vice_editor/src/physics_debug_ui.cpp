#include <vice/editor/physics_debug_ui.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

namespace vice::editor {
namespace {

constexpr const char* debug_window_name = "Debug###VICE.Debug";
constexpr const char* inspector_window_name = "Inspector###VICE.Inspector";

[[nodiscard]] const char* playback_name(const PreviewPlayback playback) noexcept {
    switch (playback) {
    case PreviewPlayback::stopped: return "Stopped";
    case PreviewPlayback::paused: return "Paused";
    case PreviewPlayback::running: return "Running";
    }
    return "Stopped";
}

[[nodiscard]] const char* fidelity_name(const world::RegionFidelity fidelity) noexcept {
    switch (fidelity) {
    case world::RegionFidelity::full: return "Full";
    case world::RegionFidelity::reduced: return "Reduced";
    case world::RegionFidelity::inactive: return "Inactive";
    }
    return "Unknown";
}

[[nodiscard]] const char* ground_state_name(const rigid_body::CharacterGroundState state) noexcept {
    switch (state) {
    case rigid_body::CharacterGroundState::grounded: return "Grounded";
    case rigid_body::CharacterGroundState::steep_ground: return "Steep ground";
    case rigid_body::CharacterGroundState::unsupported: return "Unsupported";
    case rigid_body::CharacterGroundState::airborne: return "Airborne";
    }
    return "Unknown";
}

void property(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value.c_str());
}

[[nodiscard]] std::string vec3(const math::Vec3 value) {
    char buffer[96]{};
    std::snprintf(buffer, sizeof(buffer), "%.3f, %.3f, %.3f", value.x, value.y, value.z);
    return buffer;
}

[[nodiscard]] std::string global_position(const world::GlobalPosition value) {
    char buffer[128]{};
    std::snprintf(buffer, sizeof(buffer), "%.3f, %.3f, %.3f", value.x, value.y, value.z);
    return buffer;
}

[[nodiscard]] std::string local_position(const world::LocalPosition value) {
    char buffer[96]{};
    std::snprintf(buffer, sizeof(buffer), "%.3f, %.3f, %.3f", value.x, value.y, value.z);
    return buffer;
}

void layer_button(const char* label, bool& value, const float width) {
    if (value) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.12F, 0.39F, 0.61F, 1.00F});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.16F, 0.48F, 0.72F, 1.00F});
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.96F, 0.98F, 1.00F, 1.00F});
    }
    if (ImGui::Button(label, ImVec2{width, 30.0F})) value = !value;
    if (value) ImGui::PopStyleColor(3);
}

} // namespace

struct PhysicsDebugUi::State {
    PhysicsDebugLayers layers{};
    bool start_requested = false;
    bool reset_requested = false;
    bool stop_requested = false;
    bool step_requested = false;
    bool play_pause_requested = false;
};

PhysicsDebugUi::PhysicsDebugUi() : state_{std::make_unique<State>()} {}
PhysicsDebugUi::~PhysicsDebugUi() = default;

void PhysicsDebugUi::draw(const PreviewSnapshot& preview, const bool authored_world_dirty) {
    if (state_ == nullptr) return;

    ImGui::SetNextWindowSize(ImVec2{390.0F, 420.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(debug_window_name)) {
        ImGui::End();
        return;
    }

    const bool active = preview.playback != PreviewPlayback::stopped;
    ImGui::TextDisabled("PLAY IN EDITOR");
    ImGui::SameLine();
    const ImVec4 status_color = preview.playback == PreviewPlayback::running
        ? ImVec4{0.32F, 0.82F, 0.56F, 1.0F}
        : (active ? ImVec4{0.94F, 0.70F, 0.28F, 1.0F} : ImVec4{0.55F, 0.59F, 0.65F, 1.0F});
    ImGui::TextColored(status_color, "%s", playback_name(preview.playback));

    if (!active) {
        if (ImGui::Button("Play", ImVec2{-1.0F, 34.0F})) request_play();
    } else {
        if (ImGui::Button(
                preview.playback == PreviewPlayback::running ? "Pause" : "Resume",
                ImVec2{82.0F, 30.0F}
            )) {
            state_->play_pause_requested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Step", ImVec2{72.0F, 30.0F})) state_->step_requested = true;
        ImGui::SameLine();
        if (ImGui::Button("Reset", ImVec2{72.0F, 30.0F})) state_->reset_requested = true;
        ImGui::SameLine();
        if (ImGui::Button("Stop", ImVec2{72.0F, 30.0F})) state_->stop_requested = true;
        if (authored_world_dirty) {
            ImGui::TextColored(
                ImVec4{0.94F, 0.70F, 0.28F, 1.0F},
                "Authored World changed since play session started"
            );
        }
        ImGui::TextDisabled(
            "Step %llu  ·  %zu bodies  ·  %zu contacts",
            static_cast<unsigned long long>(preview.step_index),
            preview.bodies.size(),
            preview.contacts.size()
        );
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float available = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float button_width = std::max(1.0F, (available - gap * 2.0F) / 3.0F);
    layer_button("Physics", state_->layers.physics, button_width);
    ImGui::SameLine();
    layer_button("Regions", state_->layers.regions, button_width);
    ImGui::SameLine();
    layer_button("Contacts", state_->layers.contacts, button_width);

    if (ImGui::CollapsingHeader("Advanced")) {
        ImGui::Checkbox("Velocity vectors", &state_->layers.velocities);
        ImGui::Checkbox("Character state", &state_->layers.characters);
        ImGui::Checkbox("Region handoffs", &state_->layers.handoffs);
    }

    if (active && state_->layers.regions) {
        ImGui::Spacing();
        ImGui::TextDisabled("REGIONS");
        std::size_t full = 0;
        std::size_t reduced = 0;
        std::size_t inactive = 0;
        for (const auto& region : preview.regions) {
            switch (region.region.fidelity) {
            case world::RegionFidelity::full: ++full; break;
            case world::RegionFidelity::reduced: ++reduced; break;
            case world::RegionFidelity::inactive: ++inactive; break;
            }
        }
        ImGui::TextDisabled("%zu full  ·  %zu reduced  ·  %zu inactive", full, reduced, inactive);

        if (ImGui::BeginTable(
                "##DebugRegions",
                3,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_RowBg
            )) {
            ImGui::TableSetupColumn("Region", ImGuiTableColumnFlags_WidthFixed, 68.0F);
            ImGui::TableSetupColumn("Fidelity", ImGuiTableColumnFlags_WidthStretch, 0.8F);
            ImGui::TableSetupColumn("Streaming cell", ImGuiTableColumnFlags_WidthStretch, 1.2F);
            for (const auto& region : preview.regions) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%llu", static_cast<unsigned long long>(region.region.id.value));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(fidelity_name(region.region.fidelity));
                ImGui::TableSetColumnIndex(2);
                ImGui::Text(
                    "%d, %d, %d",
                    region.region.streaming_cell.x,
                    region.region.streaming_cell.y,
                    region.region.streaming_cell.z
                );
            }
            ImGui::EndTable();
        }
    }

    ImGui::End();
}

void PhysicsDebugUi::draw_inspector_live(
    const PreviewSnapshot& preview,
    const SelectionModel& selection
) {
    if (preview.playback == PreviewPlayback::stopped) return;
    const auto selected = selection.selected();
    if (!selected.has_value()) return;

    const auto body = std::find_if(
        preview.bodies.begin(), preview.bodies.end(),
        [&](const PreviewBodySnapshot& value) { return value.owner == *selected; }
    );
    const auto character = std::find_if(
        preview.characters.begin(), preview.characters.end(),
        [&](const PreviewCharacterSnapshot& value) { return value.owner == *selected; }
    );
    if (body == preview.bodies.end() && character == preview.characters.end()) return;

    ImGuiWindow* inspector_window = ImGui::FindWindowByName(inspector_window_name);
    if (inspector_window == nullptr || !inspector_window->Active) return;

    const bool visible = ImGui::Begin(inspector_window_name);
    if (visible && ImGui::CollapsingHeader("Play In Editor", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Read only");
        if (ImGui::BeginTable(
                "##LivePreview",
                2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings
            )) {
            if (body != preview.bodies.end()) {
                property("Global position", global_position(body->global_position));
                property("Region local", local_position(body->local_position));
                property("Region", std::to_string(body->region.value));
                property("Velocity", vec3(body->body.linear_velocity));
                property("Angular velocity", vec3(body->body.angular_velocity));
                property("State", body->body.active ? "Active" : "Sleeping");
                property("Material", std::to_string(body->body.material.value));
                const auto contact = std::find_if(
                    preview.contacts.begin(), preview.contacts.end(),
                    [&](const PreviewContactSnapshot& value) {
                        return value.contact.first == body->body.participant ||
                            value.contact.second == body->body.participant;
                    }
                );
                if (contact != preview.contacts.end() && !contact->contact.material_interaction.empty()) {
                    property("Interaction", contact->contact.material_interaction);
                }
            } else {
                property("Global position", global_position(character->global_position));
                property("Region local", local_position(character->local_position));
                property("Region", std::to_string(character->region.value));
                property("Velocity", vec3(character->character.velocity));
                property("Ground", ground_state_name(character->character.ground_state));
                property(
                    "Ground material",
                    character->character.ground_material.has_value()
                        ? std::to_string(character->character.ground_material->value)
                        : "None"
                );
            }
            const auto vehicle=std::find_if(preview.vehicles.begin(),preview.vehicles.end(),[&](const auto& v){return v.owner==*selected;});
            if(vehicle!=preview.vehicles.end()) {
                property("Vehicle speed (m/s)",std::to_string(vehicle->vehicle.forward_speed));
                property("Travelled (m)",std::to_string(vehicle->vehicle.state.distance));
                for(std::size_t i=0;i<4;++i) {
                    const auto& w=vehicle->vehicle.wheels[i];
                    property(("Wheel "+std::to_string(i)+" support").c_str(),w.grounded?w.supporting_entity.to_string():"Airborne");
                    property(("Wheel "+std::to_string(i)+" load (N)").c_str(),std::to_string(w.normal_force));
                    property(("Wheel "+std::to_string(i)+" material").c_str(),w.material.to_string());
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void PhysicsDebugUi::request_play() noexcept {
    if (state_ == nullptr) return;
    state_->start_requested = true;
    state_->play_pause_requested = true;
}

PhysicsDebugLayers PhysicsDebugUi::layers() const noexcept {
    return state_ != nullptr ? state_->layers : PhysicsDebugLayers{};
}

bool PhysicsDebugUi::consume_start_request() noexcept {
    if (state_ == nullptr) return false;
    return std::exchange(state_->start_requested, false);
}

bool PhysicsDebugUi::consume_reset_request() noexcept {
    if (state_ == nullptr) return false;
    return std::exchange(state_->reset_requested, false);
}

bool PhysicsDebugUi::consume_stop_request() noexcept {
    if (state_ == nullptr) return false;
    return std::exchange(state_->stop_requested, false);
}

bool PhysicsDebugUi::consume_step_request() noexcept {
    if (state_ == nullptr) return false;
    return std::exchange(state_->step_requested, false);
}

bool PhysicsDebugUi::consume_play_pause_request() noexcept {
    if (state_ == nullptr) return false;
    return std::exchange(state_->play_pause_requested, false);
}

} // namespace vice::editor
