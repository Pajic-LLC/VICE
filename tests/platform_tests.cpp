#include <vice/platform/event.hpp>
#include <vice/platform/native_window.hpp>
#include <vice/platform/window.hpp>

#include <cassert>
#include <variant>

int main() {
    using namespace vice::platform;

    const WindowMetrics metrics{1280, 720, 2560, 1440, 2.0F};
    const Event resize{WindowMetricsChanged{7, metrics}};
    assert(std::holds_alternative<WindowMetricsChanged>(resize.payload));

    const auto& changed = std::get<WindowMetricsChanged>(resize.payload);
    assert(changed.window == 7);
    assert(changed.metrics == metrics);

    ModifierMask modifiers = modifier_mask(Modifier::shift);
    modifiers |= modifier_mask(Modifier::control);
    assert(has_modifier(modifiers, Modifier::shift));
    assert(has_modifier(modifiers, Modifier::control));
    assert(!has_modifier(modifiers, Modifier::alt));

    const Event key{KeyEvent{7, Key::escape, 53, modifiers, true, false}};
    const auto& key_event = std::get<KeyEvent>(key.payload);
    assert(key_event.window == 7);
    assert(key_event.key == Key::escape);
    assert(key_event.pressed);

    const NativeWindowHandle handle{};
    assert(handle.system == NativeWindowSystem::unknown);
    assert(handle.window == nullptr);
    assert(handle.view == nullptr);

    return 0;
}
