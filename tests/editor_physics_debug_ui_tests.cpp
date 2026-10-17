#include <vice/editor/physics_debug_ui.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "physics debug UI test failed: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    vice::editor::PhysicsDebugUi ui;
    const auto layers = ui.layers();
    require(layers.physics, "Physics is visible by default");
    require(layers.regions, "Regions are visible by default");
    require(!layers.contacts, "Contacts stay opt-in");
    require(layers.velocities, "Velocity vectors follow Physics by default");
    require(layers.characters, "Character state follows Physics by default");
    require(!layers.handoffs, "Handoffs stay advanced");
    require(!ui.consume_start_request(), "no phantom Start request");
    require(!ui.consume_reset_request(), "no phantom Reset request");
    require(!ui.consume_stop_request(), "no phantom Stop request");
    require(!ui.consume_step_request(), "no phantom Step request");
    require(!ui.consume_play_pause_request(), "no phantom playback request");

    ui.request_play();
    require(ui.consume_start_request(), "Play requests a fresh runtime session");
    require(ui.consume_play_pause_request(), "Play requests immediate running playback");
    require(!ui.consume_start_request(), "Play start request is consumed once");
    require(!ui.consume_play_pause_request(), "Play playback request is consumed once");

    return 0;
}
