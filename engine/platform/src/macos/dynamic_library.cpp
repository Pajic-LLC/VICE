#include <vice/platform/dynamic_library.hpp>

#include <dlfcn.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace vice::platform {

struct DynamicLibrary::Impl {
    void* handle = nullptr;

    ~Impl() {
        if (handle != nullptr) {
            (void)dlclose(handle);
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

    (void)dlerror();
    void* const handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* const error = dlerror();
        if (diagnostics != nullptr) {
            *diagnostics = error != nullptr
                ? "Could not load dynamic library: " + std::string{error}
                : "Could not load dynamic library.";
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
    (void)dlerror();
    void* const address = dlsym(impl_->handle, owned_name.c_str());
    const char* const error = dlerror();
    if (error != nullptr || address == nullptr) {
        if (diagnostics != nullptr) {
            *diagnostics = error != nullptr
                ? "Could not resolve dynamic library symbol `" + owned_name + "`: " + std::string{error}
                : "Could not resolve dynamic library symbol `" + owned_name + "`.";
        }
        return 0U;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return reinterpret_cast<std::uintptr_t>(address);
}

std::string_view dynamic_library_extension() noexcept {
    return ".dylib";
}

} // namespace vice::platform
