#include <vice/runtime/dade.hpp>
#include <vice/runtime/package_metadata.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#ifndef VICE_DADE_TEST_PROVIDER
#error VICE_DADE_TEST_PROVIDER must be defined
#endif

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "dade test failed: " << message << '\n';
    std::exit(1);
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "test file open");
    output << text;
    require(static_cast<bool>(output), "test file write");
}

} // namespace

int main() {
    using namespace vice::runtime;

    require(valid_dade_asset_id("com.pajic.vice"), "valid asset ID");
    require(valid_dade_asset_id("vice_game-2.preview"), "portable asset ID");
    require(!valid_dade_asset_id(""), "empty asset ID rejected");
    require(!valid_dade_asset_id("bad asset"), "space rejected");
    require(!valid_dade_asset_id("/bad"), "leading punctuation rejected");

    std::error_code error;
    const auto temp_root = std::filesystem::temp_directory_path(error) / "vice_dade_gate_test";
    require(!error, "temporary directory path");
    std::filesystem::remove_all(temp_root, error);
    error.clear();
    std::filesystem::create_directories(temp_root, error);
    require(!error, "temporary directory create");

    const auto manifest = temp_root / "game.viceproj";
    write_text(manifest, "placeholder\n");

    const auto optional = load_dade_requirement(manifest);
    require(static_cast<bool>(optional), "optional DADE config load");
    require(!optional.required, "missing DADE config stays optional");

    write_text(
        temp_root / "dade.asset",
        "# DADE ownership asset\n"
        "com.pajic.vice\n"
    );
    const auto required = load_dade_requirement(manifest);
    require(static_cast<bool>(required), "required DADE config load");
    require(required.required, "DADE config enables ownership gate");
    require(required.asset_id == "com.pajic.vice", "DADE asset ID value");

    const auto unstamped_executable = temp_root / "unstamped-player";
    write_text(unstamped_executable, "VICE runtime host\n");
    const auto unstamped = load_staged_package_metadata(unstamped_executable);
    require(!static_cast<bool>(unstamped), "unstamped executable rejected");

    const auto open_executable = temp_root / "open-player";
    write_text(open_executable, "VICE runtime host\n");
    std::string stamp_diagnostics;
    require(
        stamp_staged_package_metadata(open_executable, false, {}, &stamp_diagnostics),
        "non-DADE package stamp"
    );
    require(stamp_diagnostics.empty(), "non-DADE package stamp diagnostics");
    const auto open_package = load_staged_package_metadata(open_executable);
    require(static_cast<bool>(open_package), "non-DADE package metadata load");
    require(!open_package.dade_required, "non-DADE package stays open");
    require(open_package.dade_asset_id.empty(), "non-DADE package has no asset ID");

    const auto protected_executable = temp_root / "protected-player";
    write_text(protected_executable, "VICE runtime host\n");
    require(
        stamp_staged_package_metadata(
            protected_executable,
            true,
            "com.pajic.vice",
            &stamp_diagnostics
        ),
        "DADE package stamp"
    );
    const auto protected_package = load_staged_package_metadata(protected_executable);
    require(static_cast<bool>(protected_package), "DADE package metadata load");
    require(protected_package.dade_required, "DADE package requires ownership");
    require(protected_package.dade_asset_id == "com.pajic.vice", "stamped DADE asset ID");

    const auto tampered_executable = temp_root / "tampered-player";
    std::filesystem::copy_file(
        protected_executable,
        tampered_executable,
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    require(!error, "tampered executable copy");
    {
        std::fstream file{tampered_executable, std::ios::binary | std::ios::in | std::ios::out};
        require(static_cast<bool>(file), "tampered executable open");
        file.seekp(static_cast<std::streamoff>(-300), std::ios::end);
        require(static_cast<bool>(file), "tampered executable seek");
        file.put('x');
        require(static_cast<bool>(file), "tampered executable write");
    }
    const auto tampered_package = load_staged_package_metadata(tampered_executable);
    require(!static_cast<bool>(tampered_package), "changed package metadata rejected");

    const auto truncated_executable = temp_root / "truncated-player";
    std::filesystem::copy_file(
        protected_executable,
        truncated_executable,
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    require(!error, "truncated executable copy");
    const auto truncated_size = std::filesystem::file_size(truncated_executable, error);
    require(!error && truncated_size > 0U, "truncated executable size");
    std::filesystem::resize_file(truncated_executable, truncated_size - 1U, error);
    require(!error, "truncate package metadata");
    const auto truncated_package = load_staged_package_metadata(truncated_executable);
    require(!static_cast<bool>(truncated_package), "removed package metadata rejected");

    {
        std::ofstream signed_file{protected_executable, std::ios::binary | std::ios::app};
        require(static_cast<bool>(signed_file), "signed executable append open");
        signed_file << "simulated-platform-signature-data";
        require(static_cast<bool>(signed_file), "signed executable append");
    }
    const auto signed_package = load_staged_package_metadata(protected_executable);
    require(static_cast<bool>(signed_package), "package metadata survives trailing signature data");
    require(signed_package.dade_required, "signed package still requires DADE");
    require(signed_package.dade_asset_id == "com.pajic.vice", "signed package keeps asset ID");

    require(
        !stamp_staged_package_metadata(
            temp_root / "bad-player",
            true,
            "bad asset",
            &stamp_diagnostics
        ),
        "invalid DADE asset cannot be stamped"
    );

    auto provider = load_dade_provider(std::filesystem::path{VICE_DADE_TEST_PROVIDER});
    require(static_cast<bool>(provider), "DADE provider load");

    const auto owned = provider.provider->verify(
        required.asset_id,
        "windows",
        temp_root
    );
    require(owned.allowed, "owned asset allowed");
    require(owned.code == dade::DecisionCode::owned, "owned decision code");
    require(owned.detail == "test deed is owned", "owned detail");

    const auto wrong = provider.provider->verify(
        "com.pajic.other",
        "windows",
        temp_root
    );
    require(!wrong.allowed, "wrong asset denied");
    require(wrong.code == dade::DecisionCode::wrong_asset, "wrong asset decision code");

    require(dade_decision_code(dade::DecisionCode::controller_revoked) == "controller_revoked", "decision text");

    write_text(
        temp_root / "dade.asset",
        "com.pajic.vice\n"
        "com.pajic.other\n"
    );
    const auto invalid = load_dade_requirement(manifest);
    require(!static_cast<bool>(invalid), "multiple asset IDs rejected");

    std::filesystem::remove_all(temp_root, error);
    require(!error, "temporary directory cleanup");
    return 0;
}
