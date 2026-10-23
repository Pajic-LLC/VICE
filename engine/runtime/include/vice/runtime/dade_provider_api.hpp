#pragma once

#include <cstddef>
#include <cstdint>

namespace vice::runtime::dade {

inline constexpr std::uint32_t abi_version = 1;
inline constexpr const char* entrypoint_name = "vice_dade_verify";

enum class DecisionCode : std::uint32_t {
    owned = 0,
    not_owned,
    wrong_asset,
    invalid_deed,
    untrusted_issuer,
    owner_proof_failed,
    device_not_authorized,
    device_proof_failed,
    controller_revoked,
    issuer_key_revoked,
    trust_state_rollback,
    right_not_granted,
    provider_error,
};

struct VerifyRequest {
    std::uint32_t abi_version = dade::abi_version;
    std::size_t size = sizeof(VerifyRequest);
    const char* asset_id = nullptr;
    const char* platform = nullptr;
    const char* project_root = nullptr;
};

struct VerifyResult {
    std::uint32_t abi_version = dade::abi_version;
    std::size_t size = sizeof(VerifyResult);
    DecisionCode code = DecisionCode::provider_error;
    const char* detail = nullptr;
};

using VerifyFunction = bool (*)(
    std::uint32_t host_abi_version,
    const VerifyRequest* request,
    VerifyResult* result
) noexcept;

} // namespace vice::runtime::dade
