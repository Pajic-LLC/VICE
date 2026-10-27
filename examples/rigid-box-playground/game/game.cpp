#include <vice/runtime/module_api.hpp>

namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(std::uint8_t last) {
    EntityId id{};
    id.bytes[0] = 0x94; id.bytes[6] = 0x40; id.bytes[8] = 0x80;
    id.bytes[14] = 2; id.bytes[15] = last;
    return id;
}
constexpr auto actor = entity(2);
constexpr auto mover = entity(3);
class Playground final : public Game {
    bool left_ = false, right_ = false, forward_ = false, backward_ = false, jump_ = false;
    float direction_ = 1;
public:
    bool on_start(Context& context) override {
        EntitySnapshot state;
        if (context.read_entity(actor, state) != Result::success || state.motion != Motion::dynamic_body) return false;
        context.log(LogLevel::info, "Rigid Box Playground: arrow keys move, Space jumps from physical support.");
        return true;
    }
    void on_event(Context&, const Event& event) override {
        if (event.kind == EventKind::window_focus_changed && !(event.flags & 1U)) {
            left_ = right_ = forward_ = backward_ = jump_ = false;
        }
        if (event.kind != EventKind::key) return;
        const bool down = (event.flags & 1U) != 0;
        switch (static_cast<Key>(event.code)) {
        case Key::left: left_ = down; break;
        case Key::right: right_ = down; break;
        case Key::up: forward_ = down; break;
        case Key::down: backward_ = down; break;
        case Key::space: if (down && !(event.flags & 2U)) jump_ = true; break;
        default: break;
        }
    }
    void on_update(Context& context, double) override {
        EntitySnapshot body;
        if (context.read_entity(actor, body) == Result::success) {
            auto velocity = body.linear_velocity;
            SupportSnapshot support;
            const bool grounded = context.read_body_support(actor, support) == Result::success && support.supported != 0;
            velocity.x = (static_cast<float>(right_) - static_cast<float>(left_)) * 4 + (grounded ? support.velocity.x : 0);
            velocity.z = (static_cast<float>(backward_) - static_cast<float>(forward_)) * 4 + (grounded ? support.velocity.z : 0);
            if (jump_ && grounded && velocity.y - support.velocity.y <= 0.1F) velocity.y = support.velocity.y + 6;
            (void)context.set_body_velocity(actor, velocity);
        }
        jump_ = false;
        if (context.read_entity(mover, body) == Result::success) {
            if (body.position[0] > 1000000000004.0) direction_ = -1;
            if (body.position[0] < 999999999996.0) direction_ = 1;
            (void)context.set_body_velocity(mover, {direction_ * 1.5F, 0, 0});
        }
    }
};
}
VICE_GAME_MODULE(Playground)
