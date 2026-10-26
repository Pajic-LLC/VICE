#include <vice/dev/cli.hpp>
#include <vice/runtime/dev_session.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "VICE runtime session CLI test failed: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] vice::dev::CommandResult inspect(const std::filesystem::path& path) {
    const std::string path_text = path.string();
    const std::array<std::string_view, 3> arguments{"runtime", "inspect", path_text};
    return vice::dev::execute(arguments, {});
}

} // namespace

int main() {
    using namespace vice;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        "vice_dev_cli_runtime_session_tests";
    require(!error, "resolve temp root");
    std::filesystem::remove_all(root, error);
    error.clear();

    const runtime::RuntimeDevSession session{
        .id = "session-17",
        .root = root / "runtime-sessions",
    };
    std::string diagnostics;
    require(runtime::prepare_runtime_dev_session(session, &diagnostics), "prepare session");
    require(diagnostics.empty(), "prepare session diagnostics");

    const auto session_path = runtime::runtime_dev_session_path(session);
    const auto disconnected = inspect(session_path);
    require(disconnected.exit_code == dev::exit_unavailable, "disconnected exit status");
    require(disconnected.json.find("\"command\":\"runtime.inspect\"") != std::string::npos,
        "runtime inspect command");
    require(disconnected.json.find("\"state\":\"disconnected\"") != std::string::npos,
        "disconnected state");
    require(disconnected.json.find("\"ok\":false") != std::string::npos,
        "disconnected is not success");

    constexpr std::string_view payload =
        "{\"schema\":\"vice.dev.runtime.v1\",\"frame\":{\"index\":7},"
        "\"camera\":{\"global_position\":[1000000000000.25,-2000000000000.5,3000000000000.75]}}\n";
    require(
        runtime::publish_runtime_dev_snapshot(session, 1U, payload, &diagnostics),
        "publish runtime snapshot"
    );
    require(diagnostics.empty(), "publish diagnostics");

    const auto available = inspect(session_path);
    require(available.exit_code == dev::exit_success, "available exit status");
    require(available.json.find("\"state\":\"available\"") != std::string::npos,
        "available state");
    require(available.json.find("\"generation\":1") != std::string::npos,
        "snapshot generation");
    require(available.json.find("vice.dev.runtime.v1") != std::string::npos,
        "runtime schema preserved in payload");
    require(available.json.find("1000000000000.25") != std::string::npos,
        "large-world precision preserved through CLI envelope");
    require(available.json.find("\"ok\":true") != std::string::npos,
        "available snapshot success");

    const auto available_again = inspect(session_path);
    require(available_again.json == available.json, "deterministic unchanged runtime inspect");

    const auto invalid = inspect(root / "runtime-sessions" / "bad session");
    require(invalid.exit_code == dev::exit_validation, "invalid session path exit status");
    require(invalid.json.find("Runtime session path is invalid") != std::string::npos,
        "invalid session diagnostic");

    require(runtime::clear_runtime_dev_session(session, &diagnostics), "clear session");
    std::filesystem::remove_all(root, error);
    require(!error, "remove temp root");
    return 0;
}
