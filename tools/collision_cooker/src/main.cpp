#include <vice/tools/collision_cooker.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

void print_usage() {
    std::cout
        << "VICE collision cooker\n\n"
        << "Usage:\n"
        << "  vice_collision_cooker --input <source.vcollision> --output <asset.vcollisionc>\n\n"
        << "The source manifest is a developer interchange format. Importers can call\n"
        << "VICE::CollisionCooker directly instead of generating this text format.\n";
}

[[nodiscard]] std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.eof() && input.fail()) {
        return std::nullopt;
    }
    return contents.str();
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path input_path;
    std::filesystem::path output_path;

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
            input_path = std::filesystem::path{*value};
            continue;
        }
        if (argument == "--output") {
            const auto value = require_value("--output");
            if (!value) return 2;
            output_path = std::filesystem::path{*value};
            continue;
        }

        std::cerr << "Unknown option: " << argument << "\n";
        return 2;
    }

    if (input_path.empty() || output_path.empty()) {
        print_usage();
        return 2;
    }

    const auto source = read_text(input_path);
    if (!source.has_value()) {
        std::cerr << "Could not read collision source manifest.\n";
        return 1;
    }

    const auto parsed = vice::tools::collision_cooker::parse_source_manifest(*source);
    if (!parsed.shapes.has_value()) {
        std::cerr << parsed.diagnostics << "\n";
        return 1;
    }

    const auto cooked = vice::tools::collision_cooker::cook(*parsed.shapes);
    if (!cooked.success) {
        std::cerr << cooked.diagnostics << "\n";
        return 1;
    }

    std::string diagnostics;
    if (!vice::tools::collision_cooker::write_asset(
            output_path,
            cooked.bytes,
            &diagnostics
        )) {
        std::cerr << diagnostics << "\n";
        return 1;
    }

    std::cout
        << "cooked " << cooked.bytes.size() << " bytes\n"
        << "fingerprint: 0x" << std::hex << std::setw(16) << std::setfill('0')
        << cooked.source_fingerprint << "\n"
        << "output: " << output_path.string() << "\n";
    return 0;
}
