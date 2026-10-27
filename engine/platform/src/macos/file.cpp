#include <vice/platform/file.hpp>

#include <CommonCrypto/CommonDigest.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace vice::platform {
namespace {
struct Descriptor {
    int value = -1;
    ~Descriptor() { if (value >= 0) (void)::close(value); }
};
struct TemporaryFile {
    std::filesystem::path path;
    ~TemporaryFile() {
        if (!path.empty()) { std::error_code error; std::filesystem::remove(path, error); }
    }
};
FileReplaceResult unavailable(const std::string_view operation) {
    return {FileReplaceState::unavailable, std::string{operation} + ": " + std::strerror(errno)};
}
} // namespace

std::string text_revision(const std::string_view contents) {
    if (contents.size() > max_text_file_bytes) return {};
    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
    if (CC_SHA256(contents.data(), static_cast<CC_LONG>(contents.size()), digest.data()) == nullptr) return {};
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "sha256:";
    for (const auto byte : digest) {
        result.push_back(hex[byte >> 4U]);
        result.push_back(hex[byte & 15U]);
    }
    return result;
}

FileReplaceResult replace_text_file_if_unchanged(
    const std::filesystem::path& path,
    const std::optional<std::string_view> expected,
    const std::string_view replacement
) {
    if ((expected && expected->size() > max_text_file_bytes) || replacement.size() > max_text_file_bytes) {
        return {FileReplaceState::unavailable, "Source or replacement exceeds the bounded size limit."};
    }
    auto lock_path = path;
    lock_path += ".vice-lock";
    Descriptor lock{::open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600)};
    if (lock.value < 0) return unavailable("Could not open source publication lock");
    if (::flock(lock.value, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) return {FileReplaceState::busy, "Another source publication is in progress."};
        return unavailable("Could not lock source publication");
    }
    if (expected) {
        const auto current = read_text_file(path);
        if (!current) return {FileReplaceState::unavailable, current.diagnostics};
        if (current.contents != *expected) return {FileReplaceState::conflict, "Source changed before publication."};
        if (replacement == *expected) return {FileReplaceState::unchanged, {}};
    } else {
        std::error_code error;
        if (std::filesystem::exists(path, error)) return {FileReplaceState::conflict, "Source already exists."};
        if (error) return {FileReplaceState::unavailable, error.message()};
    }

    auto pattern = path.string() + ".vice-tmp-XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    Descriptor output{::mkstemp(name.data())};
    if (output.value < 0) return unavailable("Could not create source publication file");
    TemporaryFile temporary{std::filesystem::path{name.data()}};
    struct stat original{};
    if (expected && (::stat(path.c_str(), &original) != 0 ||
        ::fchmod(output.value, original.st_mode & 0777) != 0)) {
        return unavailable("Could not preserve source permissions");
    }
    std::size_t written = 0;
    while (written < replacement.size()) {
        const auto count = ::write(output.value, replacement.data() + written, replacement.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return unavailable("Could not write source publication file");
        written += static_cast<std::size_t>(count);
    }
    if (::fsync(output.value) != 0) return unavailable("Could not flush source publication file");
    // Recheck immediately before replacement, including non-cooperating edits.
    if (expected) {
        const auto latest = read_text_file(path);
        if (!latest) return {FileReplaceState::unavailable, latest.diagnostics};
        if (latest.contents != *expected) return {FileReplaceState::conflict, "Source changed before publication."};
        if (::rename(temporary.path.c_str(), path.c_str()) != 0) return unavailable("Could not replace source file");
        temporary.path.clear();
    } else if (::link(temporary.path.c_str(), path.c_str()) != 0) {
        if (errno == EEXIST) return {FileReplaceState::conflict, "Source already exists."};
        return unavailable("Could not publish new source file");
    }
    return {FileReplaceState::updated, {}};
}
} // namespace vice::platform
