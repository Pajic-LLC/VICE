#include <vice/runtime/module_api.hpp>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(std::uint8_t last) {
    EntityId id{}; id.bytes[0]=0x97; id.bytes[6]=0x40; id.bytes[8]=0x80; id.bytes[14]=2; id.bytes[15]=last; return id;
}
class BreakableLinks final : public Game {
    double elapsed_ = 0;
    bool driven_ = false;
public:
    bool on_start(Context& context) override {
        DistanceLinkSnapshot link;
        if (context.read_distance_link(entity(3), link) != Result::success) return false;
        driven_ = link.broken != 0;
        return true;
    }
    void on_update(Context& context, double delta) override {
        elapsed_ += delta;
        if (!driven_ && elapsed_ >= 1.0) {
            driven_ = context.set_body_velocity(entity(2), {1,-8,0}) == Result::success;
        }
    }
    void on_frame(Context& context, Frame& frame) override {
        DistanceLinkSnapshot weak, strong;
        if (context.read_distance_link(entity(3), weak) == Result::success &&
            context.read_distance_link(entity(6), strong) == Result::success && weak.broken && !strong.broken)
            frame.clear_color = {0.08F,0.035F,0.02F,1};
    }
};
}
VICE_GAME_MODULE(BreakableLinks)
