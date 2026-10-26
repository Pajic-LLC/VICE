#include <vice/runtime/dev_session.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

constexpr std::string_view session_manifest_name = "session.json";
constexpr std::string_view snapshot_prefix = "snapshot-";
constexpr std::string_view snapshot_suffix = ".json";
constexpr std::size_t snapshot_generation_width = 20U;
constexpr std::uintmax_t session_manifest_max_bytes = 4096U;

enum class PreparedState : std::uint8_t {
    ready,
    disconnected,
    invalid,
};

struct PreparedResult {
    PreparedState state = PreparedState::disconnected;
    std::string diagnostics;
};

[[nodiscard]] std::string expected_session_manifest(const RuntimeDevSession& session) {
    return "{\"schema\":\"" + std::string{runtime_dev_session_schema} +
        "\",\"session_id\":\"" + session.id + "\"}\n";
}

[[nodiscard]] std::filesystem::path session_manifest_path(const RuntimeDevSession& session) {
    return runtime_dev_session_path(session) / session_manifest_name;
}

[[nodiscard]] std::string snapshot_filename(const std::uint64_t generation) {
    std::array<char, 32> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), generation);
    if (converted.ec != std::errc{}) return {};

    const std::string digits{buffer.data(), converted.ptr};
    if (digits.size() > snapshot_generation_width) return {};

    std::string name;
    name.reserve(snapshot_prefix.size() + snapshot_generation_width + snapshot_suffix.size());
    name += snapshot_prefix;
    name.append(snapshot_generation_width - digits.size(), '0');
    name += digits;
    name += snapshot_suffix;
    return name;
}

[[nodiscard]] std::optional<std::uint64_t> snapshot_generation(const std::filesystem::path& path) {
    const auto filename = path.filename().string();
    const auto expected_size = snapshot_prefix.size() + snapshot_generation_width + snapshot_suffix.size();
    if (filename.size() != expected_size ||
        !filename.starts_with(snapshot_prefix) ||
        !filename.ends_with(snapshot_suffix)) {
        return std::nullopt;
    }

    const auto digits = std::string_view{filename}.substr(snapshot_prefix.size(), snapshot_generation_width);
    std::uint64_t value = 0U;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool read_small_text(
    const std::filesystem::path& path,
    const std::uintmax_t max_bytes,
    std::string& value,
    std::string& diagnostics
) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        diagnostics = "Could not inspect `" + path.generic_string() + "`: " + error.message();
        return false;
    }
    if (size > max_bytes) {
        diagnostics = "File exceeds the VICE runtime development transport limit: `" +
            path.generic_string() + "`.";
        return false;
    }

    std::ifstream input{path, std::ios::binary};
    if (!input) {
        diagnostics = "Could not open `" + path.generic_string() + "`.";
        return false;
    }

    value.resize(static_cast<std::size_t>(size));
    if (size != 0U) {
        input.read(value.data(), static_cast<std::streamsize>(size));
        if (input.gcount() != static_cast<std::streamsize>(size)) {
            diagnostics = "Could not read complete runtime development data from `" +
                path.generic_string() + "`.";
            return false;
        }
    }
    diagnostics.clear();
    return true;
}

[[nodiscard]] PreparedResult check_prepared_session(const RuntimeDevSession& session) {
    if (!valid_runtime_dev_session_id(session.id)) {
        return PreparedResult{
            .state = PreparedState::invalid,
            .diagnostics = "Runtime development session ID is invalid.",
        };
    }
    if (session.root.empty()) {
        return PreparedResult{
            .state = PreparedState::invalid,
            .diagnostics = "Runtime development session root is empty.",
        };
    }

    const auto directory = runtime_dev_session_path(session);
    std::error_code error;
    const bool exists = std::filesystem::exists(directory, error);
    if (error) {
        return PreparedResult{
            .state = PreparedState::invalid,
            .diagnostics = "Could not inspect runtime development session `" +
                directory.generic_string() + "`: " + error.message(),
        };
    }
    if (!exists) {
        return PreparedResult{
            .state = PreparedState::disconnected,
            .diagnostics = "Runtime development session is not connected.",
        };
    }
    if (!std::filesystem::is_directory(directory, error) || error) {
        return PreparedResult{
            .state = PreparedState::invalid,
            .diagnostics = "Runtime development session path is not a directory: `" +
                directory.generic_string() + "`.",
        };
    }

    const auto manifest_path = session_manifest_path(session);
    if (!std::filesystem::is_regular_file(manifest_path, error) || error) {
        return PreparedResult{
            .state = PreparedState::invalid,
            .diagnostics = "Runtime development session metadata is missing or invalid at `" +
                manifest_path.generic_string() + "`.",
        };
    }

    std::string manifest;
    std::string diagnostics;
    if (!read_small_text(manifest_path, session_manifest_max_bytes, manifest, diagnostics)) {
        return PreparedResult{
            .state = PreparedState::invalid,
            .diagnostics = std::move(diagnostics),
        };
    }
    if (manifest != expected_session_manifest(session)) {
        return PreparedResult{
            .state = PreparedState::invalid,
            .diagnostics = "Runtime development session identity does not match `" +
                manifest_path.generic_string() + "`.",
        };
    }

    return PreparedResult{.state = PreparedState::ready};
}

[[nodiscard]] RuntimeDevSnapshot state_snapshot(
    const PreparedResult& prepared
) {
    RuntimeDevSnapshot result;
    result.state = prepared.state == PreparedState::invalid
        ? RuntimeDevSnapshotState::invalid
        : RuntimeDevSnapshotState::disconnected;
    result.diagnostics = prepared.diagnostics;
    return result;
}

void prune_snapshot_history(const std::filesystem::path& directory) {
    std::vector<std::pair<std::uint64_t, std::filesystem::path>> snapshots;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator{directory, error}, end;
         !error && iterator != end;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || error) break;
        const auto generation = snapshot_generation(iterator->path());
        if (generation.has_value()) snapshots.emplace_back(*generation, iterator->path());
    }
    if (error) return;

    std::sort(snapshots.begin(), snapshots.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first > rhs.first;
    });
    for (std::size_t index = runtime_dev_snapshot_history; index < snapshots.size(); ++index) {
        std::filesystem::remove(snapshots[index].second, error);
        error.clear();
    }
}

} // namespace

bool valid_runtime_dev_session_id(const std::string_view id) noexcept {
    if (id.empty() || id.size() > 64U) return false;
    for (const char character : id) {
        const bool alpha_numeric =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9');
        if (!alpha_numeric && character != '-' && character != '_' && character != '.') return false;
    }
    return id != "." && id != "..";
}

std::filesystem::path runtime_dev_session_path(const RuntimeDevSession& session) {
    if (session.root.empty() || !valid_runtime_dev_session_id(session.id)) return {};
    return (session.root / session.id).lexically_normal();
}

bool prepare_runtime_dev_session(
    const RuntimeDevSession& session,
    std::string* const diagnostics
) {
    if (!valid_runtime_dev_session_id(session.id)) {
        if (diagnostics != nullptr) *diagnostics = "Runtime development session ID is invalid.";
        return false;
    }
    if (session.root.empty()) {
        if (diagnostics != nullptr) *diagnostics = "Runtime development session root is empty.";
        return false;
    }

    const auto directory = runtime_dev_session_path(session);
    std::error_code error;
    const bool existed = std::filesystem::exists(directory, error);
    if (error) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not inspect runtime development session `" +
                directory.generic_string() + "`: " + error.message();
        }
        return false;
    }

    if (existed) {
        const auto prepared = check_prepared_session(session);
        if (prepared.state == PreparedState::ready) {
            if (diagnostics != nullptr) diagnostics->clear();
            return true;
        }

        bool empty = std::filesystem::is_directory(directory, error) && !error &&
            std::filesystem::is_empty(directory, error) && !error;
        if (!empty) {
            if (diagnostics != nullptr) *diagnostics = prepared.diagnostics;
            return false;
        }
    } else {
        std::filesystem::create_directories(directory, error);
        if (error) {
            if (diagnostics != nullptr) {
                *diagnostics = "Could not create runtime development session `" +
                    directory.generic_string() + "`: " + error.message();
            }
            return false;
        }
    }

    const auto manifest = session_manifest_path(session);
    auto temporary_manifest = manifest;
    temporary_manifest += ".tmp";
    std::filesystem::remove(temporary_manifest, error);
    error.clear();

    std::ofstream output{temporary_manifest, std::ios::binary | std::ios::trunc};
    if (!output) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not create runtime development session metadata `" +
                temporary_manifest.generic_string() + "`.";
        }
        return false;
    }
    output << expected_session_manifest(session);
    output.close();
    if (!output) {
        std::filesystem::remove(temporary_manifest, error);
        if (diagnostics != nullptr) {
            *diagnostics = "Could not write runtime development session metadata `" +
                temporary_manifest.generic_string() + "`.";
        }
        return false;
    }

    std::filesystem::rename(temporary_manifest, manifest, error);
    if (error) {
        std::filesystem::remove(temporary_manifest, error);
        if (diagnostics != nullptr) {
            *diagnostics = "Could not publish runtime development session metadata `" +
                manifest.generic_string() + "`.";
        }
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool publish_runtime_dev_snapshot(
    const RuntimeDevSession& session,
    const std::uint64_t generation,
    const std::string_view payload,
    std::string* const diagnostics
) {
    const auto prepared = check_prepared_session(session);
    if (prepared.state != PreparedState::ready) {
        if (diagnostics != nullptr) *diagnostics = prepared.diagnostics;
        return false;
    }
    if (generation == 0U) {
        if (diagnostics != nullptr) *diagnostics = "Runtime development snapshot generation must be non-zero.";
        return false;
    }
    if (payload.empty()) {
        if (diagnostics != nullptr) *diagnostics = "Runtime development snapshot payload is empty.";
        return false;
    }
    if (payload.size() > runtime_dev_snapshot_max_bytes) {
        if (diagnostics != nullptr) *diagnostics = "Runtime development snapshot exceeds the bounded message size.";
        return false;
    }

    const auto name = snapshot_filename(generation);
    if (name.empty()) {
        if (diagnostics != nullptr) *diagnostics = "Runtime development snapshot generation is invalid.";
        return false;
    }

    const auto directory = runtime_dev_session_path(session);
    const auto final_path = directory / name;
    std::error_code error;
    if (std::filesystem::exists(final_path, error)) {
        if (diagnostics != nullptr) {
            *diagnostics = error
                ? "Could not inspect runtime development snapshot `" + final_path.generic_string() + "`."
                : "Runtime development snapshot generation has already been published.";
        }
        return false;
    }
    if (error) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not inspect runtime development snapshot `" +
                final_path.generic_string() + "`: " + error.message();
        }
        return false;
    }

    auto temporary_path = final_path;
    temporary_path += ".tmp";
    std::filesystem::remove(temporary_path, error);
    error.clear();

    std::ofstream output{temporary_path, std::ios::binary | std::ios::trunc};
    if (!output) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not create runtime development snapshot `" +
                temporary_path.generic_string() + "`.";
        }
        return false;
    }
    output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    output.close();
    if (!output) {
        std::filesystem::remove(temporary_path, error);
        if (diagnostics != nullptr) {
            *diagnostics = "Could not write runtime development snapshot `" +
                temporary_path.generic_string() + "`.";
        }
        return false;
    }

    std::filesystem::rename(temporary_path, final_path, error);
    if (error) {
        std::filesystem::remove(temporary_path, error);
        if (diagnostics != nullptr) {
            *diagnostics = "Could not publish runtime development snapshot `" +
                final_path.generic_string() + "`: " + error.message();
        }
        return false;
    }

    prune_snapshot_history(directory);
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeDevSnapshot read_latest_runtime_dev_snapshot(const RuntimeDevSession& session) {
    const auto prepared = check_prepared_session(session);
    if (prepared.state != PreparedState::ready) return state_snapshot(prepared);

    const auto directory = runtime_dev_session_path(session);
    std::optional<std::pair<std::uint64_t, std::filesystem::path>> latest;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator{directory, error}, end;
         !error && iterator != end;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || error) break;
        const auto generation = snapshot_generation(iterator->path());
        if (!generation.has_value()) continue;
        if (!latest.has_value() || *generation > latest->first) {
            latest = std::pair{*generation, iterator->path()};
        }
    }
    if (error) {
        return RuntimeDevSnapshot{
            .state = RuntimeDevSnapshotState::invalid,
            .diagnostics = "Could not enumerate runtime development snapshots: " + error.message(),
        };
    }
    if (!latest.has_value()) {
        return RuntimeDevSnapshot{
            .state = RuntimeDevSnapshotState::disconnected,
            .diagnostics = "Runtime development session has not published a snapshot yet.",
        };
    }

    std::string payload;
    std::string diagnostics;
    if (!read_small_text(latest->second, runtime_dev_snapshot_max_bytes, payload, diagnostics)) {
        return RuntimeDevSnapshot{
            .state = RuntimeDevSnapshotState::invalid,
            .generation = latest->first,
            .diagnostics = std::move(diagnostics),
        };
    }

    return RuntimeDevSnapshot{
        .state = RuntimeDevSnapshotState::available,
        .generation = latest->first,
        .payload = std::move(payload),
    };
}

RuntimeDevSnapshot wait_for_runtime_dev_snapshot(
    const RuntimeDevSession& session,
    const std::uint64_t after_generation,
    const std::chrono::milliseconds timeout,
    const std::chrono::milliseconds poll_interval
) {
    if (timeout.count() < 0 || poll_interval.count() <= 0) {
        return RuntimeDevSnapshot{
            .state = RuntimeDevSnapshotState::invalid,
            .diagnostics = "Runtime development snapshot timeout or polling interval is invalid.",
        };
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::uint64_t observed_generation = 0U;
    while (true) {
        auto snapshot = read_latest_runtime_dev_snapshot(session);
        if (snapshot.state == RuntimeDevSnapshotState::invalid) return snapshot;
        if (snapshot.state == RuntimeDevSnapshotState::available) {
            observed_generation = snapshot.generation;
            if (snapshot.generation > after_generation) return snapshot;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return RuntimeDevSnapshot{
                .state = RuntimeDevSnapshotState::timed_out,
                .generation = observed_generation,
                .diagnostics = "Timed out waiting for a newer runtime development snapshot.",
            };
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        std::this_thread::sleep_for(std::min(poll_interval, remaining));
    }
}

bool clear_runtime_dev_session(
    const RuntimeDevSession& session,
    std::string* const diagnostics
) {
    if (!valid_runtime_dev_session_id(session.id) || session.root.empty()) {
        if (diagnostics != nullptr) *diagnostics = "Runtime development session identity is invalid.";
        return false;
    }

    const auto directory = runtime_dev_session_path(session);
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) {
        if (error) {
            if (diagnostics != nullptr) *diagnostics = "Could not inspect runtime development session: " + error.message();
            return false;
        }
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    const auto prepared = check_prepared_session(session);
    if (prepared.state != PreparedState::ready) {
        if (diagnostics != nullptr) *diagnostics = prepared.diagnostics;
        return false;
    }

    std::filesystem::remove_all(directory, error);
    if (error) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not clear runtime development session `" +
                directory.generic_string() + "`: " + error.message();
        }
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::runtime
