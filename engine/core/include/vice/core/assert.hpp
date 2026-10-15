#pragma once

#include <source_location>
#include <string_view>

namespace vice::core {

[[noreturn]] void panic(
    std::string_view message,
    std::source_location source = std::source_location::current()
) noexcept;

[[noreturn]] void assertion_failed(
    std::string_view expression,
    std::string_view message,
    std::source_location source
) noexcept;

} // namespace vice::core

#ifndef NDEBUG
#define VICE_ASSERT(condition, message)                                                   \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            ::vice::core::assertion_failed(                                               \
                #condition,                                                               \
                (message),                                                                \
                std::source_location::current()                                            \
            );                                                                            \
        }                                                                                 \
    } while (false)
#else
#define VICE_ASSERT(condition, message) ((void)0)
#endif
