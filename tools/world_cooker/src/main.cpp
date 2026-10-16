#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

void print_usage() {
    std::cout
        << "VICE world cooker\n\n"
        << "Usage:\n"
        << "  vice_world_cooker --input <source.vworld> --output <asset.vworldc>\n";
}

[[nodiscard]] std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) return std::nullopt;
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.eof() && input.fail()) return std::nullopt;
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
        std::cerr << "Could not read world source manifest.\n";
        return 1;
    }

    const auto parsed = vice::tools::world_cooker::parse_source_manifest(*source);
    if (!parsed.snapshot.has_value()) {
        std::cerr << parsed.diagnostics << "\n";
        return 1;
    }

    std::string diagnostics;
    if (!vice::world::save_world_snapshot(output_path, *parsed.snapshot, &diagnostics)) {
        std::cerr << diagnostics << "\n";
        return 1;
    }

    std::cout
        << "cooked " << parsed.snapshot->entities.size() << " entities, "
        << parsed.snapshot->regions.size() << " regions\n"
        << "output: " << output_path.string() << "\n";
    return 0;
}
