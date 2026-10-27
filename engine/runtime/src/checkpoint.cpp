#include <vice/runtime/checkpoint.hpp>
#include <vice/platform/file.hpp>
#include <filesystem>
#include <optional>
#include <span>

namespace vice::runtime {
namespace {
constexpr std::string_view prefix = "{\"schema\":\"vice.dev.runtime.checkpoint.v1\",\"sha256\":\"";
constexpr std::string_view suffix = "\"}";
constexpr std::string_view failure = "{\"schema\":\"vice.dev.runtime.checkpoint.v1\",\"error\":\"checkpoint_unavailable\"}";
std::filesystem::path checkpoint_path(const RuntimeDevSession& session) {
    std::error_code error;
    if (!valid_runtime_dev_session_id(session.id)) return {};
    const auto parent = std::filesystem::canonical(runtime_dev_session_path(session), error);
    return error ? std::filesystem::path{} : parent / "checkpoint.vworldc";
}
std::span<const std::byte> bytes(const std::string& text) {
    return std::as_bytes(std::span{text.data(), text.size()});
}
}
std::string publish_runtime_world_checkpoint(
    const RuntimeDevSession& session, const world::WorldSnapshot& snapshot,
    std::string* diagnostics
) {
    auto fail = [&](const std::string& message) {
        if (diagnostics) *diagnostics = message;
        return std::string{failure};
    };
    const auto path = checkpoint_path(session);
    if (path.empty()) return fail("Runtime checkpoint session is unavailable.");
    auto encoded = world::encode_world_snapshot(snapshot);
    if (!encoded) return fail(encoded.diagnostics);
    if (encoded.bytes.size() > platform::max_text_file_bytes)
        return fail("Runtime checkpoint exceeds the bounded 16 MiB export size.");
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) return fail(error.message());
    platform::TextFileResult previous;
    if (exists) {
        previous = platform::read_text_file(path);
        if (!previous) return fail(previous.diagnostics);
    }
    const auto result = platform::replace_file_if_unchanged(path,
        exists ? std::optional{bytes(previous.contents)} : std::nullopt, encoded.bytes);
    if (result.state != platform::FileReplaceState::updated &&
        result.state != platform::FileReplaceState::unchanged) return fail(result.diagnostics);
    const auto revision = platform::text_revision(std::string_view{
        reinterpret_cast<const char*>(encoded.bytes.data()), encoded.bytes.size()});
    if (revision.empty()) return fail("Runtime checkpoint hashing failed.");
    if (diagnostics) diagnostics->clear();
    return std::string{prefix} + revision + std::string{suffix};
}
world::WorldSnapshotEncodeResult read_runtime_world_checkpoint(
    const RuntimeDevSession& session, const std::string_view descriptor
) {
    world::WorldSnapshotEncodeResult result;
    auto fail = [&](const std::string& message) {
        result.bytes.clear(); result.diagnostics = message; return result;
    };
    if (!descriptor.starts_with(prefix) || !descriptor.ends_with(suffix))
        return fail("Runtime checkpoint response is unavailable or malformed.");
    const auto expected = descriptor.substr(prefix.size(), descriptor.size()-prefix.size()-suffix.size());
    const auto path = checkpoint_path(session);
    if (path.empty()) return fail("Runtime checkpoint session is unavailable.");
    const auto source = platform::read_text_file(path);
    if (!source) return fail(source.diagnostics);
    if (platform::text_revision(source.contents) != expected)
        return fail("Runtime checkpoint bytes no longer match this request.");
    const auto decoded = world::decode_world_snapshot(bytes(source.contents));
    if (!decoded) return fail(decoded.diagnostics);
    const auto data = bytes(source.contents);
    result.bytes.assign(data.begin(), data.end());
    return result;
}
} // namespace vice::runtime
