#include <vice/editor/project_build.hpp>

#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace vice::editor {
namespace {

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(value.data()),
        value.size()
    };
}

[[nodiscard]] std::optional<std::string> read_text(
    const std::filesystem::path& path,
    std::string& diagnostics
) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        diagnostics = "Could not read startup World `" + path_text(path) + "`.";
        return std::nullopt;
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.eof() && input.fail()) {
        diagnostics = "Could not finish reading startup World `" + path_text(path) + "`.";
        return std::nullopt;
    }
    return contents.str();
}

[[nodiscard]] ProjectBuildResult fail_build(std::string diagnostics) {
    ProjectBuildResult result;
    result.diagnostics = std::move(diagnostics);
    return result;
}

} // namespace

ProjectBuildResult build_project(const ProjectSession& session) {
    const auto& source_path = session.startup_world_source();
    const auto& output_path = session.cooked_startup_world();
    if (source_path.empty()) {
        return fail_build("Project startup World source path is empty.");
    }
    if (output_path.empty()) {
        return fail_build("Project cooked startup World path is empty.");
    }

    std::string diagnostics;
    const auto source = read_text(source_path, diagnostics);
    if (!source.has_value()) {
        return fail_build(std::move(diagnostics));
    }

    const auto parsed = tools::world_cooker::parse_source_manifest(*source);
    if (!parsed.snapshot.has_value()) {
        return fail_build(
            "Could not cook startup World `" + path_text(source_path) + "`: " + parsed.diagnostics
        );
    }

    std::error_code error;
    const auto parent = output_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return fail_build(
                "Could not create project cooked asset directory `" + path_text(parent) + "`: " +
                error.message()
            );
        }
    }

    std::string save_diagnostics;
    if (!world::save_world_snapshot(output_path, *parsed.snapshot, &save_diagnostics)) {
        return fail_build(
            "Could not write cooked startup World `" + path_text(output_path) + "`: " + save_diagnostics
        );
    }

    ProjectBuildResult result;
    result.success = true;
    result.cooked_startup_world = output_path;
    result.entity_count = parsed.snapshot->entities.size();
    result.region_count = parsed.snapshot->regions.size();
    return result;
}

platform::ProcessLaunchRequest project_run_request(
    const ProjectSession& session,
    const std::filesystem::path& player_executable
) {
    platform::ProcessLaunchRequest request;
    request.executable = player_executable;
    request.arguments.push_back(path_text(session.manifest_path()));
    request.arguments.push_back(path_text(session.cooked_root().parent_path()));
    request.working_directory = session.manifest_path().parent_path();
    return request;
}

ProjectRunResult run_project(
    const ProjectSession& session,
    const std::filesystem::path& player_executable
) {
    ProjectRunResult result;
    result.build = build_project(session);
    if (!result.build) {
        result.diagnostics = result.build.diagnostics;
        return result;
    }

    const auto launch = platform::launch_process(project_run_request(session, player_executable));
    if (!launch) {
        result.diagnostics = launch.diagnostics;
        return result;
    }

    result.launched = true;
    return result;
}

} // namespace vice::editor
