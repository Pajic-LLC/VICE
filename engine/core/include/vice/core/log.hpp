#pragma once

#include <cstdint>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>

namespace vice::core {

enum class LogLevel : std::uint8_t {
    trace,
    debug,
    info,
    warning,
    error,
    fatal,
};

struct LogRecord {
    LogLevel level;
    std::string message;
    std::source_location source;
};

using LogSink = std::function<void(const LogRecord&)>;

[[nodiscard]] std::string_view log_level_name(LogLevel level) noexcept;

void set_log_sink(LogSink sink);
void reset_log_sink();

void set_min_log_level(LogLevel level) noexcept;
[[nodiscard]] LogLevel min_log_level() noexcept;

void log(
    LogLevel level,
    std::string_view message,
    std::source_location source = std::source_location::current()
);

} // namespace vice::core
