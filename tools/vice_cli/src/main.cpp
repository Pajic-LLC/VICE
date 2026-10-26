#include <vice/dev/cli.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

int main(const int argc, char** argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    vice::dev::CommandResult result;
    if (arguments.size() == 3U && arguments[0] == "runtime" && arguments[1] == "profile") {
        result = vice::dev::execute_runtime_profile(
            std::filesystem::path{std::string{arguments[2]}}
        );
    } else if (arguments.size() == 4U &&
               arguments[0] == "runtime" && arguments[1] == "debug") {
        result = vice::dev::execute_runtime_debug(
            std::filesystem::path{std::string{arguments[2]}},
            arguments[3]
        );
    } else {
        result = vice::dev::execute(arguments);
    }

    std::cout << result.json;
    if (result.json.empty() || result.json.back() != '\n') {
        std::cout << '\n';
    }
    return result.exit_code;
}
