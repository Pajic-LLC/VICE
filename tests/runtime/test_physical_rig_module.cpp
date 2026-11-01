#include <thread>
#include <vice/runtime/module_api.hpp>
namespace {
using namespace vice::runtime::module;
EntityId id(unsigned n) {
    EntityId id{};
    id.bytes[0] = 0xea;
    id.bytes[6] = 0x40;
    id.bytes[8] = 0x80;
    id.bytes[15] = static_cast<std::uint8_t>(n);
    return id;
}
class Test final : public Game {
    bool ok = true;

  public:
    bool on_start(Context &c) override {
        PhysicalRigSnapshot out;
        ok = c.read_physical_rig(id(3), out) == Result::success && out.root.bytes[15] == 1 &&
             out.node == 2;
        out.size = 0;
        ok &= c.read_physical_rig(id(3), out) == Result::invalid_argument;
        out.size = sizeof(out);
        ok &= c.read_physical_rig(id(1), out) == Result::no_component;
        HostApi legacy;
        legacy.size = static_cast<std::uint32_t>(offsetof(HostApi, read_physical_rig));
        Context old{&legacy};
        ok &= old.read_physical_rig(id(3), out) == Result::unavailable;
        Result background = Result::success;
        std::thread thread([&] { background = c.read_physical_rig(id(3), out); });
        thread.join();
        ok &= background == Result::wrong_phase;
        return ok;
    }
    void on_frame(Context &c, Frame &f) override {
        PhysicalRigSnapshot out;
        ok &= c.read_physical_rig(id(3), out) == Result::success;
        f.clear_color = {ok ? 1.F : 0, 0, 0, 1};
    }
};
} // namespace
VICE_GAME_MODULE(Test)
