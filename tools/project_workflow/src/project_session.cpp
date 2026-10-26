#include <vice/tools/project_workflow.hpp>

#include <vice/platform/process.hpp>
#include <vice/runtime/dev_session.hpp>

#include <filesystem>
#include <string>
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

} // namespace

ProjectLaunchRequest project_launch_request(
    const ProjectBuildResult& build,
    const ProjectRuntimeSession& runtime_session
) {
    auto request = project_launch_request(build);
    if (request.executable.empty()) return request;
    request.arguments.push_back("--vice-dev-session");
    request.arguments.push_back(path_text(runtime_session.root));
    request.arguments.push_back(runtime_session.id);
    return request;
}

ProjectRunResult run_project_with_session(
    const project::Workspace& workspace,
    const std::filesystem::path& runtime_host_executable,
    const ProjectBuildToolchain& toolchain,
    ProjectRuntimeSession runtime_session
) {
    ProjectRunResult result;
    result.build = build_project(workspace, runtime_host_executable, toolchain);
    if (!result.build) {
        result.diagnostics = result.build.diagnostics;
        return result;
    }

    runtime::RuntimeDevSession session{
        .id = runtime_session.id,
        .root = runtime_session.root,
    };
    std::string session_diagnostics;
    if (!runtime::prepare_runtime_dev_session(session, &session_diagnostics)) {
        result.diagnostics = "Could not prepare VICE Runtime development session: " +
            session_diagnostics;
        return result;
    }

    const auto launch_spec = project_launch_request(result.build, runtime_session);
    platform::ProcessLaunchRequest launch_request;
    launch_request.executable = launch_spec.executable;
    launch_request.arguments = launch_spec.arguments;
    launch_request.working_directory = launch_spec.working_directory;
    const auto launched = platform::launch_process(launch_request);
    if (!launched) {
        std::string cleanup_diagnostics;
        (void)runtime::clear_runtime_dev_session(session, &cleanup_diagnostics);
        result.diagnostics = launched.diagnostics;
        return result;
    }

    result.launched = true;
    result.runtime_session = std::move(runtime_session);
    return result;
}

ProjectRunResult run_project_with_session(
    const project::Workspace& workspace,
    const std::filesystem::path& runtime_host_executable,
    ProjectRuntimeSession runtime_session
) {
    return run_project_with_session(
        workspace,
        runtime_host_executable,
        configured_project_build_toolchain(),
        std::move(runtime_session)
    );
}

} // namespace vice::tools
