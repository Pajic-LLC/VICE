#include <vice/editor/project_build.hpp>

#include <vice/project/project.hpp>
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

[[nodiscard]] std::filesystem::path target_executable_path(
    const std::filesystem::path& root,
    const std::string& target,
    const std::filesystem::path& runtime_host
) {
    return root / (target + runtime_host.extension().string());
}

[[nodiscard]] ProjectBuildResult cook_project(const ProjectSession& session) {
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
    result.game_target = session.manifest().game_target;
    result.cooked_startup_world = output_path;
    result.entity_count = parsed.snapshot->entities.size();
    result.region_count = parsed.snapshot->regions.size();
    return result;
}

} // namespace

ProjectBuildResult build_project(const ProjectSession& session) {
#if defined(VICE_EDITOR_RUNTIME_HOST_EXECUTABLE)
    return build_game_target(
        session,
        std::filesystem::path{VICE_EDITOR_RUNTIME_HOST_EXECUTABLE}
    );
#else
    return cook_project(session);
#endif
}

ProjectBuildResult build_game_target(
    const ProjectSession& session,
    const std::filesystem::path& runtime_host_executable
) {
    ProjectBuildResult result = cook_project(session);
    if (!result) return result;

    if (runtime_host_executable.empty()) {
        return fail_build("VICE runtime host is not available in this editor build.");
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(runtime_host_executable, error) || error) {
        return fail_build(
            error
                ? "Could not inspect VICE runtime host `" + path_text(runtime_host_executable) + "`: " + error.message()
                : "VICE runtime host does not exist: " + path_text(runtime_host_executable)
        );
    }

    const auto output_root = session.game_output_root();
    if (output_root.empty()) {
        return fail_build("Project game output root could not be resolved.");
    }

    auto staging_root = output_root;
    staging_root += ".staging";
    std::filesystem::remove_all(staging_root, error);
    error.clear();
    std::filesystem::create_directories(staging_root, error);
    if (error) {
        return fail_build(
            "Could not create game target staging directory `" + path_text(staging_root) + "`: " + error.message()
        );
    }

    const auto cleanup_staging = [&]() {
        std::error_code cleanup_error;
        std::filesystem::remove_all(staging_root, cleanup_error);
    };

    const auto staged_executable = target_executable_path(
        staging_root,
        session.manifest().game_target,
        runtime_host_executable
    );
    std::filesystem::copy_file(
        runtime_host_executable,
        staged_executable,
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    if (error) {
        const std::string message = error.message();
        cleanup_staging();
        return fail_build(
            "Could not stage game target executable `" + path_text(staged_executable) + "`: " + message
        );
    }

    const auto source_permissions = std::filesystem::status(runtime_host_executable, error).permissions();
    if (!error) {
        std::filesystem::permissions(
            staged_executable,
            source_permissions,
            std::filesystem::perm_options::replace,
            error
        );
    }
    if (error) {
        const std::string message = error.message();
        cleanup_staging();
        return fail_build(
            "Could not preserve game target executable permissions: " + message
        );
    }

    const auto staged_manifest = staging_root / "vice.runtime.viceproj";
    std::string manifest_diagnostics;
    if (!project::save_manifest(staged_manifest, session.manifest(), &manifest_diagnostics)) {
        cleanup_staging();
        return fail_build("Could not stage runtime project manifest: " + manifest_diagnostics);
    }

    auto cooked_relative = session.manifest().startup_world;
    cooked_relative.replace_extension(".vworldc");
    const auto staged_content_root = staging_root / "content";
    const auto staged_world = staged_content_root / session.manifest().id.to_string() / cooked_relative;
    std::filesystem::create_directories(staged_world.parent_path(), error);
    if (error) {
        const std::string message = error.message();
        cleanup_staging();
        return fail_build(
            "Could not create runtime content directory `" + path_text(staged_world.parent_path()) + "`: " + message
        );
    }

    std::filesystem::copy_file(
        result.cooked_startup_world,
        staged_world,
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    if (error) {
        const std::string message = error.message();
        cleanup_staging();
        return fail_build(
            "Could not stage cooked startup World `" + path_text(staged_world) + "`: " + message
        );
    }

    std::filesystem::remove_all(output_root, error);
    if (error) {
        const std::string message = error.message();
        cleanup_staging();
        return fail_build(
            "Could not replace previous game target output `" + path_text(output_root) + "`: " + message
        );
    }
    std::filesystem::rename(staging_root, output_root, error);
    if (error) {
        const std::string message = error.message();
        cleanup_staging();
        return fail_build(
            "Could not publish game target output `" + path_text(output_root) + "`: " + message
        );
    }

    result.game_output_root = output_root;
    result.game_executable = target_executable_path(
        output_root,
        session.manifest().game_target,
        runtime_host_executable
    );
    result.runtime_manifest = output_root / "vice.runtime.viceproj";
    result.runtime_content_root = output_root / "content";
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

platform::ProcessLaunchRequest game_run_request(const ProjectBuildResult& build) {
    platform::ProcessLaunchRequest request;
    request.executable = build.game_executable;
    request.working_directory = build.game_output_root;
    return request;
}

ProjectRunResult run_project(
    const ProjectSession& session,
    const std::filesystem::path& runtime_host_executable
) {
    ProjectRunResult result;
    result.build = build_game_target(session, runtime_host_executable);
    if (!result.build) {
        result.diagnostics = result.build.diagnostics;
        return result;
    }

    const auto launch = platform::launch_process(game_run_request(result.build));
    if (!launch) {
        result.diagnostics = launch.diagnostics;
        return result;
    }

    result.launched = true;
    return result;
}

} // namespace vice::editor
