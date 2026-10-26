#include <vice/platform/file.hpp>

#include <windows.h>

#include <system_error>

namespace vice::platform {

AtomicReplaceResult atomic_replace_file(
    const std::filesystem::path& staged,
    const std::filesystem::path& destination
) {
    if (staged.empty() || destination.empty()) {
        return AtomicReplaceResult{
            .success = false,
            .diagnostics = "Atomic file replacement requires non-empty paths.",
        };
    }

    if (MoveFileExW(
            staged.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        ) == FALSE) {
        const auto error = std::error_code{
            static_cast<int>(GetLastError()),
            std::system_category(),
        };
        return AtomicReplaceResult{
            .success = false,
            .diagnostics = "Could not atomically replace file: " + error.message(),
        };
    }

    return AtomicReplaceResult{.success = true};
}

} // namespace vice::platform
