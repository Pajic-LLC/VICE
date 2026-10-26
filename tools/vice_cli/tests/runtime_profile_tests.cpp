#include <vice/dev/cli.hpp>
#include <vice/runtime/dev_session.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "VICE runtime profile CLI test failed: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    using namespace vice;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        "vice_runtime_profile_cli_tests";
    require(!error, "resolve temporary directory");
    std::filesystem::remove_all(root, error);
    error.clear();

    const runtime::RuntimeDevSession session{
        .id = "profile-session-17",
        .root = root / "runtime-sessions",
    };
    std::string diagnostics;
    require(runtime::prepare_runtime_dev_session(session, &diagnostics), "prepare session");

    const auto session_path = runtime::runtime_dev_session_path(session);
    const auto disconnected = dev::execute_runtime_profile(session_path);
    require(disconnected.exit_code == dev::exit_unavailable, "disconnected exit status");
    require(disconnected.json.find("\"command\":\"runtime.profile\"") != std::string::npos,
        "profile command identity");
    require(disconnected.json.find("\"state\":\"disconnected\"") != std::string::npos,
        "disconnected profile state");

    constexpr std::string_view payload =
        "{\"schema\":\"vice.dev.runtime.v1\","
        "\"profile\":{\"units\":\"seconds\",\"capacity\":120,\"count\":2,"
        "\"stages\":[\"frame\",\"update\",\"render\"],\"frames\":["
        "{\"frame_index\":76,\"frame\":0.017,\"update\":0.004,\"render\":0.009},"
        "{\"frame_index\":77,\"frame\":0.016,\"update\":0.0035,\"render\":0.008}]},"
        "\"camera\":{\"global_position\":[1000000000000.25,-2000000000000.5,3000000000000.75]}}\n";
    require(
        runtime::publish_runtime_dev_snapshot(session, 7U, payload, &diagnostics),
        "publish profile snapshot"
    );

    const auto available = dev::execute_runtime_profile(session_path);
    require(available.exit_code == dev::exit_success, "profile success exit status");
    require(available.json.find("\"command\":\"runtime.profile\"") != std::string::npos,
        "profile response command");
    require(available.json.find("\"state\":\"available\"") != std::string::npos,
        "profile available state");
    require(available.json.find("\"generation\":7") != std::string::npos,
        "profile generation");
    require(available.json.find("\\\"units\\\":\\\"seconds\\\"") != std::string::npos,
        "profile units preserved");
    require(available.json.find("\\\"capacity\\\":120") != std::string::npos,
        "profile capacity preserved");
    require(available.json.find("\\\"stages\\\":[\\\"frame\\\",\\\"update\\\",\\\"render\\\"]") != std::string::npos,
        "profile stages preserved");
    require(available.json.find("1000000000000.25") != std::string::npos,
        "large-world precision preserved in profile envelope");
    require(available.json.find("\"ok\":true") != std::string::npos,
        "profile snapshot is successful");

    const auto available_again = dev::execute_runtime_profile(session_path);
    require(available_again.json == available.json, "deterministic profile read");

    const auto invalid = dev::execute_runtime_profile(root / "runtime-sessions" / "bad session");
    require(invalid.exit_code == dev::exit_validation, "invalid session exit status");
    require(invalid.json.find("Runtime session path is invalid") != std::string::npos,
        "invalid session diagnostic");

    require(runtime::clear_runtime_dev_session(session, &diagnostics), "clear profile session");
    std::filesystem::remove_all(root, error);
    require(!error, "remove temporary directory");
    return 0;
}
