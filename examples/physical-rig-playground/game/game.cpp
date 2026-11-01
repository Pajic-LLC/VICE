#include <vice/runtime/module_api.hpp>
namespace {
using namespace vice::runtime::module;
EntityId id(unsigned n) {
    EntityId id{};
    id.bytes[0] = 0xda;
    id.bytes[6] = 0x40;
    id.bytes[8] = 0x80;
    id.bytes[15] = static_cast<std::uint8_t>(n);
    return id;
}
class Lab final : public Game {
  public:
    bool on_start(Context &c) override {
        for (unsigned i = 0; i < 11; ++i) {
            PhysicalRigSnapshot b;
            if (c.read_physical_rig(id(16 + i), b) != Result::success || b.node != i)
                return false;
        }
        return true;
    }
    void on_event(Context &c, const Event &e) override {
        if (e.kind != EventKind::key || !(e.flags & 1U))
            return;
        if (static_cast<Key>(e.code) == Key::space)
            for (unsigned i = 1; i < 11; ++i)
                (void)c.set_body_velocity(id(16 + i), {0, 0, 0}, {3, -2, 4});
        if (static_cast<Key>(e.code) == Key::r)
            for (unsigned i = 1; i < 11; ++i) {
                SwingTwistSnapshot h;
                if (c.read_swing_twist_joint(id(32 + i), h) != Result::success)
                    continue;
                auto d = h.drive;
                d.swing_mode = d.twist_mode = d.swing_mode == JointDriveMode::off
                                                  ? JointDriveMode::position
                                                  : JointDriveMode::off;
                (void)c.control_swing_twist_joint(id(32 + i), d);
            }
    }
    void on_frame(Context &, Frame &f) override { f.clear_color = {.025F, .035F, .055F, 1}; }
};
} // namespace
VICE_GAME_MODULE(Lab)
