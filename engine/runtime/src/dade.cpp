#include <vice/runtime/dade.hpp>

#include <vice/platform/dynamic_library.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace vice::runtime {
namespace {

[[nodiscard]] bool ascii_alphanumeric(const char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9');
}

[[nodiscard]] std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](const unsigned char byte) {
        return std::isspace(byte) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](const unsigned char byte) {
        return std::isspace(byte) != 0;
    }).base();
    if (first >= last) return {};
    return std::string{first, last};
}

} // namespace

struct DadeProvider::Impl {
    std::unique_ptr<platform::DynamicLibrary> library;
    dade::VerifyFunction verify = nullptr;
};

DadeProvider::DadeProvider(std::unique_ptr<Impl> impl) noexcept
    : impl_{std::move(impl)} {}

DadeProvider::~DadeProvider() = default;
DadeProvider::DadeProvider(DadeProvider&&) noexcept = default;
DadeProvider& DadeProvider::operator=(DadeProvider&&) noexcept = default;

bool valid_dade_asset_id(const std::string_view asset_id) noexcept {
    if (asset_id.empty() || asset_id.size() > 255U || !ascii_alphanumeric(asset_id.front())) {
        return false;
    }
    return std::all_of(asset_id.begin(), asset_id.end(), [](const char value) {
        return ascii_alphanumeric(value) || value == '.' || value == '-' || value == '_';
    });
}

DadeRequirement load_dade_requirement(const std::filesystem::path& manifest_path) {
    DadeRequirement result;
    if (manifest_path.empty()) {
        result.diagnostics = "Project manifest path is empty.";
        return result;
    }

    const auto path = manifest_path.parent_path() / "dade.asset";
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        result.diagnostics = "Could not inspect DADE requirement: " + error.message();
        return result;
    }
    if (!exists) return result;

    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.diagnostics = "Could not open DADE requirement: " + path.generic_string();
        return result;
    }

    std::string line;
    while (std::getline(input, line)) {
        std::string value = trim(std::move(line));
        if (value.empty() || value.front() == '#') continue;
        if (!result.asset_id.empty()) {
            result.diagnostics = "DADE requirement must contain one asset ID.";
            return result;
        }
        result.asset_id = std::move(value);
    }
    if (!input.eof() && input.fail()) {
        result.diagnostics = "Could not read DADE requirement: " + path.generic_string();
        return result;
    }
    if (!valid_dade_asset_id(result.asset_id)) {
        result.diagnostics = "DADE requirement contains an invalid asset ID.";
        return result;
    }

    result.required = true;
    return result;
}

std::filesystem::path staged_dade_provider_path(const std::filesystem::path& executable) {
    if (executable.empty()) return {};
    return executable.parent_path() /
        "modules" /
        (std::string{"dade_provider"} + std::string{platform::dynamic_library_extension()});
}

DadeProviderLoadResult load_dade_provider(const std::filesystem::path& path) {
    DadeProviderLoadResult result;
    if (path.empty()) {
        result.diagnostics = "DADE provider path is empty.";
        return result;
    }

    std::string diagnostics;
    auto library = platform::DynamicLibrary::load(path, &diagnostics);
    if (!library) {
        result.diagnostics = "Could not load DADE provider `" + path.generic_string() + "`: " + diagnostics;
        return result;
    }

    const std::uintptr_t address = library->symbol(dade::entrypoint_name, &diagnostics);
    if (address == 0U) {
        result.diagnostics = "DADE provider `" + path.generic_string() + "` is missing `" +
            std::string{dade::entrypoint_name} + "`: " + diagnostics;
        return result;
    }

    auto impl = std::make_unique<DadeProvider::Impl>();
    impl->verify = reinterpret_cast<dade::VerifyFunction>(address);
    impl->library = std::move(library);
    result.provider = std::unique_ptr<DadeProvider>{new DadeProvider{std::move(impl)}};
    return result;
}

DadeDecision DadeProvider::verify(
    const std::string_view asset_id,
    const std::string_view platform,
    const std::filesystem::path& project_root
) const {
    DadeDecision decision;
    if (impl_ == nullptr || impl_->verify == nullptr) {
        decision.detail = "DADE provider is not initialized.";
        return decision;
    }
    if (!valid_dade_asset_id(asset_id)) {
        decision.detail = "DADE asset ID is invalid.";
        return decision;
    }

    const std::string asset_text{asset_id};
    const std::string platform_text{platform};
    const std::string project_text = project_root.generic_string();
    const dade::VerifyRequest request{
        .asset_id = asset_text.c_str(),
        .platform = platform_text.c_str(),
        .project_root = project_text.c_str(),
    };
    dade::VerifyResult result{};

    if (!impl_->verify(dade::abi_version, &request, &result)) {
        decision.detail = "DADE provider could not complete ownership verification.";
        return decision;
    }
    if (result.abi_version != dade::abi_version || result.size < sizeof(dade::VerifyResult)) {
        decision.detail = "DADE provider returned an incompatible result.";
        return decision;
    }

    decision.code = result.code;
    decision.allowed = result.code == dade::DecisionCode::owned;
    decision.detail = result.detail != nullptr ? result.detail : "";
    return decision;
}

std::string_view dade_decision_code(const dade::DecisionCode code) noexcept {
    switch (code) {
        case dade::DecisionCode::owned: return "owned";
        case dade::DecisionCode::not_owned: return "not_owned";
        case dade::DecisionCode::wrong_asset: return "wrong_asset";
        case dade::DecisionCode::invalid_deed: return "invalid_deed";
        case dade::DecisionCode::untrusted_issuer: return "untrusted_issuer";
        case dade::DecisionCode::owner_proof_failed: return "owner_proof_failed";
        case dade::DecisionCode::device_not_authorized: return "device_not_authorized";
        case dade::DecisionCode::device_proof_failed: return "device_proof_failed";
        case dade::DecisionCode::controller_revoked: return "controller_revoked";
        case dade::DecisionCode::issuer_key_revoked: return "issuer_key_revoked";
        case dade::DecisionCode::trust_state_rollback: return "trust_state_rollback";
        case dade::DecisionCode::right_not_granted: return "right_not_granted";
        case dade::DecisionCode::provider_error: return "provider_error";
    }
    return "provider_error";
}

} // namespace vice::runtime
