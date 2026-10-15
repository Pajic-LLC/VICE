#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <vice/core/log.hpp>
#include <vice/platform/native_window.hpp>
#include <vice/platform/services.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

@interface VICEApplicationDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) void* service;
@end

@interface VICEWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) void* service;
@property(nonatomic, assign) std::uint32_t windowId;
@end

namespace vice::platform {
namespace {

[[nodiscard]] NSString* make_ns_string(const std::string_view text) {
    NSString* value = [[NSString alloc]
        initWithBytes:text.data()
        length:text.size()
        encoding:NSUTF8StringEncoding];
    return value != nil ? value : @"VICE";
}

[[nodiscard]] std::filesystem::path path_from_ns_string(NSString* value) {
    if (value == nil) {
        return {};
    }
    const char* utf8 = [value UTF8String];
    return utf8 != nullptr ? std::filesystem::path{std::string{utf8}} : std::filesystem::path{};
}

[[nodiscard]] WindowMetrics window_metrics(NSWindow* window) noexcept {
    if (window == nil) {
        return {};
    }

    NSView* content_view = [window contentView];
    if (content_view == nil) {
        return {};
    }

    const NSRect logical_bounds = [content_view bounds];
    const NSRect backing_bounds = [content_view convertRectToBacking:logical_bounds];
    const CGFloat scale = [window backingScaleFactor];

    const auto logical_width = static_cast<std::uint32_t>(std::max(0.0, std::round(logical_bounds.size.width)));
    const auto logical_height = static_cast<std::uint32_t>(std::max(0.0, std::round(logical_bounds.size.height)));
    const auto drawable_width = static_cast<std::uint32_t>(std::max(0.0, std::round(backing_bounds.size.width)));
    const auto drawable_height = static_cast<std::uint32_t>(std::max(0.0, std::round(backing_bounds.size.height)));

    return WindowMetrics{
        logical_width,
        logical_height,
        drawable_width,
        drawable_height,
        static_cast<float>(scale),
    };
}

[[nodiscard]] ModifierMask modifiers_for(const NSEventModifierFlags flags) noexcept {
    ModifierMask modifiers = 0;
    if ((flags & NSEventModifierFlagShift) != 0U) {
        modifiers |= modifier_mask(Modifier::shift);
    }
    if ((flags & NSEventModifierFlagControl) != 0U) {
        modifiers |= modifier_mask(Modifier::control);
    }
    if ((flags & NSEventModifierFlagOption) != 0U) {
        modifiers |= modifier_mask(Modifier::alt);
    }
    if ((flags & NSEventModifierFlagCommand) != 0U) {
        modifiers |= modifier_mask(Modifier::super);
    }
    if ((flags & NSEventModifierFlagCapsLock) != 0U) {
        modifiers |= modifier_mask(Modifier::caps_lock);
    }
    return modifiers;
}

[[nodiscard]] Key key_for(const unsigned short key_code) noexcept {
    switch (key_code) {
    case 53: return Key::escape;
    case 36: return Key::enter;
    case 48: return Key::tab;
    case 51: return Key::backspace;
    case 49: return Key::space;
    case 123: return Key::left;
    case 124: return Key::right;
    case 125: return Key::down;
    case 126: return Key::up;
    case 115: return Key::home;
    case 119: return Key::end;
    case 116: return Key::page_up;
    case 121: return Key::page_down;
    case 114: return Key::insert;
    case 117: return Key::delete_key;
    case 0: return Key::a;
    case 11: return Key::b;
    case 8: return Key::c;
    case 2: return Key::d;
    case 14: return Key::e;
    case 3: return Key::f;
    case 5: return Key::g;
    case 4: return Key::h;
    case 34: return Key::i;
    case 38: return Key::j;
    case 40: return Key::k;
    case 37: return Key::l;
    case 46: return Key::m;
    case 45: return Key::n;
    case 31: return Key::o;
    case 35: return Key::p;
    case 12: return Key::q;
    case 15: return Key::r;
    case 1: return Key::s;
    case 17: return Key::t;
    case 32: return Key::u;
    case 9: return Key::v;
    case 13: return Key::w;
    case 7: return Key::x;
    case 16: return Key::y;
    case 6: return Key::z;
    case 29: return Key::digit_0;
    case 18: return Key::digit_1;
    case 19: return Key::digit_2;
    case 20: return Key::digit_3;
    case 21: return Key::digit_4;
    case 23: return Key::digit_5;
    case 22: return Key::digit_6;
    case 26: return Key::digit_7;
    case 28: return Key::digit_8;
    case 25: return Key::digit_9;
    case 122: return Key::f1;
    case 120: return Key::f2;
    case 99: return Key::f3;
    case 118: return Key::f4;
    case 96: return Key::f5;
    case 97: return Key::f6;
    case 98: return Key::f7;
    case 100: return Key::f8;
    case 101: return Key::f9;
    case 109: return Key::f10;
    case 103: return Key::f11;
    case 111: return Key::f12;
    default: return Key::unknown;
    }
}

[[nodiscard]] MouseButton mouse_button_for(const NSInteger button_number) noexcept {
    switch (button_number) {
    case 0: return MouseButton::left;
    case 1: return MouseButton::right;
    case 2: return MouseButton::middle;
    case 3: return MouseButton::button_4;
    case 4: return MouseButton::button_5;
    default: return MouseButton::other;
    }
}

void emit_text_events(
    const WindowId window,
    NSString* characters,
    std::deque<Event>& events
) {
    if (characters == nil) {
        return;
    }

    const NSUInteger length = [characters length];
    for (NSUInteger index = 0; index < length; ++index) {
        const unichar first = [characters characterAtIndex:index];
        char32_t codepoint = static_cast<char32_t>(first);

        if (CFStringIsSurrogateHighCharacter(first) && index + 1U < length) {
            const unichar second = [characters characterAtIndex:index + 1U];
            if (CFStringIsSurrogateLowCharacter(second)) {
                codepoint = static_cast<char32_t>(CFStringGetLongCharacterForSurrogatePair(first, second));
                ++index;
            }
        }

        if (codepoint >= U' ' && codepoint != U'\x7f') {
            events.emplace_back(TextInputEvent{window, codepoint});
        }
    }
}

} // namespace

class MacPlatformServices;

class MacWindow final : public Window {
public:
    MacWindow(
        MacPlatformServices& platform,
        WindowId id,
        NSWindow* window,
        VICEWindowDelegate* delegate
    ) noexcept;
    ~MacWindow() override;

    [[nodiscard]] WindowId id() const noexcept override;
    [[nodiscard]] WindowMetrics metrics() const noexcept override;
    [[nodiscard]] bool visible() const noexcept override;

    void set_title(std::string_view title) override;
    void show() override;
    void hide() override;

private:
    [[nodiscard]] NativeWindowHandle native_handle() const noexcept override;

    MacPlatformServices* platform_ = nullptr;
    WindowId id_ = invalid_window_id;
    NSWindow* window_ = nil;
    VICEWindowDelegate* delegate_ = nil;
};

class MacPlatformServices final : public PlatformServices {
public:
    MacPlatformServices() {
        @autoreleasepool {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            app_delegate_ = [[VICEApplicationDelegate alloc] init];
            app_delegate_.service = this;
            [NSApp setDelegate:app_delegate_];
            [NSApp finishLaunching];
        }
    }

    ~MacPlatformServices() override {
        @autoreleasepool {
            if ([NSApp delegate] == app_delegate_) {
                [NSApp setDelegate:nil];
            }
            app_delegate_ = nil;
        }
    }

    [[nodiscard]] std::unique_ptr<Window> create_window(const WindowDesc& desc) override {
        @autoreleasepool {
            NSWindowStyleMask style = NSWindowStyleMaskTitled |
                NSWindowStyleMaskClosable |
                NSWindowStyleMaskMiniaturizable;
            if (desc.resizable) {
                style |= NSWindowStyleMaskResizable;
            }

            const NSRect content_rect = NSMakeRect(
                0.0,
                0.0,
                static_cast<CGFloat>(desc.width),
                static_cast<CGFloat>(desc.height)
            );

            NSWindow* window = [[NSWindow alloc]
                initWithContentRect:content_rect
                styleMask:style
                backing:NSBackingStoreBuffered
                defer:NO];
            if (window == nil) {
                core::log(core::LogLevel::error, "AppKit could not create a VICE window.");
                return nullptr;
            }

            const WindowId id = next_window_id_++;
            VICEWindowDelegate* delegate = [[VICEWindowDelegate alloc] init];
            delegate.service = this;
            delegate.windowId = id;

            [window setDelegate:delegate];
            [window setReleasedWhenClosed:NO];
            [window setTitle:make_ns_string(desc.title)];
            [window setAcceptsMouseMovedEvents:YES];
            [window center];

            register_window(window, id);

            auto result = std::make_unique<MacWindow>(*this, id, window, delegate);
            if (desc.visible) {
                result->show();
            }
            return result;
        }
    }

    bool poll_event(Event& event) override {
        @autoreleasepool {
            if (events_.empty()) {
                for (;;) {
                    NSEvent* native_event = [NSApp
                        nextEventMatchingMask:NSEventMaskAny
                        untilDate:[NSDate distantPast]
                        inMode:NSDefaultRunLoopMode
                        dequeue:YES];
                    if (native_event == nil) {
                        break;
                    }

                    translate_input(native_event);
                    [NSApp sendEvent:native_event];
                    [NSApp updateWindows];
                }
            }
        }

        if (events_.empty()) {
            return false;
        }

        event = std::move(events_.front());
        events_.pop_front();
        return true;
    }

    void request_quit() override {
        events_.emplace_back(QuitRequested{});
    }

    [[nodiscard]] PlatformPaths paths() const override {
        @autoreleasepool {
            PlatformPaths result;
            result.executable = path_from_ns_string([[NSBundle mainBundle] executablePath]);

            NSArray<NSString*>* app_support_paths = NSSearchPathForDirectoriesInDomains(
                NSApplicationSupportDirectory,
                NSUserDomainMask,
                YES
            );
            if ([app_support_paths count] > 0U) {
                result.user_data = path_from_ns_string([app_support_paths objectAtIndex:0]) /
                    "Pajic Studios" / "VICE";
            }

            result.temporary = path_from_ns_string(NSTemporaryDirectory());
            return result;
        }
    }

    void push(Event event) {
        events_.push_back(std::move(event));
    }

    void register_window(NSWindow* window, const WindowId id) {
        windows_.insert_or_assign((__bridge void*)window, id);
    }

    void unregister_window(NSWindow* window) {
        if (window != nil) {
            windows_.erase((__bridge void*)window);
        }
    }

    [[nodiscard]] WindowId window_id(NSWindow* window) const noexcept {
        if (window == nil) {
            return invalid_window_id;
        }
        const auto found = windows_.find((__bridge void*)window);
        return found == windows_.end() ? invalid_window_id : found->second;
    }

private:
    void translate_input(NSEvent* native_event) {
        NSWindow* native_window = [native_event window];
        const WindowId id = window_id(native_window);
        if (id == invalid_window_id) {
            return;
        }

        switch ([native_event type]) {
        case NSEventTypeKeyDown: {
            const ModifierMask modifiers = modifiers_for([native_event modifierFlags]);
            const unsigned short native_code = [native_event keyCode];
            events_.emplace_back(KeyEvent{
                id,
                key_for(native_code),
                static_cast<std::uint32_t>(native_code),
                modifiers,
                true,
                [native_event isARepeat] == YES,
            });
            if (!has_modifier(modifiers, Modifier::super) && !has_modifier(modifiers, Modifier::control)) {
                emit_text_events(id, [native_event characters], events_);
            }
            break;
        }
        case NSEventTypeKeyUp: {
            const unsigned short native_code = [native_event keyCode];
            events_.emplace_back(KeyEvent{
                id,
                key_for(native_code),
                static_cast<std::uint32_t>(native_code),
                modifiers_for([native_event modifierFlags]),
                false,
                false,
            });
            break;
        }
        case NSEventTypeMouseMoved:
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
        case NSEventTypeOtherMouseDragged: {
            NSPoint position = [native_event locationInWindow];
            NSView* content_view = [native_window contentView];
            const NSRect bounds = content_view != nil ? [content_view bounds] : NSZeroRect;
            events_.emplace_back(MouseMoveEvent{
                id,
                static_cast<double>(position.x),
                static_cast<double>(bounds.size.height - position.y),
                static_cast<double>([native_event deltaX]),
                static_cast<double>([native_event deltaY]),
            });
            break;
        }
        case NSEventTypeLeftMouseDown:
        case NSEventTypeRightMouseDown:
        case NSEventTypeOtherMouseDown:
        case NSEventTypeLeftMouseUp:
        case NSEventTypeRightMouseUp:
        case NSEventTypeOtherMouseUp: {
            const NSEventType type = [native_event type];
            const bool pressed = type == NSEventTypeLeftMouseDown ||
                type == NSEventTypeRightMouseDown ||
                type == NSEventTypeOtherMouseDown;
            NSPoint position = [native_event locationInWindow];
            NSView* content_view = [native_window contentView];
            const NSRect bounds = content_view != nil ? [content_view bounds] : NSZeroRect;
            events_.emplace_back(MouseButtonEvent{
                id,
                mouse_button_for([native_event buttonNumber]),
                static_cast<double>(position.x),
                static_cast<double>(bounds.size.height - position.y),
                pressed,
            });
            break;
        }
        case NSEventTypeScrollWheel: {
            const bool precise = [native_event hasPreciseScrollingDeltas] == YES;
            const CGFloat delta_x = precise ? [native_event scrollingDeltaX] : [native_event deltaX];
            const CGFloat delta_y = precise ? [native_event scrollingDeltaY] : [native_event deltaY];
            events_.emplace_back(MouseWheelEvent{
                id,
                static_cast<double>(delta_x),
                static_cast<double>(delta_y),
                precise ? ScrollUnit::pixels : ScrollUnit::lines,
            });
            break;
        }
        default:
            break;
        }
    }

    VICEApplicationDelegate* app_delegate_ = nil;
    std::deque<Event> events_;
    std::unordered_map<void*, WindowId> windows_;
    WindowId next_window_id_ = 1;
};

MacWindow::MacWindow(
    MacPlatformServices& platform,
    const WindowId id,
    NSWindow* window,
    VICEWindowDelegate* delegate
) noexcept
    : platform_{&platform}, id_{id}, window_{window}, delegate_{delegate} {}

MacWindow::~MacWindow() {
    @autoreleasepool {
        if (window_ != nil) {
            platform_->unregister_window(window_);
            [window_ setDelegate:nil];
            [window_ orderOut:nil];
            [window_ close];
            window_ = nil;
            delegate_ = nil;
        }
    }
}

WindowId MacWindow::id() const noexcept {
    return id_;
}

WindowMetrics MacWindow::metrics() const noexcept {
    return window_metrics(window_);
}

bool MacWindow::visible() const noexcept {
    return window_ != nil && [window_ isVisible] == YES;
}

void MacWindow::set_title(const std::string_view title) {
    @autoreleasepool {
        if (window_ != nil) {
            [window_ setTitle:make_ns_string(title)];
        }
    }
}

void MacWindow::show() {
    @autoreleasepool {
        if (window_ != nil) {
            [window_ makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
        }
    }
}

void MacWindow::hide() {
    @autoreleasepool {
        if (window_ != nil) {
            [window_ orderOut:nil];
        }
    }
}

NativeWindowHandle MacWindow::native_handle() const noexcept {
    return NativeWindowHandle{
        NativeWindowSystem::cocoa,
        window_ != nil ? (__bridge void*)window_ : nullptr,
        window_ != nil ? (__bridge void*)[window_ contentView] : nullptr,
    };
}

std::unique_ptr<PlatformServices> PlatformServices::create() {
    @autoreleasepool {
        if ([NSThread isMainThread] != YES) {
            core::log(core::LogLevel::error, "The macOS platform service must be created on the main thread.");
            return nullptr;
        }
        return std::make_unique<MacPlatformServices>();
    }
}

} // namespace vice::platform

@implementation VICEApplicationDelegate

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    (void)sender;
    auto* platform = static_cast<vice::platform::MacPlatformServices*>(self.service);
    if (platform != nullptr) {
        platform->push(vice::platform::Event{vice::platform::QuitRequested{}});
    }
    return NSTerminateCancel;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return NO;
}

@end

@implementation VICEWindowDelegate

- (BOOL)windowShouldClose:(id)sender {
    (void)sender;
    auto* platform = static_cast<vice::platform::MacPlatformServices*>(self.service);
    if (platform != nullptr) {
        platform->push(vice::platform::Event{
            vice::platform::WindowCloseRequested{self.windowId}
        });
    }
    return NO;
}

- (void)windowDidResize:(NSNotification*)notification {
    auto* platform = static_cast<vice::platform::MacPlatformServices*>(self.service);
    NSWindow* window = [notification object];
    if (platform != nullptr && window != nil) {
        platform->push(vice::platform::Event{
            vice::platform::WindowMetricsChanged{
                self.windowId,
                vice::platform::window_metrics(window),
            }
        });
    }
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
    [self windowDidResize:notification];
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
    (void)notification;
    auto* platform = static_cast<vice::platform::MacPlatformServices*>(self.service);
    if (platform != nullptr) {
        platform->push(vice::platform::Event{
            vice::platform::WindowFocusChanged{self.windowId, true}
        });
    }
}

- (void)windowDidResignKey:(NSNotification*)notification {
    (void)notification;
    auto* platform = static_cast<vice::platform::MacPlatformServices*>(self.service);
    if (platform != nullptr) {
        platform->push(vice::platform::Event{
            vice::platform::WindowFocusChanged{self.windowId, false}
        });
    }
}

@end
