#include <vice/runtime/dade.hpp>
#include <vice/runtime/package_metadata.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

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

#ifdef VICE_DADE_TEST_PLAYER
void copy_player(const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::copy_file(
        std::filesystem::path{VICE_DADE_TEST_PLAYER},
        destination,
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    require(!error, "vice_player copy");
}

[[nodiscard]] std::size_t package_stamp_offset(const std::filesystem::path& executable) {
    std::ifstream input{executable, std::ios::binary | std::ios::ate};
    require(static_cast<bool>(input), "package executable open");
    const auto end = input.tellg();
    require(end > 0, "package executable size");
    std::vector<char> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(input), "package executable read");

    constexpr std::string_view marker = "VICEPKG1";
    const auto found = std::search(bytes.begin(), bytes.end(), marker.begin(), marker.end());
    require(found != bytes.end(), "package stamp marker");
    const auto offset = static_cast<std::size_t>(std::distance(bytes.begin(), found));
    const auto duplicate = std::search(found + 1, bytes.end(), marker.begin(), marker.end());
    require(duplicate == bytes.end(), "unique package stamp marker");
    return offset;
}

void overwrite_byte(
    const std::filesystem::path& executable,
    const std::size_t offset,
    const char value
) {
    std::fstream file{executable, std::ios::binary | std::ios::in | std::ios::out};
    require(static_cast<bool>(file), "package executable update open");
    file.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    require(static_cast<bool>(file), "package executable update seek");
    file.put(value);
    require(static_cast<bool>(file), "package executable update write");
}
#endif

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

#ifdef VICE_DADE_TEST_PLAYER
    const auto unstamped_executable = temp_root / "unstamped-player";
    copy_player(unstamped_executable);
    const auto unstamped = load_staged_package_metadata(unstamped_executable);
    require(!static_cast<bool>(unstamped), "unstamped executable rejected");

    const auto open_executable = temp_root / "open-player";
    copy_player(open_executable);
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
    copy_player(protected_executable);
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

    const auto stamp_offset = package_stamp_offset(protected_executable);

    const auto tampered_executable = temp_root / "tampered-player";
    std::filesystem::copy_file(
        protected_executable,
        tampered_executable,
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    require(!error, "tampered executable copy");
    overwrite_byte(tampered_executable, stamp_offset + 20U, 'x');
    const auto tampered_package = load_staged_package_metadata(tampered_executable);
    require(!static_cast<bool>(tampered_package), "changed package metadata rejected");

    const auto removed_executable = temp_root / "removed-player";
    std::filesystem::copy_file(
        protected_executable,
        removed_executable,
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    require(!error, "removed executable copy");
    overwrite_byte(removed_executable, stamp_offset, 'X');
    const auto removed_package = load_staged_package_metadata(removed_executable);
    require(!static_cast<bool>(removed_package), "removed package metadata rejected");

    require(
        !stamp_staged_package_metadata(
            protected_executable,
            true,
            "bad asset",
            &stamp_diagnostics
        ),
        "invalid DADE asset cannot be stamped"
    );
#endif

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
