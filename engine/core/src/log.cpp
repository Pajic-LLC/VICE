#include <vice/core/log.hpp>

#include <iostream>
#include <mutex>
#include <utility>

namespace vice::core {
namespace {

void default_log_sink(const LogRecord& record) {
    auto& stream = record.level >= LogLevel::error ? std::cerr : std::clog;
    stream << "[VICE][" << log_level_name(record.level) << "] " << record.message;

    if (record.level >= LogLevel::warning) {
        stream << " (" << record.source.file_name() << ':' << record.source.line() << ')';
    }

    stream << '\n';
}

struct LoggerState {
    std::mutex mutex;
    LogSink sink = default_log_sink;
#ifdef NDEBUG
    LogLevel minimum = LogLevel::info;
#else
    LogLevel minimum = LogLevel::debug;
#endif
};

LoggerState& logger_state() {
    static LoggerState state;
    return state;
}

} // namespace

std::string_view log_level_name(const LogLevel level) noexcept {
    switch (level) {
    case LogLevel::trace:
        return "TRACE";
    case LogLevel::debug:
        return "DEBUG";
    case LogLevel::info:
        return "INFO";
    case LogLevel::warning:
        return "WARN";
    case LogLevel::error:
        return "ERROR";
    case LogLevel::fatal:
        return "FATAL";
    }

    return "UNKNOWN";
}

void set_log_sink(LogSink sink) {
    auto& state = logger_state();
    std::scoped_lock lock{state.mutex};
    state.sink = sink ? std::move(sink) : LogSink{default_log_sink};
}

void reset_log_sink() {
    set_log_sink(default_log_sink);
}

void set_min_log_level(const LogLevel level) noexcept {
    auto& state = logger_state();
    std::scoped_lock lock{state.mutex};
    state.minimum = level;
}

LogLevel min_log_level() noexcept {
    auto& state = logger_state();
    std::scoped_lock lock{state.mutex};
    return state.minimum;
}

void log(const LogLevel level, const std::string_view message, const std::source_location source) {
    LogSink sink;

    {
        auto& state = logger_state();
        std::scoped_lock lock{state.mutex};

        if (level < state.minimum) {
            return;
        }

        sink = state.sink;
    }

    sink(LogRecord{level, std::string{message}, source});
}

} // namespace vice::core
