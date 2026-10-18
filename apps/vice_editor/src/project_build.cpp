#include <vice/editor/project_build.hpp>

#include <vice/platform/dynamic_library.hpp>
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

[[nodiscard]] std::filesystem::path game_module_source(const ProjectSession& session) {
    return (session.manifest_path().parent_path() / "game" / "game.cpp").lexically_normal();
}

[[nodiscard]] std::filesystem::path game_module_filename(const std::string& target) {
    return std::filesystem::path{
        target + std::string{platform::dynamic_library_extension()}
    };
}

[[nodiscard]] std::string process_failure(
    const std::string& label,
    const platform::ProcessRunResult& process
) {
    std::string message = label;
    if (!process.diagnostics.empty()) {
        message += ": " + process.diagnostics;
    }
    if (!process.output.empty()) {
        message += "\n" + process.output;
    }
    return message;
}

struct GameModuleBuild {
    bool source_present = false;
    bool success = true;
    std::filesystem::path source;
    std::filesystem::path output;
    std::string diagnostics;
};

[[nodiscard]] GameModuleBuild build_project_game_module(const ProjectSession& session) {
    GameModuleBuild result;
    result.source = game_module_source(session);

    std::error_code error;
    const bool exists = std::filesystem::exists(result.source, error);
    if (error) {
        result.success = false;
        result.diagnostics = "Could not inspect project game source `" + path_text(result.source) +
            "`: " + error.message();
        return result;
    }
    if (!exists) {
        return result;
    }
    if (!std::filesystem::is_regular_file(result.source, error) || error) {
        result.success = false;
        result.diagnostics = error
            ? "Could not inspect project game source `" + path_text(result.source) + "`: " + error.message()
            : "Project game source is not a regular file: " + path_text(result.source);
        return result;
    }
    result.source_present = true;

#if !defined(VICE_EDITOR_CMAKE_EXECUTABLE) || \
    !defined(VICE_EDITOR_GAME_MODULE_CMAKE_SOURCE) || \
    !defined(VICE_EDITOR_GAME_MODULE_INCLUDE)
    result.success = false;
    result.diagnostics = "This VICE Editor build cannot compile project game modules.";
    return result;
#else
    const auto project_root = session.manifest_path().parent_path();
    const auto build_root = (
        project_root / ".vice" / "build" / "game-module" / session.manifest().game_target
    ).lexically_normal();
    const auto cmake_build = build_root / "cmake";
    const auto module_output = build_root / "output";
    result.output = module_output / game_module_filename(session.manifest().game_target);

    std::filesystem::remove_all(module_output, error);
    error.clear();
    std::filesystem::create_directories(module_output, error);
    if (error) {
        result.success = false;
        result.diagnostics = "Could not create game module output directory `" +
            path_text(module_output) + "`: " + error.message();
        return result;
    }

    platform::ProcessLaunchRequest configure;
    configure.executable = std::filesystem::path{VICE_EDITOR_CMAKE_EXECUTABLE};
    configure.arguments = {
        "-S",
        path_text(std::filesystem::path{VICE_EDITOR_GAME_MODULE_CMAKE_SOURCE}),
        "-B",
        path_text(cmake_build),
        "-DVICE_GAME_SOURCE=" + path_text(result.source),
        "-DVICE_GAME_TARGET=" + session.manifest().game_target,
        "-DVICE_GAME_MODULE_INCLUDE=" + path_text(std::filesystem::path{VICE_EDITOR_GAME_MODULE_INCLUDE}),
        "-DVICE_GAME_OUTPUT=" + path_text(module_output),
        "-DCMAKE_BUILD_TYPE=Debug",
    };
    configure.working_directory = project_root;
    const auto configured = platform::run_process(configure);
    if (!configured) {
        result.success = false;
        result.diagnostics = process_failure("Could not configure project game module build", configured);
        return result;
    }

    platform::ProcessLaunchRequest build;
    build.executable = std::filesystem::path{VICE_EDITOR_CMAKE_EXECUTABLE};
    build.arguments = {
        "--build",
        path_text(cmake_build),
        "--config",
        "Debug",
        "--target",
        "vice_project_game",
    };
    build.working_directory = project_root;
    const auto built = platform::run_process(build);
    if (!built) {
        result.success = false;
        result.diagnostics = process_failure("Could not compile project game module", built);
        return result;
    }

    if (!std::filesystem::is_regular_file(result.output, error) || error) {
        result.success = false;
        result.diagnostics = error
            ? "Could not inspect compiled game module `" + path_text(result.output) + "`: " + error.message()
            : "Project game module build did not produce `" + path_text(result.output) + "`.";
        return result;
    }

    return result;
#endif
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

    const auto module_build = build_project_game_module(session);
    if (!module_build.success) {
        return fail_build(module_build.diagnostics);
    }
    if (module_build.source_present) {
        result.game_module_source = module_build.source;
    }

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

    if (module_build.source_present) {
        const auto staged_module = staging_root / "modules" /
            game_module_filename(session.manifest().game_target);
        std::filesystem::create_directories(staged_module.parent_path(), error);
        if (error) {
            const std::string message = error.message();
            cleanup_staging();
            return fail_build(
                "Could not create runtime module directory `" + path_text(staged_module.parent_path()) +
                "`: " + message
            );
        }
        std::filesystem::copy_file(
            module_build.output,
            staged_module,
            std::filesystem::copy_options::overwrite_existing,
            error
        );
        if (error) {
            const std::string message = error.message();
            cleanup_staging();
            return fail_build(
                "Could not stage project game module `" + path_text(staged_module) + "`: " + message
            );
        }
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
    if (module_build.source_present) {
        result.game_module = output_root / "modules" /
            game_module_filename(session.manifest().game_target);
    }
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
