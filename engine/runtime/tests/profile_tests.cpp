#include <vice/runtime/profile.hpp>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "VICE runtime profile test failed: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    using namespace vice::runtime;

    RuntimeProfileHistory history;
    require(history.size() == 0U, "new history empty");
    require(history.snapshot().count == 0U, "empty snapshot");

    for (std::uint64_t frame = 1U; frame <= 125U; ++frame) {
        history.push(RuntimeFrameProfileSample{
            .frame_index = frame,
            .frame_seconds = static_cast<double>(frame) / 1000.0,
            .update_seconds = static_cast<double>(frame) / 2000.0,
            .render_seconds = static_cast<double>(frame) / 4000.0,
        });
    }

    require(history.size() == runtime_profile_frame_capacity, "history is bounded");
    const auto window = history.snapshot();
    require(window.count == runtime_profile_frame_capacity, "snapshot is bounded");
    require(window.frames[0].frame_index == 6U, "oldest frame evicted");
    require(window.frames[window.count - 1U].frame_index == 125U, "newest frame retained");
    require(window.frames[0].frame_seconds == 0.006, "frame seconds retained");
    require(window.frames[0].update_seconds == 0.003, "update seconds retained");
    require(window.frames[0].render_seconds == 0.0015, "render seconds retained");

    history.push(RuntimeFrameProfileSample{
        .frame_index = 126U,
        .frame_seconds = -1.0,
        .update_seconds = std::numeric_limits<double>::infinity(),
        .render_seconds = std::numeric_limits<double>::quiet_NaN(),
    });
    const auto sanitized = history.snapshot();
    const auto& last = sanitized.frames[sanitized.count - 1U];
    require(last.frame_index == 126U, "sanitized frame retained");
    require(last.frame_seconds == 0.0, "negative timing sanitized");
    require(last.update_seconds == 0.0, "infinite timing sanitized");
    require(last.render_seconds == 0.0, "NaN timing sanitized");

    history.clear();
    require(history.size() == 0U, "clear history");
    require(history.snapshot().count == 0U, "clear snapshot");
    return 0;
}
