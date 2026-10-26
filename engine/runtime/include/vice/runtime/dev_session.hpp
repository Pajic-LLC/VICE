#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace vice::runtime {

inline constexpr std::string_view runtime_dev_session_schema = "vice.dev.runtime.session.v1";
inline constexpr std::size_t runtime_dev_snapshot_max_bytes = 512U * 1024U;
inline constexpr std::size_t runtime_dev_snapshot_history = 16U;

struct RuntimeDevSession {
    std::string id;
    std::filesystem::path root;
};

enum class RuntimeDevSnapshotState : std::uint8_t {
    available,
    disconnected,
    invalid,
    timed_out,
};

struct RuntimeDevSnapshot {
    RuntimeDevSnapshotState state = RuntimeDevSnapshotState::disconnected;
    std::uint64_t generation = 0U;
    std::string payload;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return state == RuntimeDevSnapshotState::available;
    }
};

[[nodiscard]] bool valid_runtime_dev_session_id(std::string_view id) noexcept;

[[nodiscard]] std::filesystem::path runtime_dev_session_path(
    const RuntimeDevSession& session
);

[[nodiscard]] bool prepare_runtime_dev_session(
    const RuntimeDevSession& session,
    std::string* diagnostics = nullptr
);

[[nodiscard]] bool publish_runtime_dev_snapshot(
    const RuntimeDevSession& session,
    std::uint64_t generation,
    std::string_view payload,
    std::string* diagnostics = nullptr
);

[[nodiscard]] RuntimeDevSnapshot read_latest_runtime_dev_snapshot(
    const RuntimeDevSession& session
);

[[nodiscard]] RuntimeDevSnapshot wait_for_runtime_dev_snapshot(
    const RuntimeDevSession& session,
    std::uint64_t after_generation,
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds poll_interval = std::chrono::milliseconds{10}
);

[[nodiscard]] bool clear_runtime_dev_session(
    const RuntimeDevSession& session,
    std::string* diagnostics = nullptr
);

} // namespace vice::runtime
