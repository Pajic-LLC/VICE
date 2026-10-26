#pragma once

#include <filesystem>
#include <string>

namespace vice::platform {

struct AtomicReplaceResult {
    bool success = false;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

// Atomically publishes `staged` as `destination` when both paths are on the
// same filesystem. On success the staged path no longer exists.
[[nodiscard]] AtomicReplaceResult atomic_replace_file(
    const std::filesystem::path& staged,
    const std::filesystem::path& destination
);

} // namespace vice::platform
