#include <vice/runtime/module_api.hpp>
#include <cstdlib>
#include <fstream>

namespace {
using namespace vice::runtime::module;
int starts = 0;
constexpr EntityId actor() {
    EntityId id{}; id.bytes[0] = 0x94; id.bytes[6] = 0x40; id.bytes[8] = 0x80;
    id.bytes[14] = 2; id.bytes[15] = 2; return id;
}
void trace(const char* event) {
    if (const auto* path = std::getenv("VICE_TEST_PREVIEW_TRACE")) std::ofstream{path, std::ios::app} << event << '\n';
}
class PreviewGame final : public Game {
    bool held_ = false;
    bool valid_ = true;
public:
    ~PreviewGame() override { trace("destroy"); }
    bool on_start(Context& context) override {
        trace("start");
        EntitySnapshot entity;
        return ++starts == 1 && context.read_entity(actor(), entity) == Result::success &&
            context.set_body_velocity(actor(), {1, 0, 0}) == Result::success;
    }
    void on_event(Context&, const Event& event) override {
        trace("event");
        if (event.kind == EventKind::key && event.code == static_cast<std::uint32_t>(Key::right)) held_ = (event.flags & 1U) != 0;
        if (event.kind == EventKind::window_focus_changed && !(event.flags & 1U)) held_ = false;
        if (event.kind == EventKind::mouse_move) valid_ &= event.x == 10 && event.y == 20;
    }
    void on_update(Context& context, double) override {
        trace("update");
        EntitySnapshot entity;
        valid_ &= context.read_entity(actor(), entity) == Result::success;
        auto velocity = entity.linear_velocity;
        velocity.x = held_ ? 5.0F : 1.0F;
        valid_ &= context.set_body_velocity(actor(), velocity) == Result::success;
    }
    void on_frame(Context& context, Frame& frame) override {
        trace("frame");
        valid_ &= context.set_body_velocity(actor(), {}) == Result::wrong_phase;
        frame.clear_color.red = valid_ ? 0.75F : 0;
    }
    void on_stop(Context&) override { trace("stop"); }
};
}
VICE_GAME_MODULE(PreviewGame)
