#include <vice/dev/cli.hpp>

#include <vice/runtime/debug_session.hpp>
#include <vice/runtime/checkpoint.hpp>
#include <vice/platform/file.hpp>
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

CommandResult execute_runtime_checkpoint(
    const std::filesystem::path& session_path, const std::filesystem::path& output_path
) {
    const auto normalized = session_path.lexically_normal();
    const runtime::RuntimeDevSession session{.id=normalized.filename().string(), .root=normalized.parent_path()};
    runtime::RuntimeDebugResponse response{.state=runtime::RuntimeDebugResponseState::invalid};
    auto finish = [&](const bool ok, const std::string& message, const int code) {
        auto json = debug_response(ok, message.empty() ? std::vector<std::string>{} :
            std::vector<std::string>{message}, session, {}, response);
        json.replace(json.find("runtime.debug"), std::string_view{"runtime.debug"}.size(), "runtime.checkpoint");
        // Output path is explicit and JSON escaped, including on failure.
        json.erase(json.size()-3U);
        json += ",\"snapshot_path\":";
        append_json_string(json, normalized_path(output_path));
        json += "}}\n";
        return CommandResult{.exit_code=code, .json=std::move(json)};
    };
    std::error_code error;
    const auto parent = std::filesystem::canonical(output_path.has_parent_path() ?
        output_path.parent_path() : std::filesystem::path{"."}, error);
    if (error || output_path.filename().empty() || session.root.empty() ||
        !runtime::valid_runtime_dev_session_id(session.id))
        return finish(false, "Checkpoint session or output path is invalid.", exit_validation);
    const auto destination = parent / output_path.filename();
    if (std::filesystem::exists(destination, error) || error)
        return finish(false, "Checkpoint output must be a new file.", exit_validation);
    response.request_id = world::PersistentId::random().to_string();
    std::string diagnostics;
    if (!runtime::publish_runtime_checkpoint_request(session, response.request_id, &diagnostics))
        return finish(false, diagnostics, exit_unavailable);
    response = runtime::wait_for_runtime_debug_response(session, response.request_id, std::chrono::seconds{10});
    if (!response) return finish(false, response.diagnostics, exit_unavailable);
    const auto snapshot = runtime::read_runtime_world_checkpoint(session, response.payload);
    if (!snapshot) return finish(false, snapshot.diagnostics, exit_validation);
    const auto saved = platform::replace_file_if_unchanged(destination, std::nullopt, snapshot.bytes);
    if (saved.state != platform::FileReplaceState::updated)
        return finish(false, saved.diagnostics, exit_validation);
    return finish(true, {}, exit_success);
}

} // namespace vice::dev
