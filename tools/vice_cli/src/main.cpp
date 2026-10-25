#include <vice/dev/cli.hpp>

#include <iostream>
#include <string_view>
#include <vector>

int main(const int argc, char** argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    const auto result = vice::dev::execute(arguments);
    std::cout << result.json;
    if (result.json.empty() || result.json.back() != '\n') {
        std::cout << '\n';
    }
    return result.exit_code;
}
