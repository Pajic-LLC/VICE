#include <vice/runtime/debug_session.hpp>

#include <cstdlib>
#include <atomic>
#include <thread>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "VICE runtime debug-session test failed: " << message << '\n';
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
    using namespace vice::runtime;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        "vice_runtime_debug_session_tests";
    require(!error, "resolve temp directory");
    std::filesystem::remove_all(root, error);
    error.clear();

    const RuntimeDevSession session{
        .id = "debug-session",
        .root = root,
    };
    std::string diagnostics;
    require(prepare_runtime_dev_session(session, &diagnostics), "prepare Runtime session");
    require(
        publish_runtime_dev_snapshot(
            session,
            1U,
            "{\"schema\":\"vice.dev.runtime.v1\",\"frame\":1}\n",
            &diagnostics
        ),
        "publish Runtime availability snapshot"
    );

    const auto entity = persistent_id(0x31U);
    require(
        publish_runtime_debug_request(session, "request-001", entity, &diagnostics),
        "publish bounded debug request"
    );
    require(diagnostics.empty(), "request diagnostics empty");

    const auto request = read_runtime_debug_request(session);
    require(static_cast<bool>(request), "read debug request");
    require(request.request_id == "request-001", "request identity preserved");
    require(request.entity_id == entity, "entity persistent ID preserved");

    const auto before_response = read_runtime_debug_response(session, "request-001");
    require(
        before_response.state == RuntimeDebugResponseState::disconnected,
        "missing response is disconnected"
    );

    const std::string payload =
        "{\"schema\":\"vice.dev.runtime.debug.v1\",\"state\":\"available\"}\n";
    require(
        publish_runtime_debug_response(session, "request-001", payload, &diagnostics),
        "publish bounded debug response"
    );
    const auto response = read_runtime_debug_response(session, "request-001");
    require(static_cast<bool>(response), "read matching debug response");
    require(response.request_id == "request-001", "response identity preserved");
    require(response.payload.find("vice.dev.runtime.debug.v1") != std::string::npos,
        "response payload preserved as structured JSON");

    const auto stale = read_runtime_debug_response(session, "request-002");
    require(
        stale.state == RuntimeDebugResponseState::disconnected,
        "response for previous request is not reused"
    );
    const auto timeout = wait_for_runtime_debug_response(
        session,
        "request-002",
        std::chrono::milliseconds{0}
    );
    require(timeout.state == RuntimeDebugResponseState::timed_out, "bounded wait times out");

    require(
        !publish_runtime_debug_request(
            session,
            "request-003",
            vice::world::PersistentId{},
            &diagnostics
        ),
        "nil entity ID rejected"
    );
    require(
        !publish_runtime_debug_request(session, "../escape", entity, &diagnostics),
        "unsafe request ID rejected"
    );

    const std::string oversized(runtime_debug_transport_max_bytes + 1U, 'x');
    require(
        !publish_runtime_debug_response(session, "request-004", oversized, &diagnostics),
        "oversized debug response rejected"
    );

    const std::string short_payload = "{\"value\":1}";
    const std::string long_payload = "{\"text\":\"" + std::string(48000, 'x') + "\"}";
    require(publish_runtime_debug_response(session, "replacement", short_payload),
            "seed replacement response");
    std::atomic<bool> started = false;
    std::thread publisher{[&] {
        started.store(true);
        for (unsigned i = 0; i < 1000; ++i) {
            std::string message;
            (void)publish_runtime_debug_response(session, "replacement",
                                                 i % 2 ? short_payload : long_payload, &message);
        }
    }};
    while (!started.load())
        std::this_thread::yield();
    std::size_t available = 0;
    for (unsigned i = 0; i < 3000; ++i) {
        const auto observed = read_runtime_debug_response(session, "replacement");
        require(observed.state!=RuntimeDebugResponseState::invalid,observed.diagnostics);
        if(observed){++available;require(observed.payload==short_payload||observed.payload==long_payload,"replacement read preserves one complete response");}
        else require(observed.state==RuntimeDebugResponseState::disconnected,"replacement may be temporarily unavailable but cannot be malformed");
    }
    publisher.join();
    require(available > 0, "replacement stress read available responses");
    require(clear_runtime_dev_session(session, &diagnostics), "clear Runtime session");
    const auto disconnected = read_runtime_debug_response(session, "request-001");
    require(
        disconnected.state == RuntimeDebugResponseState::disconnected,
        "cleared Runtime session is disconnected"
    );

    std::filesystem::remove_all(root, error);
    require(!error, "remove temp directory");
    return 0;
}
