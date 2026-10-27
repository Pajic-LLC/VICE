#include <vice/runtime/module_api.hpp>
#include <thread>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(std::uint8_t last) {
    EntityId id{}; id.bytes[0]=0x96; id.bytes[6]=0x40; id.bytes[8]=0x80; id.bytes[15]=last; return id;
}
class LinkGame final : public Game {
    bool valid_ = true;
public:
    bool on_start(Context& context) override {
        DistanceLinkSnapshot link;
        valid_ = context.read_distance_link(entity(3), link) == Result::success && !link.broken && link.first.bytes[15] == 1 && link.second.bytes[15] == 2;
        valid_ &= context.read_distance_link(entity(2), link) == Result::no_link;
        link.size = 0;
        valid_ &= context.read_distance_link(entity(3), link) == Result::invalid_argument;
        Result background = Result::success;
        std::thread worker{[&] { DistanceLinkSnapshot state; background = context.read_distance_link(entity(3), state); }};
        worker.join();
        return valid_ && background == Result::wrong_phase;
    }
    void on_event(Context& context, const Event& event) override {
        if (event.kind == EventKind::key && (event.flags & 1U)) valid_ &= context.set_body_velocity(entity(2), {0,-8,0}) == Result::success;
    }
    void on_frame(Context& context, Frame& frame) override {
        DistanceLinkSnapshot link;
        valid_ &= context.read_distance_link(entity(3), link) == Result::success;
        frame.clear_color = {valid_ && link.broken ? 1.0F : 0.0F, link.last_break_impulse > link.break_threshold ? 1.0F : 0.0F, link.break_step ? 1.0F : 0.0F, 1};
    }
};
}
VICE_GAME_MODULE(LinkGame)
