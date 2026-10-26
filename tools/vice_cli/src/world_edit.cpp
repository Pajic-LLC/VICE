#include <vice/dev/cli.hpp>

#include <vice/platform/file.hpp>
#include <vice/tools/world_edit.hpp>

#include <string>

namespace vice::dev {
namespace {
void append_string(std::string& output, const std::string_view text) {
    constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c < 32U) {
            output += "\\u00";
            output.push_back(hex[c >> 4U]);
            output.push_back(hex[c & 15U]);
        } else output.push_back(raw);
    }
    output.push_back('"');
}

CommandResult response(const tools::world_authoring::FileEditResult& result) {
    std::string json = "{\"schema\":\"vice.dev.v1\",\"command\":\"world.edit\",\"ok\":";
    json += result.success ? "true" : "false";
    json += ",\"diagnostics\":[";
    if (!result.diagnostics.empty()) append_string(json, result.diagnostics);
    json += "],\"data\":{\"code\":";
    append_string(json, result.code);
    json += ",\"dry_run\":";
    json += result.dry_run ? "true" : "false";
    json += ",\"changed\":";
    json += result.changed ? "true" : "false";
    json += ",\"published\":";
    json += result.published ? "true" : "false";
    json += ",\"current_revision\":";
    append_string(json, result.previous_revision);
    json += ",\"result_revision\":";
    append_string(json, result.revision);
    json += ",\"validated_operations\":" + std::to_string(result.validated_operations);
    json += ",\"failed_operation\":";
    json += result.failed_operation ? std::to_string(*result.failed_operation) : "null";
    json += ",\"affected_entities\":[";
    for (std::size_t i = 0; i < result.affected_entities.size(); ++i) {
        if (i) json.push_back(',');
        append_string(json, result.affected_entities[i].to_string());
    }
    json += "],\"affected_regions\":[";
    for (std::size_t i = 0; i < result.affected_regions.size(); ++i) {
        if (i) json.push_back(',');
        json += std::to_string(result.affected_regions[i].value);
    }
    json += "]}}\n";
    const bool unavailable = result.code == "source_unavailable" || result.code == "publication_failed" ||
        result.code == "source_busy" || result.code == "request_unavailable";
    return {result.success ? exit_success : (unavailable ? exit_unavailable : exit_validation), std::move(json)};
}
} // namespace

CommandResult execute_world_edit(
    const std::filesystem::path& world_path,
    const std::filesystem::path& request_path,
    const bool dry_run
) {
    const auto source = platform::read_text_file(request_path, tools::world_authoring::max_edit_request_bytes);
    if (!source) {
        tools::world_authoring::FileEditResult failure;
        failure.dry_run = dry_run;
        failure.code = "request_unavailable";
        failure.diagnostics = source.diagnostics;
        return response(failure);
    }
    const auto parsed = tools::world_authoring::parse_edit_request(source.contents);
    if (!parsed.request) {
        tools::world_authoring::FileEditResult failure;
        failure.dry_run = dry_run;
        failure.code = "invalid_request";
        failure.diagnostics = parsed.diagnostics;
        failure.failed_operation = parsed.failed_operation;
        return response(failure);
    }
    return response(tools::world_authoring::edit_file(world_path, *parsed.request, dry_run));
}
} // namespace vice::dev
