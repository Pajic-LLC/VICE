#pragma once

#include <vice/runtime/dev_session.hpp>
#include <vice/world/persistent_id.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace vice::runtime {

inline constexpr std::string_view runtime_debug_request_schema =
    "vice.dev.runtime.debug.request.v1";
inline constexpr std::string_view runtime_debug_response_schema =
    "vice.dev.runtime.debug.response.v1";
inline constexpr std::size_t runtime_debug_transport_max_bytes = 64U * 1024U;

enum class RuntimeDebugRequestState : std::uint8_t {
    none,
    available,
    invalid,
};

enum class RuntimeDebugRequestKind : std::uint8_t { entity, world_checkpoint };

struct RuntimeDebugRequest {
    RuntimeDebugRequestKind kind = RuntimeDebugRequestKind::entity;
    RuntimeDebugRequestState state = RuntimeDebugRequestState::none;
    std::string request_id;
    world::PersistentId entity_id{};
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return state == RuntimeDebugRequestState::available;
    }
};

enum class RuntimeDebugResponseState : std::uint8_t {
    available,
    disconnected,
    invalid,
    timed_out,
};

struct RuntimeDebugResponse {
    RuntimeDebugResponseState state = RuntimeDebugResponseState::disconnected;
    std::string request_id;
    std::string payload;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return state == RuntimeDebugResponseState::available;
    }
};

[[nodiscard]] bool publish_runtime_debug_request(
    const RuntimeDevSession& session,
    std::string_view request_id,
    world::PersistentId entity_id,
    std::string* diagnostics = nullptr
);

[[nodiscard]] bool publish_runtime_checkpoint_request(
    const RuntimeDevSession& session, std::string_view request_id,
    std::string* diagnostics = nullptr
);

[[nodiscard]] RuntimeDebugRequest read_runtime_debug_request(
    const RuntimeDevSession& session
);

[[nodiscard]] bool publish_runtime_debug_response(
    const RuntimeDevSession& session,
    std::string_view request_id,
    std::string_view payload,
    std::string* diagnostics = nullptr
);

[[nodiscard]] RuntimeDebugResponse read_runtime_debug_response(
    const RuntimeDevSession& session,
    std::string_view request_id
);

[[nodiscard]] RuntimeDebugResponse wait_for_runtime_debug_response(
    const RuntimeDevSession& session,
    std::string_view request_id,
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds poll_interval = std::chrono::milliseconds{10}
);

} // namespace vice::runtime
