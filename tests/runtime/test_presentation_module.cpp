#include <limits>
#include <thread>
#include <vice/runtime/module_api.hpp>
namespace {
using namespace vice::runtime::module;
constexpr EntityId id(unsigned n) {
    EntityId e{};
    e.bytes[0] = 0xa2;
    e.bytes[6] = 0x40;
    e.bytes[8] = 0x80;
    e.bytes[15] = static_cast<std::uint8_t>(n);
    return e;
}
class PresentationGame final : public Game {
    bool valid_ = true;

  public:
    bool on_start(Context &c) override {
        AnimationSnapshot a;
        AudioSnapshot sound;
        valid_ = c.read_animation(id(3), a) == Result::success && a.node_count == 13 &&
                 a.part_count == 13 && a.duration == 1;
        valid_ &= c.read_audio(id(2), sound) == Result::success && sound.duration == 1 &&
                  c.read_animation(id(2), a) == Result::no_animation &&
                  c.read_audio(id(4), sound) == Result::no_audio;
        a.size = 0;
        sound.size = 0;
        valid_ &= c.read_animation(id(3), a) == Result::invalid_argument &&
                  c.read_audio(id(2), sound) == Result::invalid_argument;
        valid_ &= c.control_animation(id(3), 63) == Result::invalid_argument &&
                  c.control_animation(id(3), 1, std::numeric_limits<float>::quiet_NaN()) ==
                      Result::invalid_argument;
        valid_ &= c.control_audio(id(2), 2, 1, true) == Result::invalid_argument &&
                  c.set_character_facing(id(3), {0, 0, 0, 2}) == Result::invalid_argument;
        Result worker_result = Result::success;
        std::thread worker{[&] {
            AudioSnapshot state;
            worker_result = c.read_audio(id(2), state);
        }};
        worker.join();
        valid_ &= worker_result == Result::wrong_phase;
        HostApi old;
        old.size = static_cast<std::uint32_t>(offsetof(HostApi, read_animation));
        Context legacy{&old};
        valid_ &= legacy.read_animation(id(3), a) == Result::unavailable &&
                  legacy.control_audio(id(2), .1F, 1, true) == Result::unavailable;
        return valid_;
    }
    void on_event(Context &c, const Event &e) override {
        if (e.kind == EventKind::key && (e.flags & 1U))
            valid_ &=
                c.control_animation(id(3), 2, .5F, true, true, true) == Result::success &&
                c.control_audio(id(2), .2F, 1.3F, true, true) == Result::success &&
                c.set_character_facing(id(3), {0, 0.70710678F, 0, 0.70710678F}) == Result::success;
    }
    void on_frame(Context &c, Frame &f) override {
        AnimationSnapshot a;
        AudioSnapshot sound;
        valid_ &= c.read_animation(id(3), a) == Result::success &&
                  c.read_audio(id(2), sound) == Result::success;
        valid_ &= c.control_animation(id(3), 0) == Result::wrong_phase &&
                  c.control_audio(id(2), .1F, 1, false) == Result::wrong_phase &&
                  c.set_character_facing(id(3), {}) == Result::wrong_phase;
        f.clear_color = {valid_ ? 1.0F : 0.0F, a.clip == 2 && a.time > 0 ? 1.0F : 0.0F,
                         sound.time > 0 ? 1.0F : 0.0F, 1};
    }
};
} // namespace
VICE_GAME_MODULE(PresentationGame)
