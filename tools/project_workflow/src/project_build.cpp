#include <vice/tools/project_workflow.hpp>

#include <vice/platform/dynamic_library.hpp>
#include <vice/platform/process.hpp>
#include <vice/project/project.hpp>
#include <vice/runtime/dade.hpp>
#include <vice/runtime/package_metadata.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace vice::tools {
namespace {

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(value.data()),
        value.size()
    };
}

[[nodiscard]] ProjectBuildResult fail_build(
    ProjectBuildResult&& result,
    std::string diagnostics
) {
    result.success = false;
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

[[nodiscard]] std::filesystem::path game_module_source(
    const project::Workspace& workspace
) {
    return (workspace.manifest_path.parent_path() / "game" / "game.cpp").lexically_normal();
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

[[nodiscard]] ProjectGameModuleBuildResult build_game_module_impl(
    const project::Workspace& workspace,
    const ProjectBuildToolchain& toolchain
) {
    ProjectGameModuleBuildResult result;
    result.source = game_module_source(workspace);

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

    if (!toolchain.game_module_available()) {
        result.success = false;
        result.diagnostics = "VICE project build toolchain cannot compile project game modules.";
        return result;
    }

    const auto project_root = workspace.manifest_path.parent_path();
    const auto build_root = (
        project_root / ".vice" / "build" / "game-module" / workspace.manifest.game_target
    ).lexically_normal();
    const auto cmake_build = build_root / "cmake";
    const auto module_output = build_root / "output";
    result.output = module_output / game_module_filename(workspace.manifest.game_target);

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
    configure.executable = toolchain.cmake_executable;
    configure.arguments = {
        "-S",
        path_text(toolchain.game_module_cmake_source),
        "-B",
        path_text(cmake_build),
        "-DVICE_GAME_SOURCE=" + path_text(result.source),
        "-DVICE_GAME_TARGET=" + workspace.manifest.game_target,
        "-DVICE_GAME_MODULE_INCLUDE=" + path_text(toolchain.game_module_include),
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
    build.executable = toolchain.cmake_executable;
    build.arguments = {
        "--build",
        path_text(cmake_build),
        "--clean-first",
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
}

[[nodiscard]] bool stage_cooked_assets(
    const project::Workspace& workspace,
    const std::filesystem::path& staged_project_content_root,
    const std::string_view source_extension,
    const std::string_view cooked_extension,
    const std::string_view asset_label,
    std::string& diagnostics
) {
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator{
        workspace.asset_root,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        diagnostics = "Could not enumerate project " + std::string{asset_label} +
            " for staging: " + error.message();
        return false;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            diagnostics = "Could not enumerate project " + std::string{asset_label} +
                " for staging: " + error.message();
            return false;
        }
        if (!iterator->is_regular_file() || iterator->path().extension() != source_extension) continue;

        auto relative = std::filesystem::relative(iterator->path(), workspace.asset_root, error);
        if (error) {
            diagnostics = "Could not resolve project " + std::string{asset_label} +
                " path for staging: " + error.message();
            return false;
        }
        relative.replace_extension(cooked_extension);
        const auto source = (workspace.cooked_root / relative).lexically_normal();
        const auto destination = (staged_project_content_root / relative).lexically_normal();
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) {
            diagnostics = "Could not create staged " + std::string{asset_label} + " directory `" +
                path_text(destination.parent_path()) + "`: " + error.message();
            return false;
        }
        std::filesystem::copy_file(
            source,
            destination,
            std::filesystem::copy_options::overwrite_existing,
            error
        );
        if (error) {
            diagnostics = "Could not stage cooked " + std::string{asset_label} + " `" +
                path_text(source) + "`: " + error.message();
            return false;
        }
    }

    diagnostics.clear();
    return true;
}

[[nodiscard]] bool stage_project_render_materials(
    const project::Workspace& workspace,
    const std::filesystem::path& staged_project_content_root,
    std::string& diagnostics
) {
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator{
        workspace.asset_root,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        diagnostics = "Could not enumerate project render materials for staging: " + error.message();
        return false;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            diagnostics = "Could not enumerate project render materials for staging: " + error.message();
            return false;
        }
        if (!iterator->is_regular_file() || iterator->path().extension() != ".vmat") continue;

        auto relative = std::filesystem::relative(iterator->path(), workspace.asset_root, error);
        if (error) {
            diagnostics = "Could not resolve project render material path for staging: " + error.message();
            return false;
        }
        const auto material_stem = iterator->path().stem().string();
        auto cooked_relative = relative;
        cooked_relative.replace_extension(".vmatc");
        const auto cooked_material = (workspace.cooked_root / cooked_relative).lexically_normal();
        const auto staged_material = (staged_project_content_root / cooked_relative).lexically_normal();
        std::filesystem::create_directories(staged_material.parent_path(), error);
        if (error) {
            diagnostics = "Could not create staged render material directory: " + error.message();
            return false;
        }
        std::filesystem::copy_file(
            cooked_material,
            staged_material,
            std::filesystem::copy_options::overwrite_existing,
            error
        );
        if (error) {
            diagnostics = "Could not stage cooked render material `" + path_text(cooked_material) +
                "`: " + error.message();
            return false;
        }

        const auto cooked_shader_root = cooked_material.parent_path() / (material_stem + ".shaders");
        const auto staged_shader_root = staged_material.parent_path() / (material_stem + ".shaders");
        if (!std::filesystem::is_directory(cooked_shader_root, error) || error) {
            diagnostics = error
                ? "Could not inspect cooked render material shaders: " + error.message()
                : "Cooked render material shader directory is missing: " + path_text(cooked_shader_root);
            return false;
        }
        std::filesystem::create_directories(staged_shader_root, error);
        if (error) {
            diagnostics = "Could not create staged shader directory: " + error.message();
            return false;
        }
        std::filesystem::copy(
            cooked_shader_root,
            staged_shader_root,
            std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::overwrite_existing,
            error
        );
        if (error) {
            diagnostics = "Could not stage cooked render material shaders `" +
                path_text(cooked_shader_root) + "`: " + error.message();
            return false;
        }
    }

    diagnostics.clear();
    return true;
}

} // namespace

ProjectGameModuleBuildResult build_project_game_module(const project::Workspace& workspace, const ProjectBuildToolchain& toolchain) {
    return build_game_module_impl(workspace, toolchain);
}
ProjectGameModuleBuildResult build_project_game_module(const project::Workspace& workspace) {
    return build_project_game_module(workspace, configured_project_build_toolchain());
}

ProjectBuildToolchain configured_project_build_toolchain() {
    ProjectBuildToolchain toolchain;
#if defined(VICE_PROJECT_WORKFLOW_CMAKE_EXECUTABLE)
    toolchain.cmake_executable = std::filesystem::path{VICE_PROJECT_WORKFLOW_CMAKE_EXECUTABLE};
#endif
#if defined(VICE_PROJECT_WORKFLOW_GAME_MODULE_CMAKE_SOURCE)
    toolchain.game_module_cmake_source =
        std::filesystem::path{VICE_PROJECT_WORKFLOW_GAME_MODULE_CMAKE_SOURCE};
#endif
#if defined(VICE_PROJECT_WORKFLOW_GAME_MODULE_INCLUDE)
    toolchain.game_module_include =
        std::filesystem::path{VICE_PROJECT_WORKFLOW_GAME_MODULE_INCLUDE};
#endif
    return toolchain;
}

ProjectBuildResult build_project(
    const project::Workspace& workspace,
    const std::filesystem::path& runtime_host_executable,
    const ProjectBuildToolchain& toolchain
) {
    ProjectBuildResult result;
    result.project_id = workspace.manifest.id.to_string();
    result.game_target = workspace.manifest.game_target;
    result.assets = cook_project_assets(workspace);
    if (!result.assets) {
        return fail_build(std::move(result), result.assets.diagnostics);
    }

    const auto module_build = build_project_game_module(workspace, toolchain);
    if (!module_build.success) {
        return fail_build(std::move(result), module_build.diagnostics);
    }
    if (module_build.source_present) {
        result.game_module_source = module_build.source;
    }

    if (runtime_host_executable.empty()) {
        return fail_build(std::move(result), "VICE runtime host is not available.");
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(runtime_host_executable, error) || error) {
        return fail_build(
            std::move(result),
            error
                ? "Could not inspect VICE runtime host `" + path_text(runtime_host_executable) + "`: " +
                    error.message()
                : "VICE runtime host does not exist: " + path_text(runtime_host_executable)
        );
    }

    const auto dade_requirement = runtime::load_dade_requirement(workspace.manifest_path);
    if (!dade_requirement) {
        return fail_build(
            std::move(result),
            "Could not read DADE ownership settings: " + dade_requirement.diagnostics
        );
    }

    const auto& output_root = workspace.game_output_root;
    if (output_root.empty()) {
        return fail_build(std::move(result), "Project game output root could not be resolved.");
    }

    auto staging_root = output_root;
    staging_root += ".staging";
    std::filesystem::remove_all(staging_root, error);
    error.clear();
    std::filesystem::create_directories(staging_root, error);
    if (error) {
        return fail_build(
            std::move(result),
            "Could not create game target staging directory `" + path_text(staging_root) + "`: " +
                error.message()
        );
    }

    const auto cleanup_staging = [&]() {
        std::error_code cleanup_error;
        std::filesystem::remove_all(staging_root, cleanup_error);
    };

    const auto staged_executable = target_executable_path(
        staging_root,
        workspace.manifest.game_target,
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
            std::move(result),
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
            std::move(result),
            "Could not preserve game target executable permissions: " + message
        );
    }

    std::string package_diagnostics;
    if (!runtime::stamp_staged_package_metadata(
            staged_executable,
            dade_requirement.required,
            dade_requirement.asset_id,
            &package_diagnostics
        )) {
        cleanup_staging();
        return fail_build(
            std::move(result),
            "Could not protect staged package metadata: " + package_diagnostics
        );
    }

    const auto staged_manifest = staging_root / "vice.runtime.viceproj";
    std::string manifest_diagnostics;
    if (!project::save_manifest(staged_manifest, workspace.manifest, &manifest_diagnostics)) {
        cleanup_staging();
        return fail_build(
            std::move(result),
            "Could not stage runtime project manifest: " + manifest_diagnostics
        );
    }

    auto cooked_relative = workspace.manifest.startup_world;
    cooked_relative.replace_extension(".vworldc");
    const auto staged_content_root = staging_root / "content";
    const auto staged_project_content_root = staged_content_root / workspace.manifest.id.to_string();
    const auto staged_world = staged_project_content_root / cooked_relative;
#ifdef VICE_BUILTIN_SHADER_ROOT
    // Package renderer kernels with the game. A staged runtime must not open the developer checkout.
    const std::filesystem::path kernel_root{VICE_BUILTIN_SHADER_ROOT};
    if(std::filesystem::is_directory(kernel_root,error)) {
        for(const auto &entry:std::filesystem::directory_iterator(kernel_root,error)) {
            if(!entry.is_directory() || !entry.path().filename().string().starts_with("vice_runtime_")) continue;
            const auto destination=staged_project_content_root/"runtime-shaders"/entry.path().filename();
            std::filesystem::create_directories(destination,error);
            if(error) break;
            for(const auto name:{"shader.vice.json","shader.dxil","shader.metallib"}) {
                const auto source=entry.path()/name;
                if(!std::filesystem::is_regular_file(source,error)) {error.clear();continue;}
                std::filesystem::copy_file(source,destination/name,std::filesystem::copy_options::overwrite_existing,error);
                if(error) break;
            }
            if(error) break;
        }
        if(error) {cleanup_staging();return fail_build(std::move(result),"Could not package renderer kernels: "+error.message());}
    }
#endif

    std::filesystem::create_directories(staged_world.parent_path(), error);
    if (error) {
        const std::string message = error.message();
        cleanup_staging();
        return fail_build(
            std::move(result),
            "Could not create runtime content directory `" + path_text(staged_world.parent_path()) +
                "`: " + message
        );
    }

    std::filesystem::copy_file(
        result.assets.cooked_startup_world,
        staged_world,
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    if (error) {
        const std::string message = error.message();
        cleanup_staging();
        return fail_build(
            std::move(result),
            "Could not stage cooked startup World `" + path_text(staged_world) + "`: " + message
        );
    }

    std::string stage_diagnostics;
    if (!stage_cooked_assets(
            workspace,
            staged_project_content_root,
            ".vmesh",
            ".vmeshc",
            "meshes",
            stage_diagnostics
        )) {
        cleanup_staging();
        return fail_build(std::move(result), std::move(stage_diagnostics));
    }
    if (!stage_cooked_assets(
            workspace,
            staged_project_content_root,
            ".vtex",
            ".vtexc",
            "textures",
            stage_diagnostics
        )) {
        cleanup_staging();
        return fail_build(std::move(result), std::move(stage_diagnostics));
    }
    if (!stage_cooked_assets(
            workspace,
            staged_project_content_root,
            ".venv",
            ".venvc",
            "environments",
            stage_diagnostics
        )) {
        cleanup_staging();
        return fail_build(std::move(result), std::move(stage_diagnostics));
    }
    for (const auto& [source_ext,cooked_ext] : {std::pair{".vrig", ".vrigc"}, std::pair{".vsound", ".vpcmc"}}) {
        if (!stage_cooked_assets(workspace,staged_project_content_root,source_ext,cooked_ext,"presentation",stage_diagnostics)) {
            cleanup_staging();return fail_build(std::move(result),std::move(stage_diagnostics));
        }
    }
    std::filesystem::copy_file(VICE_PRESENTATION_NOTICES,staging_root/"THIRD_PARTY_PRESENTATION.txt",std::filesystem::copy_options::overwrite_existing,error);
    if(error){cleanup_staging();return fail_build(std::move(result),"Could not stage presentation notices: "+error.message());}
    if (!stage_project_render_materials(
            workspace,
            staged_project_content_root,
            stage_diagnostics
        )) {
        cleanup_staging();
        return fail_build(std::move(result), std::move(stage_diagnostics));
    }

    if (module_build.source_present) {
        const auto staged_module = staging_root / "modules" /
            game_module_filename(workspace.manifest.game_target);
        std::filesystem::create_directories(staged_module.parent_path(), error);
        if (error) {
            const std::string message = error.message();
            cleanup_staging();
            return fail_build(
                std::move(result),
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
                std::move(result),
                "Could not stage project game module `" + path_text(staged_module) + "`: " + message
            );
        }
    }

    std::filesystem::remove_all(output_root, error);
    if (error) {
        const std::string message = error.message();
        cleanup_staging();
        return fail_build(
            std::move(result),
            "Could not replace previous game target output `" + path_text(output_root) + "`: " + message
        );
    }
    std::filesystem::rename(staging_root, output_root, error);
    if (error) {
        const std::string message = error.message();
        cleanup_staging();
        return fail_build(
            std::move(result),
            "Could not publish game target output `" + path_text(output_root) + "`: " + message
        );
    }

    result.success = true;
    result.game_output_root = output_root;
    result.game_executable = target_executable_path(
        output_root,
        workspace.manifest.game_target,
        runtime_host_executable
    );
    result.runtime_manifest = output_root / "vice.runtime.viceproj";
    result.runtime_content_root = output_root / "content";
    if (module_build.source_present) {
        result.game_module = output_root / "modules" /
            game_module_filename(workspace.manifest.game_target);
    }
    result.diagnostics.clear();
    return result;
}

ProjectBuildResult build_project(
    const project::Workspace& workspace,
    const std::filesystem::path& runtime_host_executable
) {
    return build_project(workspace, runtime_host_executable, configured_project_build_toolchain());
}

ProjectLaunchRequest project_launch_request(const ProjectBuildResult& build) {
    ProjectLaunchRequest request;
    if (!build) return request;
    request.executable = build.game_executable;
    request.working_directory = build.game_output_root;
    return request;
}

ProjectRunResult run_project(
    const project::Workspace& workspace,
    const std::filesystem::path& runtime_host_executable,
    const ProjectBuildToolchain& toolchain
) {
    ProjectRunResult result;
    result.build = build_project(workspace, runtime_host_executable, toolchain);
    if (!result.build) {
        result.diagnostics = result.build.diagnostics;
        return result;
    }

    const auto launch_spec = project_launch_request(result.build);
    platform::ProcessLaunchRequest launch_request;
    launch_request.executable = launch_spec.executable;
    launch_request.arguments = launch_spec.arguments;
    launch_request.working_directory = launch_spec.working_directory;
    const auto launched = platform::launch_process(launch_request);
    if (!launched) {
        result.diagnostics = launched.diagnostics;
        return result;
    }

    result.launched = true;
    return result;
}

ProjectRunResult run_project(
    const project::Workspace& workspace,
    const std::filesystem::path& runtime_host_executable
) {
    return run_project(workspace, runtime_host_executable, configured_project_build_toolchain());
}

} // namespace vice::tools
