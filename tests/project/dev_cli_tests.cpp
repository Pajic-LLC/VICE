#include <vice/dev/cli.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#ifndef VICE_DEV_CLI_TEST_RUNTIME_HOST
#error "VICE dev CLI tests require a launchable test runtime host"
#endif

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "VICE dev CLI test failed: " << message << '\n';
    std::exit(1);
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    require(!error, "create test directory");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "open test file");
    output << text;
    require(static_cast<bool>(output), "write test file");
}

[[nodiscard]] vice::dev::CommandResult execute(
    const std::string_view domain,
    const std::string_view operation,
    const std::filesystem::path& path
) {
    const std::string path_text = path.string();
    const std::array<std::string_view, 3> arguments{domain, operation, path_text};
    return vice::dev::execute(arguments);
}

[[nodiscard]] vice::dev::CommandResult execute(
    const std::string_view domain,
    const std::string_view operation,
    const std::filesystem::path& path,
    const vice::dev::CommandEnvironment& environment
) {
    const std::string path_text = path.string();
    const std::array<std::string_view, 3> arguments{domain, operation, path_text};
    return vice::dev::execute(arguments, environment);
}

} // namespace

int main() {
    using namespace std::string_view_literals;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) / "vice_dev_cli_contract_tests";
    require(!error, "resolve temporary directory");
    std::filesystem::remove_all(root, error);
    error.clear();

    const auto project_path = root / "game.viceproj";
    const auto world_path = root / "assets" / "worlds" / "main.vworld";
    write_text(
        project_path,
        "vice-project 1\n"
        "id 82000000-0000-4000-8000-000000000001\n"
        "name \"VICE Agent Ω\"\n"
        "game_name \"VICE Agent Game\"\n"
        "game_target \"vice-agent-game\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    write_text(
        world_path,
        "vice-world-source 1\n"
        "component 82000000-0000-4000-8000-000000000110 2 \"Gameplay State\"\n"
        "resource 82000000-0000-4000-8000-000000000202\n"
        "resource 82000000-0000-4000-8000-000000000201\n"
        "region 9 90 1000000000000.25 -2000000000000.5 3000000000000.75 12 -13 14 reduced bounds -10 -20 -30 10 20 30\n"
        "entity 82000000-0000-4000-8000-000000000302 none\n"
        "entity 82000000-0000-4000-8000-000000000301 spatial 1000000000001.25 -2000000000002.5 3000000000003.75 9 transform 0 0 0 1 1 1 1 resource render_mesh 82000000-0000-4000-8000-000000000201\n"
        "relation 82000000-0000-4000-8000-000000000301 82000000-0000-4000-8000-000000000401 82000000-0000-4000-8000-000000000302\n"
    );

    const auto project_inspect = execute("project", "inspect", project_path);
    require(project_inspect.exit_code == vice::dev::exit_success, "project inspect exit status");
    require(project_inspect.json.starts_with("{\"schema\":\"vice.dev.v1\""), "project schema envelope");
    require(project_inspect.json.find("\"command\":\"project.inspect\"") != std::string::npos, "project command");
    require(project_inspect.json.find("\"ok\":true") != std::string::npos, "project success");
    require(project_inspect.json.find("82000000-0000-4000-8000-000000000001") != std::string::npos, "project ID");
    require(project_inspect.json.find("VICE Agent Ω") != std::string::npos, "UTF-8 project name");
    require(project_inspect.json.find("\"startup_world_exists\":true") != std::string::npos, "startup World existence");
    require(project_inspect.json.ends_with("}\n"), "project response newline");

    const auto project_inspect_again = execute("project", "inspect", project_path);
    require(project_inspect_again.json == project_inspect.json, "deterministic project JSON");

    const auto project_validate = execute("project", "validate", project_path);
    require(project_validate.exit_code == vice::dev::exit_success, "project validate exit status");
    require(project_validate.json.find("\"command\":\"project.validate\"") != std::string::npos, "project validate command");

    const auto world_inspect = execute("world", "inspect", world_path);
    require(world_inspect.exit_code == vice::dev::exit_success, "World inspect exit status");
    require(world_inspect.json.find("\"command\":\"world.inspect\"") != std::string::npos, "World command");
    require(world_inspect.json.find("\"component_types\":1") != std::string::npos, "component count");
    require(world_inspect.json.find("\"resources\":2") != std::string::npos, "resource count");
    require(world_inspect.json.find("\"regions\":1") != std::string::npos, "region count");
    require(world_inspect.json.find("\"entities\":2") != std::string::npos, "entity count");
    require(world_inspect.json.find("\"relations\":1") != std::string::npos, "relation count");
    require(world_inspect.json.find("1000000000000.25") != std::string::npos, "large World region precision");
    require(world_inspect.json.find("1000000000001.25") != std::string::npos, "large World entity precision");
    require(world_inspect.json.find("\"reduced\":1") != std::string::npos, "fidelity summary");
    require(world_inspect.json.find("\"authored_transform\":true") != std::string::npos, "authored transform feature");
    require(world_inspect.json.find("\"role\":\"render_mesh\"") != std::string::npos, "resource role");

    const auto first_entity = world_inspect.json.find("82000000-0000-4000-8000-000000000301");
    const auto second_entity = world_inspect.json.find("82000000-0000-4000-8000-000000000302");
    require(first_entity != std::string::npos && second_entity != std::string::npos, "entity IDs emitted");
    require(first_entity < second_entity, "entity IDs deterministically ordered");
    const auto first_resource = world_inspect.json.find("82000000-0000-4000-8000-000000000201");
    const auto second_resource = world_inspect.json.find("82000000-0000-4000-8000-000000000202");
    require(first_resource != std::string::npos && second_resource != std::string::npos, "resource IDs emitted");
    require(first_resource < second_resource, "resource IDs deterministically ordered");

    const auto world_inspect_again = execute("world", "inspect", world_path);
    require(world_inspect_again.json == world_inspect.json, "deterministic World JSON");

    vice::dev::CommandEnvironment workflow_environment;
    workflow_environment.runtime_host_executable = std::filesystem::path{VICE_DEV_CLI_TEST_RUNTIME_HOST};

    const auto project_build = execute("project", "build", project_path, workflow_environment);
    require(project_build.exit_code == vice::dev::exit_success, "project build exit status");
    require(project_build.json.find("\"command\":\"project.build\"") != std::string::npos, "project build command");
    require(project_build.json.find("\"state\":\"built\"") != std::string::npos, "project build state");
    require(project_build.json.find("\"project_id\":\"82000000-0000-4000-8000-000000000001\"") != std::string::npos, "build project identity");
    require(project_build.json.find("\"entities\":2") != std::string::npos, "build entity count");
    require(project_build.json.find("\"regions\":1") != std::string::npos, "build region count");
    require(project_build.json.find("\"source_present\":false") != std::string::npos, "build optional game module state");
    require(project_build.json.find("vice-agent-game") != std::string::npos, "build game target");

    const auto launched_run = execute("project", "run", project_path, workflow_environment);
    require(launched_run.exit_code == vice::dev::exit_success, "project run launch exit status");
    require(launched_run.json.find("\"command\":\"project.run\"") != std::string::npos, "project run command");
    require(launched_run.json.find("\"state\":\"launched\"") != std::string::npos, "project launch success state");
    require(launched_run.json.find("\"ok\":true") != std::string::npos, "successful process creation is success");

#ifdef _WIN32
    const auto missing_runtime = root / "bin" / "missing_runtime.exe";
#else
    const auto missing_runtime = root / "bin" / "missing_runtime";
#endif
    vice::dev::CommandEnvironment failing_launch_environment;
    failing_launch_environment.runtime_host_executable = missing_runtime;

    const auto failed_launch = execute("project", "run", project_path, failing_launch_environment);
    require(failed_launch.exit_code == vice::dev::exit_launch, "project run distinguishes launch failure");
    require(failed_launch.json.find("\"state\":\"launch_failed\"") != std::string::npos, "project launch failure state");
    require(failed_launch.json.find("\"ok\":false") != std::string::npos, "launch failure is not success");

    const vice::dev::CommandEnvironment unavailable_environment;
    const auto unavailable_build = execute("project", "build", project_path, unavailable_environment);
    require(unavailable_build.exit_code == vice::dev::exit_unavailable, "missing runtime host is unavailable");
    require(unavailable_build.json.find("runtime host") != std::string::npos, "missing runtime diagnostic");

    const auto wrong_world_type = root / "assets" / "worlds" / "main.vworldc";
    write_text(wrong_world_type, "not cooked data"sv);
    const auto wrong_type_result = execute("world", "validate", wrong_world_type);
    require(wrong_type_result.exit_code == vice::dev::exit_validation, "cooked World rejected");
    require(wrong_type_result.json.find("\"ok\":false") != std::string::npos, "structured World failure");

    const auto invalid_world = root / "assets" / "worlds" / "invalid.vworld";
    write_text(invalid_world, "vice-world-source 999\n"sv);
    const auto invalid_world_result = execute("world", "validate", invalid_world);
    require(invalid_world_result.exit_code == vice::dev::exit_validation, "invalid World exit status");
    require(invalid_world_result.json.find("World source line 1") != std::string::npos, "World diagnostic preserved");

    const auto invalid_project = root / "invalid.viceproj";
    write_text(
        invalid_project,
        "vice-project 1\n"
        "id 82000000-0000-4000-8000-000000000001\n"
        "name \"Invalid\"\n"
        "assets \"../assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    const auto invalid_project_result = execute("project", "inspect", invalid_project);
    require(invalid_project_result.exit_code == vice::dev::exit_validation, "invalid project exit status");
    require(invalid_project_result.json.find("\"ok\":false") != std::string::npos, "structured project failure");
    require(invalid_project_result.json.find("traversal") != std::string::npos, "project contract diagnostic");

    write_text(world_path, "vice-world-source 999\n"sv);
    const auto invalid_startup_result = execute("project", "validate", project_path);
    require(invalid_startup_result.exit_code == vice::dev::exit_validation, "invalid startup World fails project validation");
    require(invalid_startup_result.json.find("Configured startup World is invalid") != std::string::npos, "startup World validation context");
    require(invalid_startup_result.json.find("World source line 1") != std::string::npos, "startup World parser diagnostic");

    const auto failed_build = execute("project", "build", project_path, workflow_environment);
    require(failed_build.exit_code == vice::dev::exit_build, "project build failure has distinct exit status");
    require(failed_build.json.find("\"state\":\"build_failed\"") != std::string::npos, "project build failure state");
    require(failed_build.json.find("World source line 1") != std::string::npos, "build preserves World cooker diagnostic");

    const auto failed_run = execute("project", "run", project_path, workflow_environment);
    require(failed_run.exit_code == vice::dev::exit_build, "project run distinguishes build failure");
    require(failed_run.json.find("\"state\":\"build_failed\"") != std::string::npos, "run build failure state");

    const std::array<std::string_view, 0> no_arguments{};
    const auto usage = vice::dev::execute(no_arguments);
    require(usage.exit_code == vice::dev::exit_usage, "usage exit status");
    require(usage.json.find("\"command\":\"usage\"") != std::string::npos, "structured usage response");

    std::filesystem::remove_all(root, error);
    require(!error, "remove test directory");
    return 0;
}
