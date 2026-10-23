#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace vice::runtime {

struct StagedPackageMetadata {
    bool dade_required = false;
    std::string dade_asset_id;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] bool stamp_staged_package_metadata(
    const std::filesystem::path& executable,
    bool dade_required,
    std::string_view dade_asset_id,
    std::string* diagnostics = nullptr
);

[[nodiscard]] StagedPackageMetadata load_staged_package_metadata(
    const std::filesystem::path& executable
);

} // namespace vice::runtime
