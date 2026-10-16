#include <vice/tools/rigid_material_cooker.hpp>

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
        << "VICE rigid material cooker\n\n"
        << "Usage:\n"
        << "  vice_rigid_material_cooker --input <source.vrigmat> --output <asset.vrigmatc>\n";
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
        std::cerr << "Could not read rigid material source manifest.\n";
        return 1;
    }

    const auto cooked = vice::tools::rigid_material_cooker::cook_source_manifest(*source);
    if (!cooked) {
        std::cerr << cooked.diagnostics << "\n";
        return 1;
    }

    std::error_code error;
    std::filesystem::create_directories(output_path.parent_path(), error);
    if (error) {
        std::cerr << "Could not create output directory: " << error.message() << "\n";
        return 1;
    }

    std::ofstream output{output_path, std::ios::binary | std::ios::trunc};
    if (!output) {
        std::cerr << "Could not open rigid material output.\n";
        return 1;
    }
    output.write(
        reinterpret_cast<const char*>(cooked.bytes.data()),
        static_cast<std::streamsize>(cooked.bytes.size())
    );
    if (!output) {
        std::cerr << "Could not write rigid material output.\n";
        return 1;
    }

    std::cout << "cooked rigid material asset: " << output_path.string() << "\n";
    return 0;
}
