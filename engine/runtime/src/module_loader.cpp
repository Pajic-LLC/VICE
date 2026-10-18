#include <vice/runtime/module_loader.hpp>

#include <vice/core/log.hpp>
#include <vice/platform/dynamic_library.hpp>
#include <vice/runtime/module_api.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace vice::runtime {
namespace {

struct HostBridge {
    GameContext* context = nullptr;
};

[[nodiscard]] std::size_t host_entity_count(void* const value) noexcept {
    const auto* const bridge = static_cast<HostBridge*>(value);
    return bridge != nullptr && bridge->context != nullptr
        ? bridge->context->world.entity_count()
        : 0U;
}

[[nodiscard]] std::size_t host_resource_count(void* const value) noexcept {
    const auto* const bridge = static_cast<HostBridge*>(value);
    return bridge != nullptr && bridge->context != nullptr
        ? bridge->context->world.resource_count()
        : 0U;
}

void host_log(
    void*,
    const module::LogLevel level,
    const char* const message
) noexcept {
    core::LogLevel engine_level = core::LogLevel::info;
    switch (level) {
        case module::LogLevel::debug: engine_level = core::LogLevel::debug; break;
        case module::LogLevel::info: engine_level = core::LogLevel::info; break;
        case module::LogLevel::warning: engine_level = core::LogLevel::warning; break;
        case module::LogLevel::error: engine_level = core::LogLevel::error; break;
    }
    core::log(engine_level, message != nullptr ? message : "");
}

[[nodiscard]] module::Event translate_event(const platform::Event& event) noexcept {
    module::Event translated;
    std::visit(
        [&](const auto& payload) {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, platform::QuitRequested>) {
                translated.kind = module::EventKind::quit_requested;
            } else if constexpr (std::is_same_v<Payload, platform::WindowCloseRequested>) {
                translated.kind = module::EventKind::window_close_requested;
                translated.window = payload.window;
            } else if constexpr (std::is_same_v<Payload, platform::WindowMetricsChanged>) {
                translated.kind = module::EventKind::window_metrics_changed;
                translated.window = payload.window;
                translated.width = payload.metrics.width;
                translated.height = payload.metrics.height;
                translated.drawable_width = payload.metrics.drawable_width;
                translated.drawable_height = payload.metrics.drawable_height;
                translated.scale_factor = payload.metrics.scale_factor;
            } else if constexpr (std::is_same_v<Payload, platform::WindowFocusChanged>) {
                translated.kind = module::EventKind::window_focus_changed;
                translated.window = payload.window;
                translated.flags = payload.focused ? 1U : 0U;
            } else if constexpr (std::is_same_v<Payload, platform::KeyEvent>) {
                translated.kind = module::EventKind::key;
                translated.window = payload.window;
                translated.code = static_cast<std::uint32_t>(payload.key);
                translated.modifiers = payload.modifiers;
                translated.flags = (payload.pressed ? 1U : 0U) | (payload.repeat ? 2U : 0U);
            } else if constexpr (std::is_same_v<Payload, platform::TextInputEvent>) {
                translated.kind = module::EventKind::text_input;
                translated.window = payload.window;
                translated.code = static_cast<std::uint32_t>(payload.codepoint);
            } else if constexpr (std::is_same_v<Payload, platform::MouseMoveEvent>) {
                translated.kind = module::EventKind::mouse_move;
                translated.window = payload.window;
                translated.x = payload.x;
                translated.y = payload.y;
                translated.delta_x = payload.delta_x;
                translated.delta_y = payload.delta_y;
            } else if constexpr (std::is_same_v<Payload, platform::MouseButtonEvent>) {
                translated.kind = module::EventKind::mouse_button;
                translated.window = payload.window;
                translated.code = static_cast<std::uint32_t>(payload.button);
                translated.flags = payload.pressed ? 1U : 0U;
                translated.x = payload.x;
                translated.y = payload.y;
            } else if constexpr (std::is_same_v<Payload, platform::MouseWheelEvent>) {
                translated.kind = module::EventKind::mouse_wheel;
                translated.window = payload.window;
                translated.code = static_cast<std::uint32_t>(payload.unit);
                translated.delta_x = payload.delta_x;
                translated.delta_y = payload.delta_y;
            }
        },
        event.payload
    );
    return translated;
}

class DynamicGameModule final : public GameModule {
public:
    explicit DynamicGameModule(std::unique_ptr<platform::DynamicLibrary> library) noexcept
        : library_{std::move(library)} {
        host_.context = &bridge_;
        host_.world_entity_count = &host_entity_count;
        host_.world_resource_count = &host_resource_count;
        host_.log = &host_log;
    }

    ~DynamicGameModule() override {
        if (api_.state != nullptr && api_.destroy != nullptr) {
            api_.destroy(api_.state);
            api_.state = nullptr;
        }
    }

    [[nodiscard]] bool initialize(
        const module::LoadFunction load,
        std::string& diagnostics
    ) noexcept {
        if (load == nullptr) {
            diagnostics = "Game module load entrypoint is null.";
            return false;
        }

        module::ModuleApi loaded_api{};
        if (!load(module::abi_version, &host_, &loaded_api)) {
            diagnostics = "Game module rejected VICE module ABI version " +
                std::to_string(module::abi_version) + ".";
            return false;
        }

        const bool valid = loaded_api.abi_version == module::abi_version &&
            loaded_api.size >= sizeof(module::ModuleApi) &&
            loaded_api.state != nullptr &&
            loaded_api.start != nullptr &&
            loaded_api.event != nullptr &&
            loaded_api.update != nullptr &&
            loaded_api.frame != nullptr &&
            loaded_api.stop != nullptr &&
            loaded_api.destroy != nullptr;
        if (!valid) {
            if (loaded_api.state != nullptr && loaded_api.destroy != nullptr) {
                loaded_api.destroy(loaded_api.state);
            }
            diagnostics = "Game module returned an incomplete or incompatible module API.";
            return false;
        }

        api_ = loaded_api;
        diagnostics.clear();
        return true;
    }

    bool on_start(GameContext& context) override {
        bridge_.context = &context;
        const bool result = api_.start(api_.state, &host_);
        bridge_.context = nullptr;
        return result;
    }

    void on_event(GameContext& context, const platform::Event& event) override {
        bridge_.context = &context;
        const module::Event translated = translate_event(event);
        api_.event(api_.state, &host_, &translated);
        bridge_.context = nullptr;
    }

    void on_update(GameContext& context, const double delta_seconds) override {
        bridge_.context = &context;
        api_.update(api_.state, &host_, delta_seconds);
        bridge_.context = nullptr;
    }

    void on_frame(GameContext& context, render::Color& clear_color) override {
        bridge_.context = &context;
        module::Frame frame;
        frame.clear_color = module::Color{
            .red = clear_color.red,
            .green = clear_color.green,
            .blue = clear_color.blue,
            .alpha = clear_color.alpha,
        };
        api_.frame(api_.state, &host_, &frame);
        clear_color = render::Color{
            .red = frame.clear_color.red,
            .green = frame.clear_color.green,
            .blue = frame.clear_color.blue,
            .alpha = frame.clear_color.alpha,
        };
        bridge_.context = nullptr;
    }

    void on_stop(GameContext& context) override {
        bridge_.context = &context;
        api_.stop(api_.state, &host_);
        bridge_.context = nullptr;
    }

private:
    std::unique_ptr<platform::DynamicLibrary> library_;
    module::ModuleApi api_{};
    HostBridge bridge_{};
    module::HostApi host_{};
};

} // namespace

std::filesystem::path staged_game_module_path(
    const std::filesystem::path& executable,
    const std::string_view game_target
) {
    if (executable.empty() || game_target.empty()) return {};
    return executable.parent_path() /
        "modules" /
        (std::string{game_target} + std::string{platform::dynamic_library_extension()});
}

GameModuleLoadResult load_game_module(const std::filesystem::path& path) {
    GameModuleLoadResult result;
    if (path.empty()) {
        result.diagnostics = "Game module path is empty.";
        return result;
    }

    std::string diagnostics;
    auto library = platform::DynamicLibrary::load(path, &diagnostics);
    if (!library) {
        result.diagnostics = "Could not load game module `" + path.generic_string() + "`: " + diagnostics;
        return result;
    }

    const std::uintptr_t address = library->symbol(module::entrypoint_name, &diagnostics);
    if (address == 0U) {
        result.diagnostics = "Game module `" + path.generic_string() + "` is missing `" +
            std::string{module::entrypoint_name} + "`: " + diagnostics;
        return result;
    }

    const auto load = reinterpret_cast<module::LoadFunction>(address);
    auto module_instance = std::make_unique<DynamicGameModule>(std::move(library));
    if (!module_instance->initialize(load, diagnostics)) {
        result.diagnostics = "Could not initialize game module `" + path.generic_string() + "`: " + diagnostics;
        return result;
    }

    result.module = std::move(module_instance);
    return result;
}

} // namespace vice::runtime
