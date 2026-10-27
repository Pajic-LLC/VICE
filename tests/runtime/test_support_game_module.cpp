#include <vice/runtime/module_api.hpp>
#include <thread>
namespace {
using namespace vice::runtime::module;
constexpr EntityId actor() {
    EntityId id{}; id.bytes[0] = 0x95; id.bytes[6] = 0x40; id.bytes[8] = 0x80; id.bytes[15] = 2; return id;
}
class SupportGame final : public Game {
    bool jump_ = false;
    bool valid_ = true;
    float jumps_ = 0;
public:
    bool on_start(Context& context) override {
        SupportSnapshot support;
        valid_ = context.read_body_support(actor(), support) == Result::success && support.supported == 1 &&
            support.normal.y > 0.7F && support.supporting_entity.bytes[15] == 1;
        valid_ &= context.read_body_support(actor(), support, {}) == Result::invalid_argument;
        SupportSnapshot short_record; short_record.size = 0;
        valid_ &= context.read_body_support(actor(), short_record) == Result::invalid_argument;
        Result background = Result::success;
        std::thread worker{[&] { SupportSnapshot result; background = context.read_body_support(actor(), result); }};
        worker.join();
        return valid_ && background == Result::wrong_phase;
    }
    void on_event(Context&, const Event& event) override { if (event.kind == EventKind::key && (event.flags & 1U)) jump_ = true; }
    void on_update(Context& context, double) override {
        SupportSnapshot support;
        EntitySnapshot body;
        valid_ &= context.read_body_support(actor(), support) == Result::success && context.read_entity(actor(), body) == Result::success;
        if (jump_ && support.supported && body.linear_velocity.y - support.velocity.y <= 0.1F) {
            auto velocity = body.linear_velocity; velocity.y = support.velocity.y + 6;
            valid_ &= context.set_body_velocity(actor(), velocity) == Result::success;
            ++jumps_;
        }
        jump_ = false;
    }
    void on_frame(Context&, Frame& frame) override { frame.clear_color = {valid_ ? 0.75F : 0.0F, jumps_, 0, 1}; }
};
}
VICE_GAME_MODULE(SupportGame)
