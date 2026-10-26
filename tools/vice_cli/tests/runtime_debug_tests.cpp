#include <vice/dev/cli.hpp>

#include <vice/runtime/debug_session.hpp>
#include <vice/runtime/dev_session.hpp>
#include <vice/world/persistent_id.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "VICE runtime debug CLI test failed: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] vice::world::PersistentId persistent_id(const std::uint8_t value) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[0] = value;
    bytes[6] = 0x40U;
    bytes[8] = 0x80U;
    bytes[15] = static_cast<std::uint8_t>(value ^ 0x5aU);
    return vice::world::PersistentId{bytes};
}

} // namespace

int main() {
    using namespace vice;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) / "vice_runtime_debug_cli_tests";
    require(!error, "resolve temp directory");
    std::filesystem::remove_all(root, error);
    error.clear();

    const runtime::RuntimeDevSession session{
        .id = "cli-debug-session",
        .root = root,
    };
    std::string diagnostics;
    require(runtime::prepare_runtime_dev_session(session, &diagnostics), "prepare Runtime session");
    require(
        runtime::publish_runtime_dev_snapshot(
            session,
            1U,
            "{\"schema\":\"vice.dev.runtime.v1\",\"frame\":1}\n",
            &diagnostics
        ),
        "publish Runtime availability snapshot"
    );

    const auto entity = persistent_id(0x44U);
    std::thread runtime_worker{[&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        for (;;) {
            const auto request = runtime::read_runtime_debug_request(session);
            if (request) {
                require(request.entity_id == entity, "CLI request carries selected persistent ID");
                const std::string payload =
                    "{\"schema\":\"vice.dev.runtime.debug.v1\",\"state\":\"available\","
                    "\"entity_id\":\"" + entity.to_string() + "\"}\n";
                std::string publish_diagnostics;
                require(
                    runtime::publish_runtime_debug_response(
                        session,
                        request.request_id,
                        payload,
                        &publish_diagnostics
                    ),
                    "publish simulated Runtime debug response"
                );
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                require(false, "CLI publishes debug request before bounded deadline");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }};

    const auto result = dev::execute_runtime_debug(
        runtime::runtime_dev_session_path(session),
        entity.to_string()
    );
    runtime_worker.join();

    require(result.exit_code == dev::exit_success, "runtime debug command succeeds");
    require(result.json.find("\"command\":\"runtime.debug\"") != std::string::npos,
        "structured command identity");
    require(result.json.find("\"ok\":true") != std::string::npos, "structured success");
    require(result.json.find(entity.to_string()) != std::string::npos,
        "selected entity identity returned");
    require(result.json.find("vice.dev.runtime.debug.v1") != std::string::npos,
        "Runtime debug payload returned");

    const auto invalid = dev::execute_runtime_debug(
        runtime::runtime_dev_session_path(session),
        "not-a-persistent-id"
    );
    require(invalid.exit_code == dev::exit_validation, "invalid entity rejected before transport");
    require(invalid.json.find("\"ok\":false") != std::string::npos,
        "invalid entity returns structured failure");

    require(runtime::clear_runtime_dev_session(session, &diagnostics), "clear Runtime session");
    const auto disconnected = dev::execute_runtime_debug(
        runtime::runtime_dev_session_path(session),
        entity.to_string()
    );
    require(disconnected.exit_code == dev::exit_unavailable, "disconnected Runtime reported");
    require(disconnected.json.find("\"state\":\"disconnected\"") != std::string::npos,
        "disconnected state is structured");

    std::filesystem::remove_all(root, error);
    require(!error, "remove temp directory");
    return 0;
}
