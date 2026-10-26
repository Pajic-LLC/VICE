#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace vice::platform {

inline constexpr std::size_t max_text_file_bytes = 16U * 1024U * 1024U;

struct TextFileResult {
    bool success = false;
    std::string contents;
    std::string diagnostics;
    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

[[nodiscard]] TextFileResult read_text_file(
    const std::filesystem::path& path,
    std::size_t max_bytes = max_text_file_bytes
);

// SHA-256 of the exact source bytes, including whitespace. Empty on hash failure.
[[nodiscard]] std::string text_revision(std::string_view contents);

enum class FileReplaceState { updated, unchanged, conflict, busy, unavailable };
struct FileReplaceResult {
    FileReplaceState state = FileReplaceState::unavailable;
    std::string diagnostics;
};

// Serializes cooperating writers with an OS-owned sidecar lock. Checks exact
// bytes under the lock, then replaces through a temporary file on the same volume.
// Null expected_contents means create only if absent. Callers use a canonical
// path (or canonical parent for new files) so symlink aliases share one lock.
[[nodiscard]] FileReplaceResult replace_text_file_if_unchanged(
    const std::filesystem::path& canonical_path,
    std::optional<std::string_view> expected_contents,
    std::string_view replacement
);

} // namespace vice::platform
