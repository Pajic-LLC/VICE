#pragma once

#include <vice/runtime/dade_provider_api.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace vice::runtime {

struct DadeRequirement {
    bool required = false;
    std::string asset_id;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

struct DadeDecision {
    bool allowed = false;
    dade::DecisionCode code = dade::DecisionCode::provider_error;
    std::string detail;
};

class DadeProvider final {
public:
    ~DadeProvider();

    DadeProvider(DadeProvider&&) noexcept;
    DadeProvider& operator=(DadeProvider&&) noexcept;

    DadeProvider(const DadeProvider&) = delete;
    DadeProvider& operator=(const DadeProvider&) = delete;

    [[nodiscard]] DadeDecision verify(
        std::string_view asset_id,
        std::string_view platform,
        const std::filesystem::path& project_root
    ) const;

private:
    struct Impl;
    explicit DadeProvider(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;

    friend struct DadeProviderLoadResult;
    friend DadeProviderLoadResult load_dade_provider(const std::filesystem::path& path);
};

struct DadeProviderLoadResult {
    std::unique_ptr<DadeProvider> provider;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return provider != nullptr;
    }
};

[[nodiscard]] bool valid_dade_asset_id(std::string_view asset_id) noexcept;
[[nodiscard]] DadeRequirement load_dade_requirement(const std::filesystem::path& manifest_path);
[[nodiscard]] std::filesystem::path staged_dade_provider_path(const std::filesystem::path& executable);
[[nodiscard]] DadeProviderLoadResult load_dade_provider(const std::filesystem::path& path);
[[nodiscard]] std::string_view dade_decision_code(dade::DecisionCode code) noexcept;

} // namespace vice::runtime
