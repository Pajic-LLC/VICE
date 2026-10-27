#include <vice/runtime/package_metadata.hpp>

#include <vice/platform/process.hpp>
#include <vice/runtime/dade.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vice::runtime {

bool append_staged_package_metadata_internal(
    const std::filesystem::path& executable,
    bool dade_required,
    std::string_view dade_asset_id,
    std::string* diagnostics
);

StagedPackageMetadata load_staged_package_metadata_internal(
    const std::filesystem::path& executable
);

namespace {

constexpr std::size_t stamp_size = 320U;
constexpr std::size_t end_magic_offset = 312U;
constexpr std::array<std::uint8_t, 8> placeholder_start{
    'V', 'I', 'C', 'E', 'P', 'K', 'G', '0'
};
constexpr std::array<std::uint8_t, 8> placeholder_end{
    '0', 'G', 'K', 'P', 'E', 'C', 'I', 'V'
};

[[nodiscard]] bool read_file(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& bytes,
    std::string& diagnostics
) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        diagnostics = "Could not open staged executable: " + path.generic_string();
        return false;
    }

    const auto end = input.tellg();
    if (end < 0) {
        diagnostics = "Could not read staged executable size.";
        return false;
    }

    bytes.resize(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
    }
    if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        diagnostics = "Could not read staged executable.";
        return false;
    }
    return true;
}

[[nodiscard]] bool find_package_slot(
    const std::span<const std::uint8_t> bytes,
    std::size_t& slot_offset,
    std::string& diagnostics
) {
    std::size_t count = 0U;
    for (std::size_t offset = 0U; offset + stamp_size <= bytes.size(); ++offset) {
        if (!std::equal(
                placeholder_start.begin(),
                placeholder_start.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset)
            )) {
            continue;
        }
        if (!std::equal(
                placeholder_end.begin(),
                placeholder_end.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + end_magic_offset)
            )) {
            continue;
        }
        if (!std::all_of(
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + placeholder_start.size()),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + end_magic_offset),
                [](const std::uint8_t byte) { return byte == 0U; }
            )) {
            continue;
        }

        slot_offset = offset;
        ++count;
        if (count > 1U) break;
    }

    if (count == 0U) {
        diagnostics = "Staged executable does not contain a VICE package metadata slot.";
        return false;
    }
    if (count != 1U) {
        diagnostics = "Staged executable contains multiple VICE package metadata slots.";
        return false;
    }
    return true;
}

[[nodiscard]] bool read_appended_stamp(
    const std::filesystem::path& executable,
    const std::uintmax_t original_size,
    std::array<std::uint8_t, stamp_size>& stamp,
    std::string& diagnostics
) {
    std::error_code error;
    const auto stamped_size = std::filesystem::file_size(executable, error);
    if (error) {
        diagnostics = "Could not inspect stamped executable size: " + error.message();
        return false;
    }
    if (stamped_size != original_size + stamp_size) {
        diagnostics = "VICE package metadata staging produced an unexpected executable size.";
        return false;
    }

    std::ifstream input{executable, std::ios::binary};
    if (!input) {
        diagnostics = "Could not reopen staged executable metadata.";
        return false;
    }
    input.seekg(static_cast<std::streamoff>(original_size), std::ios::beg);
    input.read(
        reinterpret_cast<char*>(stamp.data()),
        static_cast<std::streamsize>(stamp.size())
    );
    if (!input || input.gcount() != static_cast<std::streamsize>(stamp.size())) {
        diagnostics = "Could not read generated VICE package metadata.";
        return false;
    }
    return true;
}

[[nodiscard]] bool replace_slot(
    const std::filesystem::path& executable,
    const std::size_t slot_offset,
    const std::array<std::uint8_t, stamp_size>& stamp,
    const std::uintmax_t original_size,
    std::string& diagnostics
) {
    std::fstream file{executable, std::ios::binary | std::ios::in | std::ios::out};
    if (!file) {
        diagnostics = "Could not reopen staged executable to protect package metadata.";
        return false;
    }
    file.seekp(static_cast<std::streamoff>(slot_offset), std::ios::beg);
    file.write(
        reinterpret_cast<const char*>(stamp.data()),
        static_cast<std::streamsize>(stamp.size())
    );
    file.flush();
    if (!file) {
        diagnostics = "Could not write VICE package metadata into the executable slot.";
        return false;
    }
    file.close();

    std::error_code error;
    std::filesystem::resize_file(executable, original_size, error);
    if (error) {
        diagnostics = "Could not remove temporary package metadata bytes: " + error.message();
        return false;
    }
    return true;
}

#if defined(__APPLE__)
[[nodiscard]] bool ad_hoc_sign_macos(
    const std::filesystem::path& executable,
    std::string& diagnostics
) {
    platform::ProcessLaunchRequest sign;
    sign.executable = "/usr/bin/codesign";
    sign.arguments = {
        "--force",
        "--sign",
        "-",
        "--timestamp=none",
        executable.generic_string(),
    };
    const auto signed_result = platform::run_process(sign);
    if (!signed_result) {
        diagnostics = "Could not ad-hoc sign staged executable";
        if (!signed_result.diagnostics.empty()) diagnostics += ": " + signed_result.diagnostics;
        if (!signed_result.output.empty()) diagnostics += "\n" + signed_result.output;
        return false;
    }

    platform::ProcessLaunchRequest verify;
    verify.executable = "/usr/bin/codesign";
    verify.arguments = {
        "--verify",
        "--strict",
        "--verbose=2",
        executable.generic_string(),
    };
    const auto verified = platform::run_process(verify);
    if (!verified) {
        diagnostics = "Staged executable failed macOS code signature verification";
        if (!verified.diagnostics.empty()) diagnostics += ": " + verified.diagnostics;
        if (!verified.output.empty()) diagnostics += "\n" + verified.output;
        return false;
    }
    return true;
}
#endif

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

    if (dade_required && !valid_dade_asset_id(dade_asset_id)) {
        return fail("DADE asset ID cannot be stamped because it is invalid.");
    }
    if (!dade_required && !dade_asset_id.empty()) {
        return fail("A package without DADE cannot carry a DADE asset ID.");
    }

    std::vector<std::uint8_t> original_bytes;
    std::string slot_diagnostics;
    if (!read_file(executable, original_bytes, slot_diagnostics)) {
        return fail(std::move(slot_diagnostics));
    }

    std::size_t slot_offset = 0U;
    if (!find_package_slot(original_bytes, slot_offset, slot_diagnostics)) {
        return fail(std::move(slot_diagnostics));
    }
    const auto original_size = static_cast<std::uintmax_t>(original_bytes.size());

    std::string appended_diagnostics;
    if (!append_staged_package_metadata_internal(
            executable,
            dade_required,
            dade_asset_id,
            &appended_diagnostics
        )) {
        return fail(std::move(appended_diagnostics));
    }

    std::array<std::uint8_t, stamp_size> stamp{};
    if (!read_appended_stamp(executable, original_size, stamp, slot_diagnostics)) {
        std::error_code restore_error;
        std::filesystem::resize_file(executable, original_size, restore_error);
        return fail(std::move(slot_diagnostics));
    }
    if (!replace_slot(executable, slot_offset, stamp, original_size, slot_diagnostics)) {
        return fail(std::move(slot_diagnostics));
    }

#if defined(__APPLE__)
    if (!ad_hoc_sign_macos(executable, slot_diagnostics)) {
        return fail(std::move(slot_diagnostics));
    }
#endif

    const auto verified = load_staged_package_metadata_internal(executable);
    if (!verified) {
        return fail("Stamped VICE package metadata could not be verified: " + verified.diagnostics);
    }
    if (verified.dade_required != dade_required || verified.dade_asset_id != dade_asset_id) {
        return fail("Stamped VICE package metadata does not match the requested DADE requirement.");
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

StagedPackageMetadata load_staged_package_metadata(const std::filesystem::path& executable) {
    return load_staged_package_metadata_internal(executable);
}

} // namespace vice::runtime
