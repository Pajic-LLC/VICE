#include <vice/platform/file.hpp>

#include <fstream>

namespace vice::platform {

TextFileResult read_text_file(const std::filesystem::path& path, const std::size_t max_bytes) {
    TextFileResult result;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        result.diagnostics = "Source is not an available regular file: " + path.generic_string();
        return result;
    }
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        result.diagnostics = "Could not open source file: " + path.generic_string();
        return result;
    }
    const auto size = input.tellg();
    if (size < 0 || static_cast<std::uintmax_t>(size) > max_bytes ||
        static_cast<std::uintmax_t>(size) > max_text_file_bytes) {
        result.diagnostics = "Source file exceeds the bounded size limit.";
        return result;
    }
    result.contents.resize(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(result.contents.data(), static_cast<std::streamsize>(result.contents.size()));
    if (input.gcount() != static_cast<std::streamsize>(result.contents.size()) ||
        input.peek() != std::char_traits<char>::eof() || input.bad()) {
        result.contents.clear();
        result.diagnostics = "Source file changed or could not be read completely.";
        return result;
    }
    result.success = true;
    return result;
}

} // namespace vice::platform
