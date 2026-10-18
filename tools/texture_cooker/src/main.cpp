#include <vice/tools/texture_cooker.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) return {};
    std::ostringstream contents;
    contents << input.rdbuf();
    return input.eof() || !input.fail() ? contents.str() : std::string{};
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) return {};
    const std::vector<char> raw{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    if (!input.eof() && input.fail()) return {};
    const auto bytes = std::as_bytes(std::span<const char>{raw});
    return {bytes.begin(), bytes.end()};
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: vice_texture_cooker <input.vtex> <output.vtexc>\n";
        return 2;
    }

    const std::filesystem::path source_path{argv[1]};
    const std::filesystem::path output_path{argv[2]};
    const std::string source_text = read_text(source_path);
    const auto parsed = vice::tools::texture_cooker::parse_source_manifest(source_text);
    if (!parsed.source.has_value()) {
        std::cerr << parsed.diagnostics << '\n';
        return 1;
    }
    const auto asset_root = source_path.parent_path().parent_path();
    const auto image_path = (asset_root / parsed.source->image_source).lexically_normal();
    const auto image = read_bytes(image_path);
    if (image.empty()) {
        std::cerr << "Could not read texture image: " << image_path.string() << '\n';
        return 1;
    }
    const auto cooked = vice::tools::texture_cooker::cook(*parsed.source, image);
    if (!cooked.success) {
        std::cerr << cooked.diagnostics << '\n';
        return 1;
    }
    std::string diagnostics;
    if (!vice::tools::texture_cooker::write_asset(output_path, cooked.bytes, &diagnostics)) {
        std::cerr << diagnostics << '\n';
        return 1;
    }
    return 0;
}
