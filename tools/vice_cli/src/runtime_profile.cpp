#include <vice/dev/cli.hpp>

#include <vice/runtime/dev_session.hpp>

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
    const runtime::RuntimeDevSnapshotState state
) noexcept {
    switch (state) {
    case runtime::RuntimeDevSnapshotState::available: return "available";
    case runtime::RuntimeDevSnapshotState::disconnected: return "disconnected";
    case runtime::RuntimeDevSnapshotState::invalid: return "invalid";
    case runtime::RuntimeDevSnapshotState::timed_out: return "timed_out";
    }
    return "invalid";
}

[[nodiscard]] std::string profile_response(
    const bool ok,
    const std::vector<std::string>& diagnostics,
    const runtime::RuntimeDevSession& session,
    const runtime::RuntimeDevSnapshot& snapshot
) {
    std::string output;
    output.reserve(384U + snapshot.payload.size());
    output += "{\"schema\":\"vice.dev.v1\",\"command\":\"runtime.profile\",\"ok\":";
    output += ok ? "true" : "false";
    output += ",\"diagnostics\":[";
    for (std::size_t index = 0U; index < diagnostics.size(); ++index) {
        if (index != 0U) output.push_back(',');
        append_json_string(output, diagnostics[index]);
    }
    output += "],\"data\":{\"state\":";
    append_json_string(output, state_name(snapshot.state));
    output += ",\"session_id\":";
    append_json_string(output, session.id);
    output += ",\"session_path\":";
    append_json_string(output, normalized_path(runtime::runtime_dev_session_path(session)));
    output += ",\"generation\":" + std::to_string(snapshot.generation);
    output += ",\"payload\":";
    if (snapshot.payload.empty()) output += "null"; else append_json_string(output, snapshot.payload);
    output += "}}\n";
    return output;
}

} // namespace

CommandResult execute_runtime_profile(const std::filesystem::path& runtime_session_path) {
    const auto normalized = runtime_session_path.lexically_normal();
    const auto id = normalized.filename().string();
    const auto root = normalized.parent_path();
    if (root.empty() || !runtime::valid_runtime_dev_session_id(id)) {
        runtime::RuntimeDevSession invalid_session{
            .id = id,
            .root = root,
        };
        runtime::RuntimeDevSnapshot invalid_snapshot{
            .state = runtime::RuntimeDevSnapshotState::invalid,
            .diagnostics = "Runtime session path is invalid.",
        };
        return CommandResult{
            .exit_code = exit_validation,
            .json = profile_response(
                false,
                {invalid_snapshot.diagnostics},
                invalid_session,
                invalid_snapshot
            ),
        };
    }

    const runtime::RuntimeDevSession session{
        .id = id,
        .root = root,
    };
    const auto snapshot = runtime::read_latest_runtime_dev_snapshot(session);
    std::vector<std::string> diagnostics;
    if (!snapshot.diagnostics.empty()) diagnostics.push_back(snapshot.diagnostics);

    if (snapshot.state == runtime::RuntimeDevSnapshotState::available) {
        return CommandResult{
            .exit_code = exit_success,
            .json = profile_response(true, diagnostics, session, snapshot),
        };
    }

    return CommandResult{
        .exit_code = snapshot.state == runtime::RuntimeDevSnapshotState::invalid
            ? exit_validation
            : exit_unavailable,
        .json = profile_response(false, diagnostics, session, snapshot),
    };
}

} // namespace vice::dev
