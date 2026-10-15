#include <vice/tools/shader_cooker.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

using vice::tools::shader_cooker::CookRequest;
using vice::tools::shader_cooker::CookTarget;
using vice::tools::shader_cooker::ShaderStage;

void print_usage() {
    std::cout
        << "VICE shader cooker\n\n"
        << "Usage:\n"
        << "  vice_shader_cooker --input <file> --output <directory> --entry <name>\n"
        << "    --stage <vertex|fragment|compute> [--target <d3d12|metal|all>]\n"
        << "    [--shader-model <major.minor>] [--dxc <path>]\n"
        << "    [--metal-shader-converter <path>] [--debug] [--force]\n";
}

[[nodiscard]] std::optional<ShaderStage> parse_stage(const std::string_view value) {
    if (value == "vertex") {
        return ShaderStage::vertex;
    }
    if (value == "fragment" || value == "pixel") {
        return ShaderStage::fragment;
    }
    if (value == "compute") {
        return ShaderStage::compute;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<CookTarget> parse_target(const std::string_view value) {
    if (value == "d3d12" || value == "dxil") {
        return CookTarget::direct3d12;
    }
    if (value == "metal" || value == "metallib") {
        return CookTarget::metal;
    }
    if (value == "all") {
        return CookTarget::all;
    }
    return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
    CookRequest request;
    bool have_stage = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        const auto require_value = [&](const char* option) -> std::optional<std::string_view> {
            if (index + 1 >= argc) {
                std::cerr << option << " requires a value.\n";
                return std::nullopt;
            }
            ++index;
            return std::string_view{argv[index]};
        };

        if (argument == "--help" || argument == "-h") {
            print_usage();
            return 0;
        }
        if (argument == "--input") {
            const auto value = require_value("--input");
            if (!value) return 2;
            request.input = std::filesystem::path{*value};
            continue;
        }
        if (argument == "--output") {
            const auto value = require_value("--output");
            if (!value) return 2;
            request.output_directory = std::filesystem::path{*value};
            continue;
        }
        if (argument == "--entry") {
            const auto value = require_value("--entry");
            if (!value) return 2;
            request.entry_point = std::string{*value};
            continue;
        }
        if (argument == "--stage") {
            const auto value = require_value("--stage");
            if (!value) return 2;
            const auto stage = parse_stage(*value);
            if (!stage) {
                std::cerr << "Unknown shader stage: " << *value << "\n";
                return 2;
            }
            request.stage = *stage;
            have_stage = true;
            continue;
        }
        if (argument == "--target") {
            const auto value = require_value("--target");
            if (!value) return 2;
            const auto target = parse_target(*value);
            if (!target) {
                std::cerr << "Unknown shader target: " << *value << "\n";
                return 2;
            }
            request.target = *target;
            continue;
        }
        if (argument == "--shader-model") {
            const auto value = require_value("--shader-model");
            if (!value) return 2;
            request.shader_model = std::string{*value};
            continue;
        }
        if (argument == "--dxc") {
            const auto value = require_value("--dxc");
            if (!value) return 2;
            request.toolchain.dxc = std::filesystem::path{*value};
            continue;
        }
        if (argument == "--metal-shader-converter") {
            const auto value = require_value("--metal-shader-converter");
            if (!value) return 2;
            request.toolchain.metal_shader_converter = std::filesystem::path{*value};
            continue;
        }
        if (argument == "--debug") {
            request.debug = true;
            continue;
        }
        if (argument == "--force") {
            request.force = true;
            continue;
        }

        std::cerr << "Unknown option: " << argument << "\n";
        return 2;
    }

    if (request.input.empty() || request.output_directory.empty() ||
        request.entry_point.empty() || !have_stage) {
        print_usage();
        return 2;
    }

    const auto result = vice::tools::shader_cooker::cook(request);
    if (!result.success) {
        std::cerr << result.diagnostics << "\n";
        return 1;
    }

    std::cout
        << (result.cache_hit ? "cached " : "cooked ")
        << result.cache_key << "\n"
        << "DXIL: " << result.dxil_path.string() << "\n";
    if (result.metallib_path.has_value()) {
        std::cout << "metallib: " << result.metallib_path->string() << "\n";
    }
    std::cout << "manifest: " << result.manifest_path.string() << "\n";
    return 0;
}
