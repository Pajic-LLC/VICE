#include <vice/runtime/module_api.hpp>

namespace {

class TestGame final : public vice::runtime::module::Game {
public:
    bool on_start(vice::runtime::module::Context& context) override {
        return context.entity_count() == 1U && context.resource_count() == 0U;
    }

    void on_frame(
        vice::runtime::module::Context&,
        vice::runtime::module::Frame& frame
    ) override {
        frame.clear_color = {0.75F, 0.25F, 0.125F, 1.0F};
    }
};

} // namespace

VICE_GAME_MODULE(TestGame)
