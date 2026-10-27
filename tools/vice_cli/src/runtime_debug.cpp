#include <vice/dev/cli.hpp>

#include <vice/runtime/debug_session.hpp>
#include <vice/world/persistent_id.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace vice::dev {
namespace {

void append_json_string(std::string& output, const std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20U) {
                output += "\\u00";
                output.push_back(hex[(character >> 4U) & 0x0fU]);
                output.push_back(hex[character & 0x0fU]);
            } else {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output.push_back('"');
}

[[nodiscard]] std::string normalized_path(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) return path.lexically_normal().generic_string();
    return absolute.lexically_normal().generic_string();
}

[[nodiscard]] constexpr std::string_view state_name(
    const runtime::RuntimeDebugResponseState state
) noexcept {
    switch (state) {
    case runtime::RuntimeDebugResponseState::available: return "available";
    case runtime::RuntimeDebugResponseState::disconnected: return "disconnected";
    case runtime::RuntimeDebugResponseState::invalid: return "invalid";
    case runtime::RuntimeDebugResponseState::timed_out: return "timed_out";
    }
    return "invalid";
}

[[nodiscard]] std::string debug_response(
    const bool ok,
    const std::vector<std::string>& diagnostics,
    const runtime::RuntimeDevSession& session,
    const std::string_view entity_id,
    const runtime::RuntimeDebugResponse& response
) {
    std::string output;
    output.reserve(512U + response.payload.size());
    output += "{\"schema\":\"vice.dev.v1\",\"command\":\"runtime.debug\",\"ok\":";
    output += ok ? "true" : "false";
    output += ",\"diagnostics\":[";
    for (std::size_t index = 0U; index < diagnostics.size(); ++index) {
        if (index != 0U) output.push_back(',');
        append_json_string(output, diagnostics[index]);
    }
    output += "],\"data\":{\"state\":";
    append_json_string(output, state_name(response.state));
    output += ",\"session_id\":";
    append_json_string(output, session.id);
    output += ",\"session_path\":";
    append_json_string(output, normalized_path(runtime::runtime_dev_session_path(session)));
    output += ",\"request_id\":";
    if (response.request_id.empty()) output += "null"; else append_json_string(output, response.request_id);
    output += ",\"entity_id\":";
    if (entity_id.empty()) output += "null"; else append_json_string(output, entity_id);
    output += ",\"payload\":";
    if (response.payload.empty()) output += "null"; else append_json_string(output, response.payload);
    output += "}}\n";
    return output;
}

} // namespace

CommandResult execute_runtime_debug(
    const std::filesystem::path& runtime_session_path,
    const std::string_view entity_id_text
) {
    const auto normalized = runtime_session_path.lexically_normal();
    const auto id = normalized.filename().string();
    const auto root = normalized.parent_path();
    const runtime::RuntimeDevSession session{
        .id = id,
        .root = root,
    };
    if (root.empty() || !runtime::valid_runtime_dev_session_id(id)) {
        runtime::RuntimeDebugResponse response{
            .state = runtime::RuntimeDebugResponseState::invalid,
            .diagnostics = "Runtime session path is invalid.",
        };
        return CommandResult{
            .exit_code = exit_validation,
            .json = debug_response(false, {response.diagnostics}, session, entity_id_text, response),
        };
    }

    const auto entity_id = world::PersistentId::parse(entity_id_text);
    if (!entity_id.has_value() || entity_id->is_nil()) {
        runtime::RuntimeDebugResponse response{
            .state = runtime::RuntimeDebugResponseState::invalid,
            .diagnostics = "Runtime debug entity ID must be a non-nil persistent ID.",
        };
        return CommandResult{
            .exit_code = exit_validation,
            .json = debug_response(false, {response.diagnostics}, session, entity_id_text, response),
        };
    }

    const auto request_id = world::PersistentId::random().to_string();
    std::string diagnostics;
    if (!runtime::publish_runtime_debug_request(
            session,
            request_id,
            *entity_id,
            &diagnostics
        )) {
        runtime::RuntimeDebugResponse response{
            .state = runtime::RuntimeDebugResponseState::disconnected,
            .request_id = request_id,
            .diagnostics = diagnostics,
        };
        return CommandResult{
            .exit_code = exit_unavailable,
            .json = debug_response(false, {diagnostics}, session, entity_id_text, response),
        };
    }

    auto response = runtime::wait_for_runtime_debug_response(
        session,
        request_id,
        std::chrono::seconds{2}
    );
    std::vector<std::string> response_diagnostics;
    if (!response.diagnostics.empty()) response_diagnostics.push_back(response.diagnostics);
    if (response.state == runtime::RuntimeDebugResponseState::available) {
        return CommandResult{
            .exit_code = exit_success,
            .json = debug_response(true, response_diagnostics, session, entity_id_text, response),
        };
    }

    return CommandResult{
        .exit_code = response.state == runtime::RuntimeDebugResponseState::invalid
            ? exit_validation
            : exit_unavailable,
        .json = debug_response(false, response_diagnostics, session, entity_id_text, response),
    };
}

} // namespace vice::dev
