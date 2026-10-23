#include <vice/runtime/dade.hpp>

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
    {
        std::ofstream output{manifest};
        output << "placeholder\n";
    }

    const auto optional = load_dade_requirement(manifest);
    require(static_cast<bool>(optional), "optional DADE config load");
    require(!optional.required, "missing DADE config stays optional");

    {
        std::ofstream output{temp_root / "dade.asset"};
        output << "# DADE ownership asset\n";
        output << "com.pajic.vice\n";
    }
    const auto required = load_dade_requirement(manifest);
    require(static_cast<bool>(required), "required DADE config load");
    require(required.required, "DADE config enables ownership gate");
    require(required.asset_id == "com.pajic.vice", "DADE asset ID value");

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

    {
        std::ofstream output{temp_root / "dade.asset", std::ios::trunc};
        output << "com.pajic.vice\ncom.pajic.other\n";
    }
    const auto invalid = load_dade_requirement(manifest);
    require(!static_cast<bool>(invalid), "multiple asset IDs rejected");

    std::filesystem::remove_all(temp_root, error);
    require(!error, "temporary directory cleanup");
    return 0;
}
