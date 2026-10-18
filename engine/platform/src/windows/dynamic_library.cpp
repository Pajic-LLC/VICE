#include <vice/platform/dynamic_library.hpp>

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace vice::platform {
namespace {

[[nodiscard]] std::string windows_error(const DWORD code) {
    wchar_t* message = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<wchar_t*>(&message),
        0,
        nullptr
    );
    if (size == 0U || message == nullptr) {
        return "Windows error " + std::to_string(code) + ".";
    }

    const std::wstring_view wide{message, size};
    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.data(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    std::string result;
    if (required > 0) {
        result.resize(static_cast<std::size_t>(required));
        (void)WideCharToMultiByte(
            CP_UTF8,
            0,
            wide.data(),
            static_cast<int>(wide.size()),
            result.data(),
            required,
            nullptr,
            nullptr
        );
        while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
            result.pop_back();
        }
    }
    LocalFree(message);
    return result.empty() ? "Windows error " + std::to_string(code) + "." : result;
}

} // namespace

struct DynamicLibrary::Impl {
    HMODULE handle = nullptr;

    ~Impl() {
        if (handle != nullptr) {
            (void)FreeLibrary(handle);
        }
    }
};

DynamicLibrary::DynamicLibrary(std::unique_ptr<Impl> impl) noexcept
    : impl_{std::move(impl)} {}

DynamicLibrary::~DynamicLibrary() = default;
DynamicLibrary::DynamicLibrary(DynamicLibrary&&) noexcept = default;
DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&&) noexcept = default;

std::unique_ptr<DynamicLibrary> DynamicLibrary::load(
    const std::filesystem::path& path,
    std::string* const diagnostics
) {
    if (path.empty()) {
        if (diagnostics != nullptr) *diagnostics = "Dynamic library path is empty.";
        return nullptr;
    }

    HMODULE const handle = LoadLibraryW(path.c_str());
    if (handle == nullptr) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not load dynamic library: " + windows_error(GetLastError());
        }
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->handle = handle;
    if (diagnostics != nullptr) diagnostics->clear();
    return std::unique_ptr<DynamicLibrary>{new DynamicLibrary{std::move(impl)}};
}

std::uintptr_t DynamicLibrary::symbol(
    const std::string_view name,
    std::string* const diagnostics
) const noexcept {
    if (impl_ == nullptr || impl_->handle == nullptr) {
        if (diagnostics != nullptr) *diagnostics = "Dynamic library is not loaded.";
        return 0U;
    }
    if (name.empty()) {
        if (diagnostics != nullptr) *diagnostics = "Dynamic library symbol name is empty.";
        return 0U;
    }

    const std::string owned_name{name};
    FARPROC const address = GetProcAddress(impl_->handle, owned_name.c_str());
    if (address == nullptr) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not resolve dynamic library symbol `" + owned_name + "`: " +
                windows_error(GetLastError());
        }
        return 0U;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return reinterpret_cast<std::uintptr_t>(address);
}

std::string_view dynamic_library_extension() noexcept {
    return ".dll";
}

} // namespace vice::platform
