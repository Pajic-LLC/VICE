#include <vice/platform/file.hpp>

#include <cerrno>
#include <cstring>
#include <string>

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

    if (::rename(staged.c_str(), destination.c_str()) != 0) {
        const int code = errno;
        return AtomicReplaceResult{
            .success = false,
            .diagnostics = "Could not atomically replace file: " + std::string{std::strerror(code)},
        };
    }

    return AtomicReplaceResult{.success = true};
}

} // namespace vice::platform
