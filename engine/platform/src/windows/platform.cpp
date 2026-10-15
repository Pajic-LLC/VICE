#include <vice/core/log.hpp>
#include <vice/platform/native_window.hpp>
#include <vice/platform/services.hpp>

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace vice::platform {
namespace {

constexpr wchar_t window_class_name[] = L"VICE.PlatformWindow";
constexpr double windows_dpi_base = 96.0;

[[nodiscard]] std::wstring utf8_to_wide(const std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int input_size = static_cast<int>(text.size());
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, nullptr, 0);
    if (required <= 0) {
        return L"VICE";
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        input_size,
        result.data(),
        required
    );
    if (converted != required) {
        return L"VICE";
    }
    return result;
}

[[nodiscard]] Key key_for(const WPARAM key) noexcept {
    if (key >= static_cast<WPARAM>('A') && key <= static_cast<WPARAM>('Z')) {
        const auto offset = static_cast<std::uint16_t>(key - static_cast<WPARAM>('A'));
        return static_cast<Key>(static_cast<std::uint16_t>(Key::a) + offset);
    }
    if (key >= static_cast<WPARAM>('0') && key <= static_cast<WPARAM>('9')) {
        const auto offset = static_cast<std::uint16_t>(key - static_cast<WPARAM>('0'));
        return static_cast<Key>(static_cast<std::uint16_t>(Key::digit_0) + offset);
    }
    if (key >= VK_F1 && key <= VK_F12) {
        const auto offset = static_cast<std::uint16_t>(key - VK_F1);
        return static_cast<Key>(static_cast<std::uint16_t>(Key::f1) + offset);
    }

    switch (key) {
    case VK_ESCAPE: return Key::escape;
    case VK_RETURN: return Key::enter;
    case VK_TAB: return Key::tab;
    case VK_BACK: return Key::backspace;
    case VK_SPACE: return Key::space;
    case VK_LEFT: return Key::left;
    case VK_RIGHT: return Key::right;
    case VK_UP: return Key::up;
    case VK_DOWN: return Key::down;
    case VK_HOME: return Key::home;
    case VK_END: return Key::end;
    case VK_PRIOR: return Key::page_up;
    case VK_NEXT: return Key::page_down;
    case VK_INSERT: return Key::insert;
    case VK_DELETE: return Key::delete_key;
    default: return Key::unknown;
    }
}

[[nodiscard]] ModifierMask current_modifiers() noexcept {
    ModifierMask modifiers = 0;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        modifiers |= modifier_mask(Modifier::shift);
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        modifiers |= modifier_mask(Modifier::control);
    }
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
        modifiers |= modifier_mask(Modifier::alt);
    }
    if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) {
        modifiers |= modifier_mask(Modifier::super);
    }
    if ((GetKeyState(VK_CAPITAL) & 0x0001) != 0) {
        modifiers |= modifier_mask(Modifier::caps_lock);
    }
    return modifiers;
}

[[nodiscard]] std::uint32_t drawable_dimension(const LONG value) noexcept {
    return value > 0 ? static_cast<std::uint32_t>(value) : 0U;
}

[[nodiscard]] WindowMetrics metrics_for(HWND window) noexcept {
    if (window == nullptr) {
        return {};
    }

    RECT client{};
    if (GetClientRect(window, &client) == FALSE) {
        return {};
    }

    const LONG pixel_width = client.right - client.left;
    const LONG pixel_height = client.bottom - client.top;
    const UINT dpi = GetDpiForWindow(window);
    const double scale = dpi == 0U ? 1.0 : static_cast<double>(dpi) / windows_dpi_base;
    const auto logical_width = static_cast<std::uint32_t>(
        std::max(0.0, std::round(static_cast<double>(pixel_width) / scale))
    );
    const auto logical_height = static_cast<std::uint32_t>(
        std::max(0.0, std::round(static_cast<double>(pixel_height) / scale))
    );

    return WindowMetrics{
        logical_width,
        logical_height,
        drawable_dimension(pixel_width),
        drawable_dimension(pixel_height),
        static_cast<float>(scale),
    };
}

} // namespace

class WinPlatformServices;

class WinWindow final : public Window {
public:
    WinWindow(WinPlatformServices& platform, WindowId id, HINSTANCE instance, const WindowDesc& desc);
    ~WinWindow() override;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] WindowId id() const noexcept override;
    [[nodiscard]] WindowMetrics metrics() const noexcept override;
    [[nodiscard]] bool visible() const noexcept override;

    void set_title(std::string_view title) override;
    void show() override;
    void hide() override;

    [[nodiscard]] static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

private:
    [[nodiscard]] NativeWindowHandle native_handle() const noexcept override;
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    void push_pointer_move(LPARAM lparam);
    void push_pointer_button(MouseButton button, bool pressed, LPARAM lparam);
    void close_native() noexcept;

    WinPlatformServices* platform_ = nullptr;
    WindowId id_ = invalid_window_id;
    HWND window_ = nullptr;
    bool has_last_pointer_ = false;
    double last_pointer_x_ = 0.0;
    double last_pointer_y_ = 0.0;
    wchar_t pending_high_surrogate_ = 0;
};

class WinPlatformServices final : public PlatformServices {
public:
    WinPlatformServices() : instance_{GetModuleHandleW(nullptr)} {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        window_class.lpfnWndProc = &WinWindow::window_proc;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = window_class_name;

        class_atom_ = RegisterClassExW(&window_class);
        if (class_atom_ == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            core::log(core::LogLevel::error, "Win32 could not register the VICE window class.");
        }
    }

    ~WinPlatformServices() override {
        if (class_atom_ != 0) {
            UnregisterClassW(window_class_name, instance_);
        }
    }

    [[nodiscard]] std::unique_ptr<Window> create_window(const WindowDesc& desc) override {
        const WindowId id = next_window_id_++;
        auto window = std::make_unique<WinWindow>(*this, id, instance_, desc);
        if (!window->valid()) {
            return nullptr;
        }
        return window;
    }

    bool poll_event(Event& event) override {
        if (events_.empty()) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
                if (message.message == WM_QUIT) {
                    events_.emplace_back(QuitRequested{});
                    continue;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
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
        PlatformPaths result;

        std::wstring executable_buffer(32768U, L'\0');
        const DWORD executable_size = GetModuleFileNameW(
            nullptr,
            executable_buffer.data(),
            static_cast<DWORD>(executable_buffer.size())
        );
        if (executable_size > 0U && static_cast<std::size_t>(executable_size) < executable_buffer.size()) {
            executable_buffer.resize(static_cast<std::size_t>(executable_size));
            result.executable = std::filesystem::path{executable_buffer};
        }

        PWSTR local_app_data = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_app_data))) {
            result.user_data = std::filesystem::path{local_app_data} / L"Pajic Studios" / L"VICE";
            CoTaskMemFree(local_app_data);
        }

        std::wstring temporary_buffer(MAX_PATH + 1U, L'\0');
        const DWORD temporary_size = GetTempPathW(
            static_cast<DWORD>(temporary_buffer.size()),
            temporary_buffer.data()
        );
        if (temporary_size > 0U && static_cast<std::size_t>(temporary_size) < temporary_buffer.size()) {
            temporary_buffer.resize(static_cast<std::size_t>(temporary_size));
            result.temporary = std::filesystem::path{temporary_buffer};
        }

        return result;
    }

    void push(Event event) {
        events_.push_back(std::move(event));
    }

private:
    HINSTANCE instance_ = nullptr;
    ATOM class_atom_ = 0;
    std::deque<Event> events_;
    WindowId next_window_id_ = 1;
};

WinWindow::WinWindow(
    WinPlatformServices& platform,
    const WindowId id,
    HINSTANCE instance,
    const WindowDesc& desc
) : platform_{&platform}, id_{id} {
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
        (desc.resizable ? (WS_THICKFRAME | WS_MAXIMIZEBOX) : 0U);

    const UINT dpi = GetDpiForSystem();
    RECT outer_rect{
        0,
        0,
        MulDiv(static_cast<int>(desc.width), static_cast<int>(dpi), 96),
        MulDiv(static_cast<int>(desc.height), static_cast<int>(dpi), 96),
    };
    AdjustWindowRectExForDpi(&outer_rect, style, FALSE, 0, dpi);

    const std::wstring title = utf8_to_wide(desc.title);
    window_ = CreateWindowExW(
        0,
        window_class_name,
        title.c_str(),
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        outer_rect.right - outer_rect.left,
        outer_rect.bottom - outer_rect.top,
        nullptr,
        nullptr,
        instance,
        this
    );

    if (window_ == nullptr) {
        core::log(core::LogLevel::error, "Win32 could not create a VICE window.");
        return;
    }

    if (desc.visible) {
        show();
    }
}

WinWindow::~WinWindow() {
    close_native();
}

bool WinWindow::valid() const noexcept {
    return window_ != nullptr;
}

WindowId WinWindow::id() const noexcept {
    return id_;
}

WindowMetrics WinWindow::metrics() const noexcept {
    return metrics_for(window_);
}

bool WinWindow::visible() const noexcept {
    return window_ != nullptr && IsWindowVisible(window_) != FALSE;
}

void WinWindow::set_title(const std::string_view title) {
    if (window_ != nullptr) {
        const std::wstring wide_title = utf8_to_wide(title);
        SetWindowTextW(window_, wide_title.c_str());
    }
}

void WinWindow::show() {
    if (window_ != nullptr) {
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
    }
}

void WinWindow::hide() {
    if (window_ != nullptr) {
        ShowWindow(window_, SW_HIDE);
    }
}

NativeWindowHandle WinWindow::native_handle() const noexcept {
    return NativeWindowHandle{
        NativeWindowSystem::win32,
        window_,
        nullptr,
    };
}

LRESULT CALLBACK WinWindow::window_proc(
    HWND window,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam
) {
    WinWindow* self = reinterpret_cast<WinWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<WinWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }

    if (self != nullptr) {
        return self->handle_message(message, wparam, lparam);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT WinWindow::handle_message(const UINT message, const WPARAM wparam, const LPARAM lparam) {
    switch (message) {
    case WM_CLOSE:
        platform_->push(Event{WindowCloseRequested{id_}});
        return 0;
    case WM_SIZE:
        platform_->push(Event{WindowMetricsChanged{id_, metrics()}});
        return 0;
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        if (suggested != nullptr) {
            SetWindowPos(
                window_,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER
            );
        }
        platform_->push(Event{WindowMetricsChanged{id_, metrics()}});
        return 0;
    }
    case WM_SETFOCUS:
        platform_->push(Event{WindowFocusChanged{id_, true}});
        return 0;
    case WM_KILLFOCUS:
        platform_->push(Event{WindowFocusChanged{id_, false}});
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        platform_->push(Event{KeyEvent{
            id_,
            key_for(wparam),
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(lparam) >> 16U) & 0xffU),
            current_modifiers(),
            true,
            (static_cast<std::uint64_t>(lparam) & (1ULL << 30U)) != 0ULL,
        }});
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        platform_->push(Event{KeyEvent{
            id_,
            key_for(wparam),
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(lparam) >> 16U) & 0xffU),
            current_modifiers(),
            false,
            false,
        }});
        break;
    case WM_CHAR: {
        const wchar_t unit = static_cast<wchar_t>(wparam);
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            pending_high_surrogate_ = unit;
            return 0;
        }

        char32_t codepoint = static_cast<char32_t>(unit);
        if (pending_high_surrogate_ != 0 && unit >= 0xDC00 && unit <= 0xDFFF) {
            const char32_t high = static_cast<char32_t>(pending_high_surrogate_ - 0xD800);
            const char32_t low = static_cast<char32_t>(unit - 0xDC00);
            codepoint = static_cast<char32_t>(0x10000) + (high << 10U) + low;
        }
        pending_high_surrogate_ = 0;

        if (codepoint >= U' ' && codepoint != U'\x7f') {
            platform_->push(Event{TextInputEvent{id_, codepoint}});
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        push_pointer_move(lparam);
        return 0;
    case WM_LBUTTONDOWN:
        push_pointer_button(MouseButton::left, true, lparam);
        return 0;
    case WM_LBUTTONUP:
        push_pointer_button(MouseButton::left, false, lparam);
        return 0;
    case WM_RBUTTONDOWN:
        push_pointer_button(MouseButton::right, true, lparam);
        return 0;
    case WM_RBUTTONUP:
        push_pointer_button(MouseButton::right, false, lparam);
        return 0;
    case WM_MBUTTONDOWN:
        push_pointer_button(MouseButton::middle, true, lparam);
        return 0;
    case WM_MBUTTONUP:
        push_pointer_button(MouseButton::middle, false, lparam);
        return 0;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP: {
        const WORD button = GET_XBUTTON_WPARAM(wparam);
        const MouseButton mapped = button == XBUTTON1 ? MouseButton::button_4 : MouseButton::button_5;
        push_pointer_button(mapped, message == WM_XBUTTONDOWN, lparam);
        return TRUE;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        const double wheel_delta = static_cast<double>(GET_WHEEL_DELTA_WPARAM(wparam)) /
            static_cast<double>(WHEEL_DELTA);
        platform_->push(Event{MouseWheelEvent{
            id_,
            message == WM_MOUSEHWHEEL ? wheel_delta : 0.0,
            message == WM_MOUSEWHEEL ? wheel_delta : 0.0,
            ScrollUnit::lines,
        }});
        return 0;
    }
    case WM_QUERYENDSESSION:
        platform_->push(Event{QuitRequested{}});
        return TRUE;
    case WM_NCDESTROY: {
        HWND destroyed_window = window_;
        SetWindowLongPtrW(destroyed_window, GWLP_USERDATA, 0);
        window_ = nullptr;
        return DefWindowProcW(destroyed_window, message, wparam, lparam);
    }
    default:
        break;
    }

    return DefWindowProcW(window_, message, wparam, lparam);
}

void WinWindow::push_pointer_move(const LPARAM lparam) {
    const WindowMetrics current_metrics = metrics();
    const double scale = current_metrics.scale_factor > 0.0F
        ? static_cast<double>(current_metrics.scale_factor)
        : 1.0;
    const double x = static_cast<double>(GET_X_LPARAM(lparam)) / scale;
    const double y = static_cast<double>(GET_Y_LPARAM(lparam)) / scale;
    const double delta_x = has_last_pointer_ ? x - last_pointer_x_ : 0.0;
    const double delta_y = has_last_pointer_ ? y - last_pointer_y_ : 0.0;

    last_pointer_x_ = x;
    last_pointer_y_ = y;
    has_last_pointer_ = true;

    platform_->push(Event{MouseMoveEvent{id_, x, y, delta_x, delta_y}});
}

void WinWindow::push_pointer_button(
    const MouseButton button,
    const bool pressed,
    const LPARAM lparam
) {
    const WindowMetrics current_metrics = metrics();
    const double scale = current_metrics.scale_factor > 0.0F
        ? static_cast<double>(current_metrics.scale_factor)
        : 1.0;
    const double x = static_cast<double>(GET_X_LPARAM(lparam)) / scale;
    const double y = static_cast<double>(GET_Y_LPARAM(lparam)) / scale;
    platform_->push(Event{MouseButtonEvent{id_, button, x, y, pressed}});
}

void WinWindow::close_native() noexcept {
    if (window_ != nullptr) {
        HWND window = window_;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        window_ = nullptr;
        DestroyWindow(window);
    }
}

std::unique_ptr<PlatformServices> PlatformServices::create() {
    return std::make_unique<WinPlatformServices>();
}

} // namespace vice::platform
