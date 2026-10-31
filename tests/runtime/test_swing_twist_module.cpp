#include <thread>
#include <vice/runtime/module_api.hpp>
namespace {
using namespace vice::runtime::module;
constexpr EntityId joint(unsigned n) {
    EntityId id{};
    id.bytes[0] = 0xd7;
    id.bytes[6] = 0x40;
    id.bytes[8] = 0x80;
    id.bytes[15] = static_cast<std::uint8_t>(n);
    return id;
}
class PoweredGame final : public Game {
    bool valid_ = true;

  public:
    bool on_start(Context &c) override {
        HostApi legacy;
        legacy.size = static_cast<std::uint32_t>(offsetof(HostApi, read_swing_twist_joint));
        Context old_host{&legacy};
        SwingTwistSnapshot missing;
        SwingTwistDrive command;
        if (old_host.read_swing_twist_joint(joint(3), missing) != Result::unavailable ||
            old_host.control_swing_twist_joint(joint(3), command) != Result::unavailable)
            return false;
        SwingTwistSnapshot h;
        valid_ = c.read_swing_twist_joint(joint(3), h) == Result::success && h.simulated &&
                 h.first.bytes[15] == 1 && h.second.bytes[15] == 2;
        h.size = 0;
        valid_ &= c.read_swing_twist_joint(joint(3), h) == Result::invalid_argument;
        SwingTwistDrive d;
        d.target_orientation[0] = -.19866933F;
        d.target_orientation[3] = .98006658F;
        d.max_swing_axis_torque = 1000;
        d.max_twist_torque = 700;
        auto invalid = d;
        invalid.size = 0;
        valid_ &= c.control_swing_twist_joint(joint(3), invalid) == Result::invalid_argument;
        invalid = d;
        invalid.swing_mode = static_cast<JointDriveMode>(99);
        valid_ &= c.control_swing_twist_joint(joint(3), invalid) == Result::invalid_argument;
        valid_ &= c.control_swing_twist_joint(joint(2), d) == Result::no_link;
        Result background = Result::success;
        std::thread t([&] {
            SwingTwistSnapshot out;
            background = c.read_swing_twist_joint(joint(3), out);
        });
        t.join();
        valid_ &= background == Result::wrong_phase;
        return valid_ && c.control_swing_twist_joint(joint(3), d) == Result::success;
    }
    void on_frame(Context &c, Frame &frame) override {
        SwingTwistSnapshot h;
        SwingTwistDrive d;
        valid_ &= c.control_swing_twist_joint(joint(3), d) == Result::wrong_phase;
        valid_ &= c.read_swing_twist_joint(joint(3), h) == Result::success &&
                  h.drive.target_orientation[0] < 0;
        frame.clear_color = {valid_ ? 1.0F : 0, 0, 0, 1};
    }
};
} // namespace
VICE_GAME_MODULE(PoweredGame)
