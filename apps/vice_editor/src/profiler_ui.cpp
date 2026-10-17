#include <vice/editor/profiler_ui.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace vice::editor {
namespace {

constexpr const char* profiler_window_name = "Profiler###VICE.Profiler";

[[nodiscard]] ImVec4 frame_color(const double milliseconds) noexcept {
    if (milliseconds <= 16.7) return {0.45F, 0.82F, 0.60F, 1.0F};
    if (milliseconds <= 33.4) return {0.96F, 0.72F, 0.30F, 1.0F};
    return {0.96F, 0.39F, 0.37F, 1.0F};
}

void metric_cell(const char* label, const std::string& value, const ImVec4* color = nullptr) {
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    if (color != nullptr) ImGui::TextColored(*color, "%s", value.c_str());
    else ImGui::TextUnformatted(value.c_str());
}

void property_row(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value.c_str());
}

[[nodiscard]] std::string one_decimal(const double value) {
    char buffer[48]{};
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return buffer;
}

[[nodiscard]] std::string two_decimals(const double value) {
    char buffer[48]{};
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

} // namespace

void ProfilerUi::draw(const ProfilerSnapshot& snapshot, const ProfilerHistory& history) {
    ImGui::SetNextWindowSize(ImVec2{760.0F, 410.0F}, ImGuiCond_FirstUseEver);
    const bool visible = ImGui::Begin(profiler_window_name);
    if (!visible) {
        ImGui::End();
        return;
    }

    const double frame_ms = snapshot.frame_seconds * 1000.0;
    const ImVec4 performance_color = frame_color(frame_ms);

    if (ImGui::BeginTable(
            "##ProfilerHeadline",
            4,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings
        )) {
        metric_cell("FPS", one_decimal(snapshot.frames_per_second), &performance_color);
        metric_cell("Frame", two_decimals(frame_ms) + " ms", &performance_color);
        metric_cell("World", std::to_string(snapshot.world.entities) + " entities");
        metric_cell(
            "Jobs",
            std::to_string(snapshot.jobs.running_tasks) + " running · " +
                std::to_string(snapshot.jobs.outstanding_tasks) + " outstanding"
        );
        ImGui::EndTable();
    }

    ImGui::Spacing();
    const auto samples = history.frame_milliseconds();
    if (!samples.empty()) {
        const float maximum = std::max(20.0F, *std::max_element(samples.begin(), samples.end()) * 1.15F);
        ImGui::PlotLines(
            "##ProfilerFrameHistory",
            samples.data(),
            static_cast<int>(samples.size()),
            0,
            nullptr,
            0.0F,
            maximum,
            ImVec2{-1.0F, 82.0F}
        );
    } else {
        ImGui::Dummy(ImVec2{1.0F, 82.0F});
    }

    if (ImGui::BeginTable(
            "##ProfilerSections",
            2,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings
        )) {
        ImGui::TableNextColumn();
        if (ImGui::CollapsingHeader("Jobs", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("##ProfilerJobs", 2, ImGuiTableFlags_SizingStretchProp)) {
                property_row("Workers", std::to_string(snapshot.jobs.worker_count));
                property_row("Running", std::to_string(snapshot.jobs.running_tasks));
                property_row(
                    "Queued",
                    std::to_string(snapshot.jobs.queued_worker_tasks + snapshot.jobs.queued_main_thread_tasks)
                );
                property_row("Outstanding", std::to_string(snapshot.jobs.outstanding_tasks));
                property_row("Submitted", std::to_string(snapshot.jobs.total_submitted));
                property_row("Completed", std::to_string(snapshot.jobs.total_completed));
                property_row("Failed", std::to_string(snapshot.jobs.total_failed));
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("World", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("##ProfilerWorld", 2, ImGuiTableFlags_SizingStretchProp)) {
                property_row("Entities", std::to_string(snapshot.world.entities));
                property_row("Resources", std::to_string(snapshot.world.resources));
                property_row("Relations", std::to_string(snapshot.world.relations));
                property_row("Regions", std::to_string(snapshot.world.regions));
                property_row("Full", std::to_string(snapshot.world.full_regions));
                property_row("Reduced", std::to_string(snapshot.world.reduced_regions));
                property_row("Inactive", std::to_string(snapshot.world.inactive_regions));
                ImGui::EndTable();
            }
        }

        ImGui::TableNextColumn();
        if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!snapshot.simulation.has_value()) {
                ImGui::TextDisabled("Inactive");
            } else if (ImGui::BeginTable("##ProfilerSimulation", 2, ImGuiTableFlags_SizingStretchProp)) {
                const auto& simulation = *snapshot.simulation;
                property_row("Fixed delta", two_decimals(simulation.fixed_delta_seconds * 1000.0) + " ms");
                property_row("Last step", two_decimals(simulation.last_step_wall_seconds * 1000.0) + " ms");
                property_row("Recent average", two_decimals(simulation.recent_step_average_seconds * 1000.0) + " ms");
                property_row("Step", std::to_string(simulation.step_index));
                property_row("Domains", std::to_string(simulation.registered_domains));
                property_row("Full", std::to_string(simulation.full_domains));
                property_row("Reduced", std::to_string(simulation.reduced_domains));
                property_row("Off", std::to_string(simulation.off_domains));
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("##ProfilerRender", 2, ImGuiTableFlags_SizingStretchProp)) {
                property_row("Submissions", std::to_string(snapshot.render.submitted_command_lists));
                property_row("Completed", std::to_string(snapshot.render.completed_submission));
                property_row("Buffers created", std::to_string(snapshot.render.created_buffers));
                property_row("Textures created", std::to_string(snapshot.render.created_textures));
                property_row("Samplers created", std::to_string(snapshot.render.created_samplers));
                property_row("Shaders created", std::to_string(snapshot.render.created_shader_modules));
                property_row("Pipelines created", std::to_string(snapshot.render.created_graphics_pipelines));
                ImGui::EndTable();
            }
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace vice::editor
