#pragma once

#include <vice/core/time.hpp>
#include <vice/platform/types.hpp>

#include <cstdint>
#include <utility>
#include <variant>

namespace vice::platform {

enum class Key : std::uint16_t {
    unknown,
    escape,
    enter,
    tab,
    backspace,
    space,
    left,
    right,
    up,
    down,
    home,
    end,
    page_up,
    page_down,
    insert,
    delete_key,
    a,
    b,
    c,
    d,
    e,
    f,
    g,
    h,
    i,
    j,
    k,
    l,
    m,
    n,
    o,
    p,
    q,
    r,
    s,
    t,
    u,
    v,
    w,
    x,
    y,
    z,
    digit_0,
    digit_1,
    digit_2,
    digit_3,
    digit_4,
    digit_5,
    digit_6,
    digit_7,
    digit_8,
    digit_9,
    f1,
    f2,
    f3,
    f4,
    f5,
    f6,
    f7,
    f8,
    f9,
    f10,
    f11,
    f12,
};

enum class MouseButton : std::uint8_t {
    left,
    right,
    middle,
    button_4,
    button_5,
    other,
};

enum class ScrollUnit : std::uint8_t {
    lines,
    pixels,
};

enum class Modifier : std::uint16_t {
    shift = 1U << 0U,
    control = 1U << 1U,
    alt = 1U << 2U,
    super = 1U << 3U,
    caps_lock = 1U << 4U,
};

using ModifierMask = std::uint16_t;

[[nodiscard]] constexpr ModifierMask modifier_mask(const Modifier modifier) noexcept {
    return static_cast<ModifierMask>(modifier);
}

[[nodiscard]] constexpr bool has_modifier(const ModifierMask mask, const Modifier modifier) noexcept {
    return (mask & modifier_mask(modifier)) != 0U;
}

struct QuitRequested {};

struct WindowCloseRequested {
    WindowId window = invalid_window_id;
};

struct WindowMetricsChanged {
    WindowId window = invalid_window_id;
    WindowMetrics metrics{};
};

struct WindowFocusChanged {
    WindowId window = invalid_window_id;
    bool focused = false;
};

struct KeyEvent {
    WindowId window = invalid_window_id;
    Key key = Key::unknown;
    std::uint32_t native_code = 0;
    ModifierMask modifiers = 0;
    bool pressed = false;
    bool repeat = false;
};

struct TextInputEvent {
    WindowId window = invalid_window_id;
    char32_t codepoint = U'\0';
};

struct MouseMoveEvent {
    WindowId window = invalid_window_id;
    double x = 0.0;
    double y = 0.0;
    double delta_x = 0.0;
    double delta_y = 0.0;
};

struct MouseButtonEvent {
    WindowId window = invalid_window_id;
    MouseButton button = MouseButton::other;
    double x = 0.0;
    double y = 0.0;
    bool pressed = false;
};

struct MouseWheelEvent {
    WindowId window = invalid_window_id;
    double delta_x = 0.0;
    double delta_y = 0.0;
    ScrollUnit unit = ScrollUnit::lines;
};

using EventPayload = std::variant<
    QuitRequested,
    WindowCloseRequested,
    WindowMetricsChanged,
    WindowFocusChanged,
    KeyEvent,
    TextInputEvent,
    MouseMoveEvent,
    MouseButtonEvent,
    MouseWheelEvent
>;

struct Event {
    core::TimePoint timestamp{};
    EventPayload payload{QuitRequested{}};

    template <typename Payload>
    explicit Event(Payload event_payload, core::TimePoint event_time = core::monotonic_now())
        : timestamp{event_time}, payload{std::move(event_payload)} {}
};

} // namespace vice::platform
