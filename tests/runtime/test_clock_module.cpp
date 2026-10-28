#include <thread>
#include <vice/runtime/module_api.hpp>
namespace {
using namespace vice::runtime::module;
class ClockGame final : public Game {
    bool valid_ = true;

  public:
    bool on_start(Context &c) override {
        WorldClockSnapshot out;
        valid_ &= c.read_world_clock(out) == Result::success && out.ticks_per_second == 1'000'000;
        valid_ &= c.configure_world_clock(true, 2000, false) == Result::success;
        if (!out.ticks)
            valid_ &= c.step_world_clock(7) == Result::success;
        valid_ &= c.configure_world_clock(false, 0) == Result::invalid_argument;
        out.size = 0;
        valid_ &= c.read_world_clock(out) == Result::invalid_argument;
        Result other = Result::success;
        std::thread t{[&] {
            WorldClockSnapshot x;
            other = c.read_world_clock(x);
        }};
        t.join();
        valid_ &= other == Result::wrong_phase;
        HostApi old;
        old.size = static_cast<std::uint32_t>(offsetof(HostApi, read_world_clock));
        Context legacy{&old};
        valid_ &= legacy.read_world_clock(out) == Result::unavailable;
        return valid_;
    }
    void on_event(Context &c, const Event &e) override {
        if (e.kind == EventKind::key && (e.flags & 1U))
            valid_ &= c.step_world_clock(9) == Result::success;
    }
    void on_frame(Context &c, Frame &f) override {
        WorldClockSnapshot out;
        valid_ &= c.read_world_clock(out) == Result::success &&
                  c.step_world_clock(1) == Result::wrong_phase &&
                  c.configure_world_clock(false) == Result::wrong_phase;
        f.clear_color = {valid_ ? 1.0F : 0.0F, out.ticks >= 16 ? 1.0F : 0.0F, 1, 1};
    }
};
} // namespace
VICE_GAME_MODULE(ClockGame)
