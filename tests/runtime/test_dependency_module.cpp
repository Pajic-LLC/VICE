#include <vice/runtime/module_api.hpp>
#include <thread>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(std::uint8_t last) {
    EntityId id{}; id.bytes[0]=0x98; id.bytes[6]=0x40; id.bytes[8]=0x80; id.bytes[15]=last; return id;
}
class SupplyGame final : public Game {
public:
    bool on_start(Context& context) override {
        DependencySnapshot state;
        if (context.read_dependency(entity(6), state) != Result::success || !state.available || state.path_length != 3) return false;
        if (context.read_dependency(entity(3), state) != Result::no_dependency || context.read_dependency(entity(99), state) != Result::not_found) return false;
        state.size = 0;
        if (context.read_dependency(entity(6), state) != Result::invalid_argument) return false;
        state.size = sizeof(state);
        if (context.read_dependency({}, state) != Result::invalid_argument) return false;
        Result background = Result::success;
        std::thread worker{[&] { DependencySnapshot d; background = context.read_dependency(entity(6), d); }};
        worker.join(); return background == Result::wrong_phase;
    }
    void on_event(Context& context, const Event& event) override {
        if (event.kind == EventKind::key && (event.flags & 1U)) (void)context.set_body_velocity(entity(2), {0,-8,0});
    }
    void on_frame(Context& context, Frame& frame) override {
        DependencySnapshot failed, independent;
        const bool ok = context.read_dependency(entity(6), failed) == Result::success &&
            context.read_dependency(entity(7), independent) == Result::success;
        frame.clear_color = {ok && !failed.available && independent.available ? 1.0F : 0.0F,
            failed.root_reason == DependencyReason::link_broken && failed.root_cause.bytes[15] == 3 ? 1.0F : 0.0F,
            failed.break_impulse > 1000 && failed.break_step && failed.path_length == 3 ? 1.0F : 0.0F, 1};
    }
};
}
VICE_GAME_MODULE(SupplyGame)
