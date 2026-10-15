#include <vice/core/assert.hpp>

#include <vice/core/log.hpp>

#include <cstdlib>
#include <string>

namespace vice::core {

[[noreturn]] void panic(const std::string_view message, const std::source_location source) noexcept {
    try {
        log(LogLevel::fatal, message, source);
    } catch (...) {
    }

    std::abort();
}

[[noreturn]] void assertion_failed(
    const std::string_view expression,
    const std::string_view message,
    const std::source_location source
) noexcept {
    try {
        std::string text{"Assertion failed: "};
        text.append(expression);

        if (!message.empty()) {
            text.append(". ");
            text.append(message);
        }

        log(LogLevel::fatal, text, source);
    } catch (...) {
    }

    std::abort();
}

} // namespace vice::core
