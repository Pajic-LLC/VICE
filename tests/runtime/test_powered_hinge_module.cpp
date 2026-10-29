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
        PoweredHingeSnapshot h;
        valid_ = c.read_powered_hinge(joint(3), h) == Result::success && h.simulated &&
                 h.first.bytes[15] == 1 && h.second.bytes[15] == 2;
        h.size = 0;
        valid_ &= c.read_powered_hinge(joint(3), h) == Result::invalid_argument;
        JointDrive d;
        d.target_angle = -.4F;
        d.max_torque = 1000;
        auto invalid = d;
        invalid.size = 0;
        valid_ &= c.control_powered_hinge(joint(3), invalid) == Result::invalid_argument;
        valid_ &= c.control_powered_hinge(joint(2), d) == Result::no_link;
        Result background = Result::success;
        std::thread t([&] {
            PoweredHingeSnapshot out;
            background = c.read_powered_hinge(joint(3), out);
        });
        t.join();
        valid_ &= background == Result::wrong_phase;
        return valid_ && c.control_powered_hinge(joint(3), d) == Result::success;
    }
    void on_frame(Context &c, Frame &frame) override {
        PoweredHingeSnapshot h;
        JointDrive d;
        valid_ &= c.control_powered_hinge(joint(3), d) == Result::wrong_phase;
        valid_ &= c.read_powered_hinge(joint(3), h) == Result::success && h.drive.target_angle < 0;
        frame.clear_color = {valid_ ? 1.0F : 0, 0, 0, 1};
    }
};
} // namespace
VICE_GAME_MODULE(PoweredGame)
