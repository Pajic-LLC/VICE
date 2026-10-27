#include <vice/platform/file.hpp>

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <atomic>

namespace vice::platform {
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) (void)CloseHandle(value); }
};
struct TemporaryFile {
    std::filesystem::path path;
    ~TemporaryFile() {
        if (!path.empty()) { std::error_code error; std::filesystem::remove(path, error); }
    }
};
FileReplaceResult unavailable(const std::string_view operation) {
    return {FileReplaceState::unavailable,
        std::string{operation} + " (Windows error " + std::to_string(GetLastError()) + ")."};
}
} // namespace

std::string text_revision(const std::string_view contents) {
    if (contents.size() > max_text_file_bytes) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    std::array<unsigned char, 32> digest{};
    const auto status = BCryptHash(algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(contents.data())),
        static_cast<ULONG>(contents.size()), digest.data(), static_cast<ULONG>(digest.size()));
    (void)BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return {};
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
    lock_path += L".vice-lock";
    Handle lock{CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr)};
    if (lock.value == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_SHARING_VIOLATION) {
            return {FileReplaceState::busy, "Another source publication is in progress."};
        }
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

    static std::atomic<unsigned long long> sequence{0};
    TemporaryFile temporary;
    Handle output;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        temporary.path = path;
        temporary.path += L".vice-tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(sequence.fetch_add(1));
        output.value = CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output.value != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) {
            temporary.path.clear();
            return unavailable("Could not create source publication file");
        }
        // A collision belongs to another operation and must never be removed.
        temporary.path.clear();
    }
    if (output.value == INVALID_HANDLE_VALUE) return unavailable("Could not reserve source publication file");
    DWORD written = 0;
    if (!WriteFile(output.value, replacement.data(), static_cast<DWORD>(replacement.size()), &written, nullptr) ||
        written != replacement.size() || !FlushFileBuffers(output.value)) {
        (void)CloseHandle(output.value);
        output.value = INVALID_HANDLE_VALUE;
        return unavailable("Could not write or flush source publication file");
    }
    if (!CloseHandle(output.value)) {
        output.value = INVALID_HANDLE_VALUE;
        return unavailable("Could not close source publication file");
    }
    output.value = INVALID_HANDLE_VALUE;
    if (expected) {
        const auto latest = read_text_file(path);
        if (!latest) return {FileReplaceState::unavailable, latest.diagnostics};
        if (latest.contents != *expected) return {FileReplaceState::conflict, "Source changed before publication."};
        if (!ReplaceFileW(path.c_str(), temporary.path.c_str(), nullptr, 0, nullptr, nullptr)) {
            return unavailable("Could not replace source file");
        }
    } else if (!MoveFileExW(temporary.path.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) {
        if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_FILE_EXISTS) {
            return {FileReplaceState::conflict, "Source already exists."};
        }
        return unavailable("Could not publish new source file");
    }
    temporary.path.clear();
    return {FileReplaceState::updated, {}};
}
} // namespace vice::platform
