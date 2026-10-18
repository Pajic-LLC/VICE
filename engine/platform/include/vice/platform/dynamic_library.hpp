#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace vice::platform {

class DynamicLibrary final {
public:
    ~DynamicLibrary();

    DynamicLibrary(DynamicLibrary&&) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&&) noexcept;

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    [[nodiscard]] static std::unique_ptr<DynamicLibrary> load(
        const std::filesystem::path& path,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] std::uintptr_t symbol(
        std::string_view name,
        std::string* diagnostics = nullptr
    ) const noexcept;

private:
    struct Impl;

    explicit DynamicLibrary(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string_view dynamic_library_extension() noexcept;

} // namespace vice::platform
