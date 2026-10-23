#include <vice/runtime/package_metadata.hpp>

#include <vice/runtime/dade.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

constexpr std::size_t stamp_size = 320U;
constexpr std::size_t signed_size = 280U;
constexpr std::size_t digest_offset = 280U;
constexpr std::size_t digest_size = 32U;
constexpr std::size_t end_magic_offset = 312U;
constexpr std::size_t asset_offset = 20U;
constexpr std::size_t asset_capacity = 255U;
constexpr std::uint32_t stamp_version = 1U;
constexpr std::uint32_t dade_required_flag = 1U;
constexpr std::uint32_t allowed_flags = dade_required_flag;

constexpr std::array<std::uint8_t, 8> start_magic{
    'V', 'I', 'C', 'E', 'P', 'K', 'G', '1'
};
constexpr std::array<std::uint8_t, 8> end_magic{
    '1', 'G', 'K', 'P', 'E', 'C', 'I', 'V'
};

constexpr std::array<std::uint32_t, 64> sha256_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

void write_u32_le(
    std::array<std::uint8_t, stamp_size>& bytes,
    const std::size_t offset,
    const std::uint32_t value
) noexcept {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

[[nodiscard]] std::uint32_t read_u32_le(
    const std::array<std::uint8_t, stamp_size>& bytes,
    const std::size_t offset
) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::array<std::uint8_t, digest_size> sha256(
    const std::span<const std::uint8_t> input
) {
    std::vector<std::uint8_t> padded{input.begin(), input.end()};
    const auto bit_length = static_cast<std::uint64_t>(input.size()) * 8U;
    padded.push_back(0x80U);
    while ((padded.size() % 64U) != 56U) {
        padded.push_back(0U);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));
    }

    std::array<std::uint32_t, 8> state{
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    };

    for (std::size_t block = 0; block < padded.size(); block += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16U; ++index) {
            const auto offset = block + (index * 4U);
            words[index] =
                (static_cast<std::uint32_t>(padded[offset]) << 24U) |
                (static_cast<std::uint32_t>(padded[offset + 1U]) << 16U) |
                (static_cast<std::uint32_t>(padded[offset + 2U]) << 8U) |
                static_cast<std::uint32_t>(padded[offset + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            const std::uint32_t s0 =
                std::rotr(words[index - 15U], 7) ^
                std::rotr(words[index - 15U], 18) ^
                (words[index - 15U] >> 3U);
            const std::uint32_t s1 =
                std::rotr(words[index - 2U], 17) ^
                std::rotr(words[index - 2U], 19) ^
                (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
        }

        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        std::uint32_t f = state[5];
        std::uint32_t g = state[6];
        std::uint32_t h = state[7];

        for (std::size_t index = 0; index < words.size(); ++index) {
            const std::uint32_t sum1 =
                std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const std::uint32_t choose = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + sum1 + choose + sha256_constants[index] + words[index];
            const std::uint32_t sum0 =
                std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sum0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::array<std::uint8_t, digest_size> digest{};
    for (std::size_t index = 0; index < state.size(); ++index) {
        const auto value = state[index];
        const auto offset = index * 4U;
        digest[offset] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
        digest[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
        digest[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
        digest[offset + 3U] = static_cast<std::uint8_t>(value & 0xffU);
    }
    return digest;
}

[[nodiscard]] bool read_unique_stamp(
    const std::filesystem::path& executable,
    std::array<std::uint8_t, stamp_size>& stamp,
    std::string& diagnostics
) {
    std::ifstream input{executable, std::ios::binary | std::ios::ate};
    if (!input) {
        diagnostics = "Could not open staged executable metadata: " + executable.generic_string();
        return false;
    }

    const auto end = input.tellg();
    if (end < 0 || end < static_cast<std::streamoff>(stamp_size)) {
        diagnostics = "Staged executable is missing VICE package metadata.";
        return false;
    }
    const auto size = static_cast<std::size_t>(end);
    std::vector<std::uint8_t> bytes(size);
    input.seekg(0, std::ios::beg);
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        diagnostics = "Could not read staged executable metadata.";
        return false;
    }

    std::size_t match_offset = 0U;
    std::size_t match_count = 0U;
    for (std::size_t offset = 0U; offset + stamp_size <= bytes.size(); ++offset) {
        if (!std::equal(
                start_magic.begin(),
                start_magic.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset)
            )) {
            continue;
        }
        if (!std::equal(
                end_magic.begin(),
                end_magic.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + end_magic_offset)
            )) {
            continue;
        }
        match_offset = offset;
        ++match_count;
        if (match_count > 1U) break;
    }

    if (match_count == 0U) {
        diagnostics = "Staged executable is missing VICE package metadata.";
        return false;
    }
    if (match_count != 1U) {
        diagnostics = "Staged executable contains multiple VICE package metadata records.";
        return false;
    }

    std::copy_n(
        bytes.begin() + static_cast<std::ptrdiff_t>(match_offset),
        stamp_size,
        stamp.begin()
    );
    return true;
}

} // namespace

bool stamp_staged_package_metadata(
    const std::filesystem::path& executable,
    const bool dade_required,
    const std::string_view dade_asset_id,
    std::string* diagnostics
) {
    const auto fail = [&](std::string message) {
        if (diagnostics != nullptr) *diagnostics = std::move(message);
        return false;
    };

    if (executable.empty()) {
        return fail("Staged executable path is empty.");
    }
    if (dade_required) {
        if (!valid_dade_asset_id(dade_asset_id)) {
            return fail("DADE asset ID cannot be stamped because it is invalid.");
        }
    } else if (!dade_asset_id.empty()) {
        return fail("A package without DADE cannot carry a DADE asset ID.");
    }

    std::array<std::uint8_t, stamp_size> stamp{};
    std::copy(start_magic.begin(), start_magic.end(), stamp.begin());
    write_u32_le(stamp, 8U, stamp_version);
    write_u32_le(stamp, 12U, dade_required ? dade_required_flag : 0U);
    write_u32_le(stamp, 16U, static_cast<std::uint32_t>(dade_asset_id.size()));
    for (std::size_t index = 0; index < dade_asset_id.size(); ++index) {
        stamp[asset_offset + index] = static_cast<std::uint8_t>(
            static_cast<unsigned char>(dade_asset_id[index])
        );
    }

    const auto digest = sha256(std::span<const std::uint8_t>{stamp.data(), signed_size});
    std::copy(digest.begin(), digest.end(), stamp.begin() + static_cast<std::ptrdiff_t>(digest_offset));
    std::copy(end_magic.begin(), end_magic.end(), stamp.begin() + static_cast<std::ptrdiff_t>(end_magic_offset));

    std::ofstream output{executable, std::ios::binary | std::ios::app};
    if (!output) {
        return fail("Could not open staged executable to write VICE package metadata.");
    }
    output.write(
        reinterpret_cast<const char*>(stamp.data()),
        static_cast<std::streamsize>(stamp.size())
    );
    output.flush();
    if (!output) {
        return fail("Could not write VICE package metadata to staged executable.");
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

StagedPackageMetadata load_staged_package_metadata(const std::filesystem::path& executable) {
    StagedPackageMetadata result;
    std::array<std::uint8_t, stamp_size> stamp{};
    if (!read_unique_stamp(executable, stamp, result.diagnostics)) {
        return result;
    }

    if (read_u32_le(stamp, 8U) != stamp_version) {
        result.diagnostics = "Staged executable uses an unsupported VICE package metadata version.";
        return result;
    }

    const auto expected_digest = sha256(
        std::span<const std::uint8_t>{stamp.data(), signed_size}
    );
    if (!std::equal(
            expected_digest.begin(),
            expected_digest.end(),
            stamp.begin() + static_cast<std::ptrdiff_t>(digest_offset)
        )) {
        result.diagnostics = "VICE package metadata failed its integrity check.";
        return result;
    }

    const auto flags = read_u32_le(stamp, 12U);
    if ((flags & ~allowed_flags) != 0U) {
        result.diagnostics = "VICE package metadata contains unsupported flags.";
        return result;
    }
    const auto asset_length = static_cast<std::size_t>(read_u32_le(stamp, 16U));
    if (asset_length > asset_capacity) {
        result.diagnostics = "VICE package metadata contains an invalid DADE asset length.";
        return result;
    }
    if (!std::all_of(
            stamp.begin() + static_cast<std::ptrdiff_t>(asset_offset + asset_length),
            stamp.begin() + static_cast<std::ptrdiff_t>(signed_size),
            [](const std::uint8_t byte) { return byte == 0U; }
        )) {
        result.diagnostics = "VICE package metadata contains unexpected reserved data.";
        return result;
    }

    result.dade_required = (flags & dade_required_flag) != 0U;
    result.dade_asset_id.assign(
        reinterpret_cast<const char*>(stamp.data() + asset_offset),
        asset_length
    );
    if (result.dade_required) {
        if (!valid_dade_asset_id(result.dade_asset_id)) {
            result.diagnostics = "VICE package metadata contains an invalid DADE asset ID.";
            return result;
        }
    } else if (!result.dade_asset_id.empty()) {
        result.diagnostics = "VICE package metadata carries a DADE asset without requiring DADE.";
        return result;
    }

    return result;
}

} // namespace vice::runtime
