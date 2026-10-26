#include <vice/runtime/dev_session.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "VICE runtime dev-session test failed: " << message << '\n';
    std::exit(1);
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "open test file");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    require(static_cast<bool>(output), "write test file");
}

} // namespace

int main() {
    using namespace vice::runtime;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) / "vice_runtime_dev_session_tests";
    require(!error, "resolve temp directory");
    std::filesystem::remove_all(root, error);
    error.clear();

    require(valid_runtime_dev_session_id("game-01.session"), "valid session ID");
    require(!valid_runtime_dev_session_id(""), "empty session ID rejected");
    require(!valid_runtime_dev_session_id("../escape"), "traversal session ID rejected");
    require(!valid_runtime_dev_session_id(".."), "parent session ID rejected");

    const RuntimeDevSession session{
        .id = "game-01.session",
        .root = root,
    };
    require(runtime_dev_session_path(session) == root / session.id, "session path");

    const auto disconnected = read_latest_runtime_dev_snapshot(session);
    require(
        disconnected.state == RuntimeDevSnapshotState::disconnected,
        "missing session is disconnected"
    );

    std::string diagnostics;
    require(prepare_runtime_dev_session(session, &diagnostics), "prepare session");
    require(diagnostics.empty(), "prepare diagnostics empty");
    require(prepare_runtime_dev_session(session, &diagnostics), "prepare session idempotent");

    const auto waiting = read_latest_runtime_dev_snapshot(session);
    require(
        waiting.state == RuntimeDevSnapshotState::disconnected,
        "prepared session without snapshot is disconnected"
    );

    const std::string first_payload = "{\"schema\":\"vice.dev.v1\",\"value\":1}\n";
    require(
        publish_runtime_dev_snapshot(session, 1U, first_payload, &diagnostics),
        "publish first snapshot"
    );
    require(!publish_runtime_dev_snapshot(session, 1U, first_payload, &diagnostics), "generation immutable");

    const auto first = read_latest_runtime_dev_snapshot(session);
    require(static_cast<bool>(first), "read first snapshot");
    require(first.generation == 1U, "first generation");
    require(first.payload == first_payload, "first payload exact");

    write_text(runtime_dev_session_path(session) / "snapshot-99999999999999999999.json.tmp", "partial");
    const auto ignores_temporary = read_latest_runtime_dev_snapshot(session);
    require(ignores_temporary.generation == 1U, "temporary snapshot ignored");

    const std::string oversized(runtime_dev_snapshot_max_bytes + 1U, 'x');
    require(
        !publish_runtime_dev_snapshot(session, 2U, oversized, &diagnostics),
        "oversized snapshot rejected"
    );

    for (std::uint64_t generation = 2U; generation <= 20U; ++generation) {
        const std::string payload = "{\"generation\":" + std::to_string(generation) + "}\n";
        require(
            publish_runtime_dev_snapshot(session, generation, payload, &diagnostics),
            "publish retained snapshot history"
        );
    }

    const auto latest = read_latest_runtime_dev_snapshot(session);
    require(static_cast<bool>(latest), "read latest snapshot");
    require(latest.generation == 20U, "latest generation");
    require(latest.payload == "{\"generation\":20}\n", "latest payload");

    std::size_t snapshot_count = 0U;
    for (const auto& entry : std::filesystem::directory_iterator{runtime_dev_session_path(session)}) {
        const auto filename = entry.path().filename().string();
        if (filename.starts_with("snapshot-") && filename.ends_with(".json")) ++snapshot_count;
    }
    require(snapshot_count == runtime_dev_snapshot_history, "snapshot history bounded");

    const auto timeout = wait_for_runtime_dev_snapshot(
        session,
        20U,
        std::chrono::milliseconds{0}
    );
    require(timeout.state == RuntimeDevSnapshotState::timed_out, "bounded wait timeout");
    require(timeout.generation == 20U, "timeout reports observed generation");

    require(
        publish_runtime_dev_snapshot(session, 21U, "{\"generation\":21}\n", &diagnostics),
        "publish newer snapshot"
    );
    const auto newer = wait_for_runtime_dev_snapshot(
        session,
        20U,
        std::chrono::milliseconds{10}
    );
    require(static_cast<bool>(newer), "newer snapshot available");
    require(newer.generation == 21U, "newer generation");

    const RuntimeDevSession foreign{
        .id = "foreign",
        .root = root,
    };
    std::filesystem::create_directories(runtime_dev_session_path(foreign), error);
    require(!error, "create foreign directory");
    write_text(runtime_dev_session_path(foreign) / "keep.txt", "owned elsewhere\n");
    require(!clear_runtime_dev_session(foreign, &diagnostics), "foreign directory not removed");
    require(
        std::filesystem::is_regular_file(runtime_dev_session_path(foreign) / "keep.txt"),
        "foreign data preserved"
    );

    require(clear_runtime_dev_session(session, &diagnostics), "clear prepared session");
    require(!std::filesystem::exists(runtime_dev_session_path(session)), "session directory removed");
    const auto cleared = read_latest_runtime_dev_snapshot(session);
    require(cleared.state == RuntimeDevSnapshotState::disconnected, "cleared session disconnected");

    std::filesystem::remove_all(root, error);
    require(!error, "remove temp directory");
    return 0;
}
