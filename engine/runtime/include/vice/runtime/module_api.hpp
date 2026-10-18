#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

#if defined(_WIN32)
#define VICE_GAME_MODULE_EXPORT __declspec(dllexport)
#else
#define VICE_GAME_MODULE_EXPORT __attribute__((visibility("default")))
#endif

namespace vice::runtime::module {

inline constexpr std::uint32_t abi_version = 1U;
inline constexpr const char* entrypoint_name = "vice_game_module_load";

enum class LogLevel : std::uint32_t {
    debug,
    info,
    warning,
    error,
};

enum class EventKind : std::uint32_t {
    unknown,
    quit_requested,
    window_close_requested,
    window_metrics_changed,
    window_focus_changed,
    key,
    text_input,
    mouse_move,
    mouse_button,
    mouse_wheel,
};

struct Event {
    std::uint32_t size = sizeof(Event);
    EventKind kind = EventKind::unknown;
    std::uint32_t window = 0;
    std::uint32_t code = 0;
    std::uint32_t modifiers = 0;
    std::uint32_t flags = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t drawable_width = 0;
    std::uint32_t drawable_height = 0;
    float scale_factor = 1.0F;
    double x = 0.0;
    double y = 0.0;
    double delta_x = 0.0;
    double delta_y = 0.0;
};

struct Color {
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    float alpha = 1.0F;
};

struct Frame {
    std::uint32_t size = sizeof(Frame);
    Color clear_color{};
};

struct HostApi {
    std::uint32_t abi_version = module::abi_version;
    std::uint32_t size = sizeof(HostApi);
    void* context = nullptr;
    std::size_t (*world_entity_count)(void* context) noexcept = nullptr;
    std::size_t (*world_resource_count)(void* context) noexcept = nullptr;
    void (*log)(void* context, LogLevel level, const char* message) noexcept = nullptr;
};

struct ModuleApi {
    std::uint32_t abi_version = module::abi_version;
    std::uint32_t size = sizeof(ModuleApi);
    void* state = nullptr;
    bool (*start)(void* state, const HostApi* host) noexcept = nullptr;
    void (*event)(void* state, const HostApi* host, const Event* event) noexcept = nullptr;
    void (*update)(void* state, const HostApi* host, double delta_seconds) noexcept = nullptr;
    void (*frame)(void* state, const HostApi* host, Frame* frame) noexcept = nullptr;
    void (*stop)(void* state, const HostApi* host) noexcept = nullptr;
    void (*destroy)(void* state) noexcept = nullptr;
};

using LoadFunction = bool (*)(
    std::uint32_t host_abi_version,
    const HostApi* host,
    ModuleApi* module
) noexcept;

class Context final {
public:
    explicit Context(const HostApi* host) noexcept : host_{host} {}

    [[nodiscard]] std::size_t entity_count() const noexcept {
        return host_ != nullptr && host_->world_entity_count != nullptr
            ? host_->world_entity_count(host_->context)
            : 0U;
    }

    [[nodiscard]] std::size_t resource_count() const noexcept {
        return host_ != nullptr && host_->world_resource_count != nullptr
            ? host_->world_resource_count(host_->context)
            : 0U;
    }

    void log(const LogLevel level, const char* const message) const noexcept {
        if (host_ != nullptr && host_->log != nullptr) {
            host_->log(host_->context, level, message != nullptr ? message : "");
        }
    }

private:
    const HostApi* host_ = nullptr;
};

class Game {
public:
    virtual ~Game() = default;

    [[nodiscard]] virtual bool on_start(Context&) { return true; }
    virtual void on_event(Context&, const Event&) {}
    virtual void on_update(Context&, double) {}
    virtual void on_frame(Context&, Frame&) {}
    virtual void on_stop(Context&) {}
};

namespace detail {

template <typename GameType>
[[nodiscard]] bool start(void* const state, const HostApi* const host) noexcept {
    if (state == nullptr || host == nullptr) return false;
    Context context{host};
    try {
        return static_cast<GameType*>(state)->on_start(context);
    } catch (...) {
        context.log(LogLevel::error, "Unhandled exception in game module on_start.");
        return false;
    }
}

template <typename GameType>
void event(
    void* const state,
    const HostApi* const host,
    const Event* const value
) noexcept {
    if (state == nullptr || host == nullptr || value == nullptr) return;
    Context context{host};
    try {
        static_cast<GameType*>(state)->on_event(context, *value);
    } catch (...) {
        context.log(LogLevel::error, "Unhandled exception in game module on_event.");
    }
}

template <typename GameType>
void update(
    void* const state,
    const HostApi* const host,
    const double delta_seconds
) noexcept {
    if (state == nullptr || host == nullptr) return;
    Context context{host};
    try {
        static_cast<GameType*>(state)->on_update(context, delta_seconds);
    } catch (...) {
        context.log(LogLevel::error, "Unhandled exception in game module on_update.");
    }
}

template <typename GameType>
void frame(
    void* const state,
    const HostApi* const host,
    Frame* const value
) noexcept {
    if (state == nullptr || host == nullptr || value == nullptr) return;
    Context context{host};
    try {
        static_cast<GameType*>(state)->on_frame(context, *value);
    } catch (...) {
        context.log(LogLevel::error, "Unhandled exception in game module on_frame.");
    }
}

template <typename GameType>
void stop(void* const state, const HostApi* const host) noexcept {
    if (state == nullptr || host == nullptr) return;
    Context context{host};
    try {
        static_cast<GameType*>(state)->on_stop(context);
    } catch (...) {
        context.log(LogLevel::error, "Unhandled exception in game module on_stop.");
    }
}

template <typename GameType>
void destroy(void* const state) noexcept {
    delete static_cast<GameType*>(state);
}

template <typename GameType>
[[nodiscard]] bool load(
    const std::uint32_t host_abi_version,
    const HostApi* const host,
    ModuleApi* const output
) noexcept {
    if (host_abi_version != abi_version || host == nullptr || output == nullptr ||
        host->abi_version != abi_version || host->size < sizeof(HostApi)) {
        return false;
    }

    GameType* const game = new (std::nothrow) GameType{};
    if (game == nullptr) return false;

    *output = ModuleApi{
        .abi_version = abi_version,
        .size = sizeof(ModuleApi),
        .state = game,
        .start = &detail::start<GameType>,
        .event = &detail::event<GameType>,
        .update = &detail::update<GameType>,
        .frame = &detail::frame<GameType>,
        .stop = &detail::stop<GameType>,
        .destroy = &detail::destroy<GameType>,
    };
    return true;
}

} // namespace detail

} // namespace vice::runtime::module

#define VICE_GAME_MODULE(GameType) \
    extern "C" VICE_GAME_MODULE_EXPORT bool vice_game_module_load( \
        const std::uint32_t host_abi_version, \
        const ::vice::runtime::module::HostApi* const host, \
        ::vice::runtime::module::ModuleApi* const module \
    ) noexcept { \
        return ::vice::runtime::module::detail::load<GameType>(host_abi_version, host, module); \
    }
