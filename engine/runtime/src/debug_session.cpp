#include <vice/runtime/debug_session.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

namespace vice::runtime {
namespace {

constexpr std::string_view request_filename = "debug-request.json";
constexpr std::string_view response_filename = "debug-response.json";

[[nodiscard]] std::filesystem::path request_path(const RuntimeDevSession& session) {
    return runtime_dev_session_path(session) / request_filename;
}

[[nodiscard]] std::filesystem::path response_path(const RuntimeDevSession& session) {
    return runtime_dev_session_path(session) / response_filename;
}

[[nodiscard]] bool session_available(
    const RuntimeDevSession& session,
    std::string& diagnostics
) {
    if (!valid_runtime_dev_session_id(session.id) || session.root.empty()) {
        diagnostics = "Runtime development session is invalid.";
        return false;
    }
    const auto latest = read_latest_runtime_dev_snapshot(session);
    if (latest.state == RuntimeDevSnapshotState::available) {
        diagnostics.clear();
        return true;
    }
    diagnostics = latest.diagnostics.empty()
        ? "Runtime development session is not connected."
        : latest.diagnostics;
    return false;
}

[[nodiscard]] bool read_bounded_text(
    const std::filesystem::path& path,
    std::string& value,
    std::string& diagnostics
) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        diagnostics = "Could not inspect Runtime debug transport file `" +
            path.generic_string() + "`: " + error.message();
        return false;
    }
    if (size > runtime_debug_transport_max_bytes) {
        diagnostics = "Runtime debug transport file exceeds the bounded message size.";
        return false;
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        diagnostics = "Could not open Runtime debug transport file `" +
            path.generic_string() + "`.";
        return false;
    }
    value.resize(static_cast<std::size_t>(size));
    if (size != 0U) {
        input.read(value.data(), static_cast<std::streamsize>(size));
        if (input.gcount() != static_cast<std::streamsize>(size)) {
            diagnostics = "Could not read complete Runtime debug transport data.";
            return false;
        }
    }
    diagnostics.clear();
    return true;
}

[[nodiscard]] bool write_atomic(
    const std::filesystem::path& path,
    const std::string_view contents,
    std::string& diagnostics
) {
    if (contents.empty() || contents.size() > runtime_debug_transport_max_bytes) {
        diagnostics = "Runtime debug transport payload is empty or exceeds the bounded message size.";
        return false;
    }
    std::error_code error;
    auto temporary = path;
    temporary += ".tmp";
    std::filesystem::remove(temporary, error);
    error.clear();

    std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
    if (!output) {
        diagnostics = "Could not create Runtime debug transport file `" +
            temporary.generic_string() + "`.";
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, error);
        diagnostics = "Could not write Runtime debug transport file `" +
            temporary.generic_string() + "`.";
        return false;
    }

    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        diagnostics = "Could not publish Runtime debug transport file `" +
            path.generic_string() + "`: " + error.message();
        return false;
    }
    diagnostics.clear();
    return true;
}

[[nodiscard]] bool valid_request_id(const std::string_view request_id) noexcept {
    return valid_runtime_dev_session_id(request_id);
}

[[nodiscard]] std::string request_json(
    const std::string_view request_id,
    const world::PersistentId entity_id
) {
    return "{\"schema\":\"" + std::string{runtime_debug_request_schema} +
        "\",\"request_id\":\"" + std::string{request_id} +
        "\",\"entity_id\":\"" + entity_id.to_string() + "\"}\n";
}

[[nodiscard]] RuntimeDebugRequest parse_request(const std::string_view contents) {
    RuntimeDebugRequest result;
    const std::string prefix = "{\"schema\":\"" + std::string{runtime_debug_request_schema} +
        "\",\"request_id\":\"";
    constexpr std::string_view entity_marker = "\",\"entity_id\":\"";
    constexpr std::string_view suffix = "\"}\n";
    if (!contents.starts_with(prefix) || !contents.ends_with(suffix)) {
        result.state = RuntimeDebugRequestState::invalid;
        result.diagnostics = "Runtime debug request schema is invalid.";
        return result;
    }
    const auto request_begin = prefix.size();
    const auto marker = contents.find(entity_marker, request_begin);
    if (marker == std::string_view::npos) {
        result.state = RuntimeDebugRequestState::invalid;
        result.diagnostics = "Runtime debug request identity is malformed.";
        return result;
    }
    result.request_id = std::string{contents.substr(request_begin, marker - request_begin)};
    if (!valid_request_id(result.request_id)) {
        result.state = RuntimeDebugRequestState::invalid;
        result.diagnostics = "Runtime debug request ID is invalid.";
        return result;
    }
    const auto entity_begin = marker + entity_marker.size();
    const auto entity_length = contents.size() - suffix.size() - entity_begin;
    const auto parsed_id = world::PersistentId::parse(contents.substr(entity_begin, entity_length));
    if (!parsed_id.has_value() || parsed_id->is_nil()) {
        result.state = RuntimeDebugRequestState::invalid;
        result.diagnostics = "Runtime debug entity ID is invalid.";
        return result;
    }
    result.entity_id = *parsed_id;
    result.state = RuntimeDebugRequestState::available;
    return result;
}

[[nodiscard]] std::string response_json(
    const std::string_view request_id,
    std::string_view payload
) {
    while (!payload.empty() && (payload.back() == '\n' || payload.back() == '\r')) {
        payload.remove_suffix(1U);
    }
    return "{\"schema\":\"" + std::string{runtime_debug_response_schema} +
        "\",\"request_id\":\"" + std::string{request_id} +
        "\",\"payload\":" + std::string{payload} + "}\n";
}

[[nodiscard]] RuntimeDebugResponse parse_response(
    const std::string_view contents,
    const std::string_view expected_request_id
) {
    RuntimeDebugResponse result;
    const std::string prefix = "{\"schema\":\"" + std::string{runtime_debug_response_schema} +
        "\",\"request_id\":\"";
    constexpr std::string_view payload_marker = "\",\"payload\":";
    constexpr std::string_view suffix = "}\n";
    if (!contents.starts_with(prefix) || !contents.ends_with(suffix)) {
        result.state = RuntimeDebugResponseState::invalid;
        result.diagnostics = "Runtime debug response schema is invalid.";
        return result;
    }
    const auto request_begin = prefix.size();
    const auto marker = contents.find(payload_marker, request_begin);
    if (marker == std::string_view::npos) {
        result.state = RuntimeDebugResponseState::invalid;
        result.diagnostics = "Runtime debug response identity is malformed.";
        return result;
    }
    result.request_id = std::string{contents.substr(request_begin, marker - request_begin)};
    if (!valid_request_id(result.request_id)) {
        result.state = RuntimeDebugResponseState::invalid;
        result.diagnostics = "Runtime debug response request ID is invalid.";
        return result;
    }
    if (result.request_id != expected_request_id) {
        result.state = RuntimeDebugResponseState::disconnected;
        result.diagnostics = "Runtime has not answered this debug request yet.";
        return result;
    }
    const auto payload_begin = marker + payload_marker.size();
    const auto payload_length = contents.size() - suffix.size() - payload_begin;
    if (payload_length == 0U) {
        result.state = RuntimeDebugResponseState::invalid;
        result.diagnostics = "Runtime debug response payload is empty.";
        return result;
    }
    result.payload = std::string{contents.substr(payload_begin, payload_length)};
    result.state = RuntimeDebugResponseState::available;
    return result;
}

} // namespace

bool publish_runtime_debug_request(
    const RuntimeDevSession& session,
    const std::string_view request_id,
    const world::PersistentId entity_id,
    std::string* const diagnostics
) {
    std::string local_diagnostics;
    if (!session_available(session, local_diagnostics)) {
        if (diagnostics != nullptr) *diagnostics = std::move(local_diagnostics);
        return false;
    }
    if (!valid_request_id(request_id)) {
        if (diagnostics != nullptr) *diagnostics = "Runtime debug request ID is invalid.";
        return false;
    }
    if (entity_id.is_nil()) {
        if (diagnostics != nullptr) *diagnostics = "Runtime debug entity ID must be non-nil.";
        return false;
    }
    const auto contents = request_json(request_id, entity_id);
    if (!write_atomic(request_path(session), contents, local_diagnostics)) {
        if (diagnostics != nullptr) *diagnostics = std::move(local_diagnostics);
        return false;
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeDebugRequest read_runtime_debug_request(const RuntimeDevSession& session) {
    RuntimeDebugRequest result;
    std::string diagnostics;
    if (!session_available(session, diagnostics)) {
        result.state = RuntimeDebugRequestState::none;
        result.diagnostics = std::move(diagnostics);
        return result;
    }
    const auto path = request_path(session);
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        if (error) {
            result.state = RuntimeDebugRequestState::invalid;
            result.diagnostics = "Could not inspect Runtime debug request: " + error.message();
        }
        return result;
    }
    std::string contents;
    if (!read_bounded_text(path, contents, diagnostics)) {
        result.state = RuntimeDebugRequestState::invalid;
        result.diagnostics = std::move(diagnostics);
        return result;
    }
    return parse_request(contents);
}

bool publish_runtime_debug_response(
    const RuntimeDevSession& session,
    const std::string_view request_id,
    const std::string_view payload,
    std::string* const diagnostics
) {
    std::string local_diagnostics;
    if (!session_available(session, local_diagnostics)) {
        if (diagnostics != nullptr) *diagnostics = std::move(local_diagnostics);
        return false;
    }
    if (!valid_request_id(request_id)) {
        if (diagnostics != nullptr) *diagnostics = "Runtime debug response request ID is invalid.";
        return false;
    }
    if (payload.empty()) {
        if (diagnostics != nullptr) *diagnostics = "Runtime debug response payload is empty.";
        return false;
    }
    const auto contents = response_json(request_id, payload);
    if (!write_atomic(response_path(session), contents, local_diagnostics)) {
        if (diagnostics != nullptr) *diagnostics = std::move(local_diagnostics);
        return false;
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeDebugResponse read_runtime_debug_response(
    const RuntimeDevSession& session,
    const std::string_view request_id
) {
    RuntimeDebugResponse result;
    result.request_id = std::string{request_id};
    if (!valid_request_id(request_id)) {
        result.state = RuntimeDebugResponseState::invalid;
        result.diagnostics = "Runtime debug response request ID is invalid.";
        return result;
    }
    std::string diagnostics;
    if (!session_available(session, diagnostics)) {
        result.state = RuntimeDebugResponseState::disconnected;
        result.diagnostics = std::move(diagnostics);
        return result;
    }
    const auto path = response_path(session);
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        if (error) {
            result.state = RuntimeDebugResponseState::invalid;
            result.diagnostics = "Could not inspect Runtime debug response: " + error.message();
        } else {
            result.state = RuntimeDebugResponseState::disconnected;
            result.diagnostics = "Runtime has not answered this debug request yet.";
        }
        return result;
    }
    std::string contents;
    if (!read_bounded_text(path, contents, diagnostics)) {
        result.state = RuntimeDebugResponseState::invalid;
        result.diagnostics = std::move(diagnostics);
        return result;
    }
    return parse_response(contents, request_id);
}

RuntimeDebugResponse wait_for_runtime_debug_response(
    const RuntimeDevSession& session,
    const std::string_view request_id,
    const std::chrono::milliseconds timeout,
    const std::chrono::milliseconds poll_interval
) {
    if (timeout.count() < 0 || poll_interval.count() <= 0) {
        return RuntimeDebugResponse{
            .state = RuntimeDebugResponseState::invalid,
            .request_id = std::string{request_id},
            .diagnostics = "Runtime debug response wait interval is invalid.",
        };
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        auto response = read_runtime_debug_response(session, request_id);
        if (response.state == RuntimeDebugResponseState::available ||
            response.state == RuntimeDebugResponseState::invalid) {
            return response;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return RuntimeDebugResponse{
                .state = RuntimeDebugResponseState::timed_out,
                .request_id = std::string{request_id},
                .diagnostics = "Timed out waiting for Runtime debug response.",
            };
        }
        std::this_thread::sleep_for(poll_interval);
    }
}

} // namespace vice::runtime
