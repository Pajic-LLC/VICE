#include <vice/runtime/module_api.hpp>

#include <limits>
#include <optional>
#include <thread>

namespace {
using namespace vice::runtime::module;
std::optional<Context> saved_context;
constexpr EntityId entity(std::uint8_t last) {
    EntityId id{};
    id.bytes[0] = 0x93; id.bytes[6] = 0x40; id.bytes[8] = 0x80; id.bytes[15] = last;
    return id;
}
class Gameplay final : public Game {
    bool ok_ = true;
public:
    bool on_start(Context& context) override {
        saved_context = context;
        EntitySnapshot state;
        ok_ = context.read_entity(entity(2), state) == Result::success && state.flags == 3 &&
            state.position[0] == 1000000000000.25 && state.motion == Motion::dynamic_body;
        EntitySnapshot too_small; too_small.size = 0;
        ok_ &= context.read_entity(entity(2), too_small) == Result::invalid_argument;
        ok_ &= context.read_entity(entity(99), state) == Result::not_found;
        ok_ &= context.set_body_velocity(entity(1), {1, 0, 0}) == Result::static_body;
        ok_ &= context.set_body_velocity(entity(4), {1, 0, 0}) == Result::inactive_region;
        ok_ &= context.set_body_velocity(entity(5), {1, 0, 0}) == Result::no_body;
        ok_ &= context.set_body_velocity(entity(2), {std::numeric_limits<float>::quiet_NaN(), 0, 0}) == Result::invalid_argument;
        Result background = Result::success;
        std::thread worker{[&] {
            EntitySnapshot snapshot;
            background = context.read_entity(entity(2), snapshot);
            if (context.set_body_velocity(entity(2), {}) != Result::wrong_phase) background = Result::success;
        }};
        worker.join();
        ok_ &= background == Result::wrong_phase;
        ok_ &= context.set_body_velocity(entity(2), {3, 4, 0}, {0, 1, 0}) == Result::success;
        ok_ &= context.read_entity(entity(2), state) == Result::success && state.linear_velocity.x == 3 && state.linear_velocity.y == 4;
        return ok_;
    }
    void on_event(Context& context, const Event& event) override {
        if (event.kind == EventKind::key && (event.flags & 1U))
            ok_ &= context.set_body_velocity(entity(2), {5, 8, 0}) == Result::success;
    }
    void on_update(Context& context, double) override {
        ok_ &= context.set_body_velocity(entity(3), {2, 0, 0}) == Result::success;
    }
    void on_frame(Context& context, Frame& frame) override {
        EntitySnapshot state;
        ok_ &= context.read_entity(entity(2), state) == Result::success;
        ok_ &= context.set_body_velocity(entity(2), {}) == Result::wrong_phase;
        frame.clear_color.red = ok_ ? 0.75F : 0.0F;
    }
    void on_stop(Context& context) override {
        ok_ &= context.set_body_velocity(entity(2), {}) == Result::wrong_phase;
    }
};
}
VICE_GAME_MODULE(Gameplay)
extern "C" VICE_GAME_MODULE_EXPORT std::uint32_t vice_gameplay_probe() noexcept {
    vice::runtime::module::EntitySnapshot snapshot;
    return static_cast<std::uint32_t>(saved_context ? saved_context->read_entity(entity(2), snapshot) : Result::unavailable);
}
