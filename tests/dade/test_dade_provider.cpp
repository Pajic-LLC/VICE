#include <vice/runtime/dade_provider_api.hpp>

#include <cstring>

extern "C" VICE_DADE_PROVIDER_EXPORT bool vice_dade_verify(
    const std::uint32_t host_abi_version,
    const vice::runtime::dade::VerifyRequest* const request,
    vice::runtime::dade::VerifyResult* const result
) noexcept {
    using namespace vice::runtime::dade;

    if (host_abi_version != abi_version || request == nullptr || result == nullptr ||
        request->abi_version != abi_version || request->size < sizeof(VerifyRequest) ||
        request->asset_id == nullptr || request->platform == nullptr || request->project_root == nullptr) {
        return false;
    }

    *result = VerifyResult{};
    if (std::strcmp(request->asset_id, "com.pajic.vice") == 0) {
        result->code = DecisionCode::owned;
        result->detail = "test deed is owned";
    } else {
        result->code = DecisionCode::wrong_asset;
        result->detail = "test deed belongs to another asset";
    }
    return true;
}
