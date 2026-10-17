#include <vice/core/build.hpp>
#include <vice/core/log.hpp>
#include <vice/core/platform.hpp>
#include <vice/core/time.hpp>
#include <vice/core/version.hpp>

#include <cassert>
#include <chrono>
#include <string_view>
#include <vector>

namespace {

void test_version() {
    static_assert(vice::core::Version::major == 0);
    static_assert(vice::core::Version::minor == 1);
    static_assert(vice::core::Version::patch == 0);
    assert(vice::core::version_string() == std::string_view{"0.1.0"});
}

void test_platform() {
    using vice::core::Platform;

    const auto platform = vice::core::current_platform();
    assert(platform == Platform::macOS || platform == Platform::Windows);
    assert(vice::core::platform_name(platform) != std::string_view{"Unsupported"});
}

void test_logging() {
    using namespace vice::core;

    std::vector<LogRecord> primary_records;
    std::vector<LogRecord> observed_records;
    set_min_log_level(LogLevel::warning);
    set_log_sink([&primary_records](const LogRecord& record) {
        primary_records.push_back(record);
    });

    const auto observer_id = add_log_observer([&observed_records](const LogRecord& record) {
        observed_records.push_back(record);
    });
    assert(observer_id != invalid_log_observer_id);

    log(LogLevel::info, "ignored");
    log(LogLevel::error, "captured");

    assert(primary_records.size() == 1);
    assert(primary_records.front().level == LogLevel::error);
    assert(primary_records.front().message == "captured");
    assert(observed_records.size() == 1);
    assert(observed_records.front().message == "captured");

    assert(remove_log_observer(observer_id));
    assert(!remove_log_observer(observer_id));
    log(LogLevel::fatal, "primary only");

    assert(primary_records.size() == 2);
    assert(primary_records.back().message == "primary only");
    assert(observed_records.size() == 1);
    assert(add_log_observer({}) == invalid_log_observer_id);
    assert(!remove_log_observer(invalid_log_observer_id));

    reset_log_sink();
    set_min_log_level(LogLevel::debug);
}

void test_frame_clock() {
    using namespace std::chrono_literals;
    using vice::core::FrameClock;
    using vice::core::TimePoint;

    const TimePoint start{};
    FrameClock clock{start};

    clock.tick(start + 16ms);
    assert(clock.delta() == 16ms);
    assert(clock.elapsed() == 16ms);
    assert(clock.frame_index() == 1);

    clock.tick(start + 40ms);
    assert(clock.delta() == 24ms);
    assert(clock.elapsed() == 40ms);
    assert(clock.frame_index() == 2);

    clock.tick(start + 20ms);
    assert(clock.delta() == 0ms);
    assert(clock.elapsed() == 40ms);
    assert(clock.frame_index() == 3);
}

void test_build_metadata() {
    using namespace vice::core;

    assert(!build_configuration_name(current_build_configuration()).empty());
    assert(!compiler_name().empty());
    assert(!architecture_name().empty());
}

} // namespace

int main() {
    test_version();
    test_platform();
    test_logging();
    test_frame_clock();
    test_build_metadata();
    return 0;
}
