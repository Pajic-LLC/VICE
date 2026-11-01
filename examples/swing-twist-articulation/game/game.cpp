#include <vice/runtime/module_api.hpp>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(unsigned n) {
    EntityId id{};
    id.bytes[0] = 0xd7;
    id.bytes[6] = 0x40;
    id.bytes[8] = 0x80;
    id.bytes[15] = static_cast<std::uint8_t>(n);
    return id;
}
class Articulation final : public Game {
  public:
    bool on_start(Context &c) override {
        SwingTwistSnapshot h;
        return c.read_swing_twist_joint(entity(3), h) == Result::success;
    }
    void on_event(Context &c, const Event &e) override {
        if (e.kind != EventKind::key || !(e.flags & 1U))
            return;
        SwingTwistSnapshot h;
        if (c.read_swing_twist_joint(entity(3), h) != Result::success)
            return;
        if (static_cast<Key>(e.code) == Key::space)
            (void)c.set_body_velocity(entity(2), {0, 0, 0}, {4, -5, 3});
        if (static_cast<Key>(e.code) == Key::e) {
            auto d = h.drive;
            d.swing_mode = d.twist_mode = JointDriveMode::position;
            for (unsigned i = 0; i < 3; ++i)
                d.target_orientation[i] = -d.target_orientation[i];
            (void)c.control_swing_twist_joint(entity(3), d);
        }
        if (static_cast<Key>(e.code) == Key::r) {
            auto d = h.drive;
            d.swing_mode = d.twist_mode = h.drive.swing_mode == JointDriveMode::off
                                              ? JointDriveMode::position
                                              : JointDriveMode::off;
            (void)c.control_swing_twist_joint(entity(3), d);
        }
    }
    void on_frame(Context &, Frame &f) override { f.clear_color = {.025F, .035F, .055F, 1}; }
};
} // namespace
VICE_GAME_MODULE(Articulation)
