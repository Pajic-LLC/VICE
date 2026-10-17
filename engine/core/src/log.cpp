#include <vice/core/log.hpp>

#include <algorithm>
#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

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

struct ObserverEntry {
    LogObserverId id = invalid_log_observer_id;
    LogSink sink;
};

struct LoggerState {
    std::mutex mutex;
    LogSink sink = default_log_sink;
    std::vector<ObserverEntry> observers;
    LogObserverId next_observer_id = 1;
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

LogObserverId add_log_observer(LogSink observer) {
    if (!observer) {
        return invalid_log_observer_id;
    }

    auto& state = logger_state();
    std::scoped_lock lock{state.mutex};

    const auto observer_id = state.next_observer_id++;
    if (state.next_observer_id == invalid_log_observer_id) {
        state.next_observer_id = 1;
    }
    state.observers.push_back(ObserverEntry{observer_id, std::move(observer)});
    return observer_id;
}

bool remove_log_observer(const LogObserverId observer_id) {
    if (observer_id == invalid_log_observer_id) {
        return false;
    }

    auto& state = logger_state();
    std::scoped_lock lock{state.mutex};
    const auto previous_size = state.observers.size();
    std::erase_if(state.observers, [observer_id](const ObserverEntry& entry) {
        return entry.id == observer_id;
    });
    return state.observers.size() != previous_size;
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
    std::vector<LogSink> observers;

    {
        auto& state = logger_state();
        std::scoped_lock lock{state.mutex};

        if (level < state.minimum) {
            return;
        }

        sink = state.sink;
        observers.reserve(state.observers.size());
        for (const auto& observer : state.observers) {
            observers.push_back(observer.sink);
        }
    }

    const LogRecord record{level, std::string{message}, source};
    sink(record);
    for (const auto& observer : observers) {
        observer(record);
    }
}

} // namespace vice::core
