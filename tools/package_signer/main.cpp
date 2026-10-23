#include <vice/platform/process.hpp>
#include <vice/runtime/package_metadata.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::filesystem::path executable;
    std::optional<std::string> stamp_asset;
    bool stamp_open = false;
    std::string macos_identity = "-";
    std::filesystem::path signtool = "signtool.exe";
    std::string certificate_sha1;
    std::string timestamp_url;
};

void usage() {
    std::cerr
        << "Usage:\n"
        << "  vice_package_signer --executable <path> [--stamp-asset <id> | --stamp-open]\n"
#if defined(__APPLE__)
        << "    [--identity <codesign identity>]\n";
#elif defined(_WIN32)
        << "    --certificate-sha1 <thumbprint> [--signtool <path>] [--timestamp-url <url>]\n";
#else
        << "\n";
#endif
}

[[nodiscard]] std::optional<Options> parse_options(const int argc, char** argv) {
    Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        const auto take_value = [&](std::string_view name) -> std::optional<std::string> {
            if (index + 1 >= argc) {
                std::cerr << name << " requires a value.\n";
                return std::nullopt;
            }
            ++index;
            return std::string{argv[index]};
        };

        if (argument == "--executable") {
            auto value = take_value(argument);
            if (!value.has_value()) return std::nullopt;
            options.executable = *value;
        } else if (argument == "--stamp-asset") {
            auto value = take_value(argument);
            if (!value.has_value()) return std::nullopt;
            options.stamp_asset = std::move(*value);
        } else if (argument == "--stamp-open") {
            options.stamp_open = true;
        } else if (argument == "--identity") {
            auto value = take_value(argument);
            if (!value.has_value()) return std::nullopt;
            options.macos_identity = std::move(*value);
        } else if (argument == "--signtool") {
            auto value = take_value(argument);
            if (!value.has_value()) return std::nullopt;
            options.signtool = *value;
        } else if (argument == "--certificate-sha1") {
            auto value = take_value(argument);
            if (!value.has_value()) return std::nullopt;
            options.certificate_sha1 = std::move(*value);
        } else if (argument == "--timestamp-url") {
            auto value = take_value(argument);
            if (!value.has_value()) return std::nullopt;
            options.timestamp_url = std::move(*value);
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            return std::nullopt;
        }
    }

    if (options.executable.empty()) {
        std::cerr << "--executable is required.\n";
        return std::nullopt;
    }
    if (options.stamp_asset.has_value() && options.stamp_open) {
        std::cerr << "Use either --stamp-asset or --stamp-open, not both.\n";
        return std::nullopt;
    }
#if defined(_WIN32)
    if (options.certificate_sha1.empty()) {
        std::cerr << "--certificate-sha1 is required on Windows.\n";
        return std::nullopt;
    }
#endif
    return options;
}

[[nodiscard]] bool run_command(
    const std::filesystem::path& executable,
    std::vector<std::string> arguments,
    std::string_view label
) {
    vice::platform::ProcessLaunchRequest request;
    request.executable = executable;
    request.arguments = std::move(arguments);
    const auto result = vice::platform::run_process(request);
    if (result) return true;

    std::cerr << label;
    if (!result.diagnostics.empty()) std::cerr << ": " << result.diagnostics;
    std::cerr << '\n';
    if (!result.output.empty()) std::cerr << result.output << '\n';
    return false;
}

[[nodiscard]] bool sign_platform_executable(const Options& options) {
#if defined(__APPLE__)
    std::vector<std::string> sign_arguments{
        "--force",
        "--sign",
        options.macos_identity,
    };
    if (options.macos_identity == "-") {
        sign_arguments.push_back("--timestamp=none");
    } else {
        sign_arguments.push_back("--timestamp");
    }
    sign_arguments.push_back(options.executable.generic_string());

    if (!run_command("/usr/bin/codesign", std::move(sign_arguments), "codesign failed")) {
        return false;
    }
    return run_command(
        "/usr/bin/codesign",
        {
            "--verify",
            "--strict",
            "--verbose=2",
            options.executable.generic_string(),
        },
        "codesign verification failed"
    );
#elif defined(_WIN32)
    std::vector<std::string> sign_arguments{
        "sign",
        "/fd",
        "SHA256",
        "/s",
        "My",
        "/sha1",
        options.certificate_sha1,
    };
    if (!options.timestamp_url.empty()) {
        sign_arguments.push_back("/tr");
        sign_arguments.push_back(options.timestamp_url);
        sign_arguments.push_back("/td");
        sign_arguments.push_back("SHA256");
    }
    sign_arguments.push_back(options.executable.generic_string());

    if (!run_command(options.signtool, std::move(sign_arguments), "Authenticode signing failed")) {
        return false;
    }
    return run_command(
        options.signtool,
        {
            "verify",
            "/pa",
            "/all",
            "/v",
            options.executable.generic_string(),
        },
        "Authenticode verification failed"
    );
#else
    std::cerr << "VICE package signing is not implemented for this platform.\n";
    return false;
#endif
}

} // namespace

int main(const int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    if (!options.has_value()) {
        usage();
        return 2;
    }

    std::string diagnostics;
    if (options->stamp_asset.has_value()) {
        if (!vice::runtime::stamp_staged_package_metadata(
                options->executable,
                true,
                *options->stamp_asset,
                &diagnostics
            )) {
            std::cerr << "Could not stamp DADE package metadata: " << diagnostics << '\n';
            return 1;
        }
    } else if (options->stamp_open) {
        if (!vice::runtime::stamp_staged_package_metadata(
                options->executable,
                false,
                {},
                &diagnostics
            )) {
            std::cerr << "Could not stamp package metadata: " << diagnostics << '\n';
            return 1;
        }
    }

    const auto before = vice::runtime::load_staged_package_metadata(options->executable);
    if (!before) {
        std::cerr << "Executable does not contain valid VICE package metadata: "
                  << before.diagnostics << '\n';
        return 1;
    }

    if (!sign_platform_executable(*options)) return 1;

    const auto after = vice::runtime::load_staged_package_metadata(options->executable);
    if (!after) {
        std::cerr << "Platform signing damaged VICE package metadata: "
                  << after.diagnostics << '\n';
        return 1;
    }
    if (before.dade_required != after.dade_required ||
        before.dade_asset_id != after.dade_asset_id) {
        std::cerr << "Platform signing changed the DADE package requirement.\n";
        return 1;
    }

    if (after.dade_required) {
        std::cout << "Signed package. DADE asset: " << after.dade_asset_id << '\n';
    } else {
        std::cout << "Signed package. DADE is not required.\n";
    }
    return 0;
}
