#include <vice/runtime/module_loader.hpp>
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
#include <vice/rigid_body/authored_simulation.hpp>
#endif

#include <vice/core/log.hpp>
#include <vice/platform/dynamic_library.hpp>
#include <vice/runtime/module_api.hpp>

#include <cstdint>
#include <cmath>
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
    bool writable = false;
};

// A retained HostApi cannot access a callback's World from another thread or phase.
thread_local HostBridge* active_bridge = nullptr;
struct CallbackScope {
    HostBridge& bridge;
    HostBridge* previous;
    CallbackScope(HostBridge& b, GameContext& context, bool writable) : bridge{b}, previous{active_bridge} {
        bridge.context = &context;
        bridge.writable = writable;
        active_bridge = &bridge;
    }
    ~CallbackScope() { active_bridge = previous; bridge.context = nullptr; bridge.writable = false; }
};
HostBridge* current_bridge(void* value) noexcept {
    return active_bridge == value ? active_bridge : nullptr;
}
world::PersistentId persistent_id(module::EntityId id) noexcept {
    world::PersistentId::Bytes bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = id.bytes[i];
    return world::PersistentId{bytes};
}
module::Result host_read_entity(void* value, module::EntityId id, module::EntitySnapshot* output) noexcept {
    const auto* bridge = current_bridge(value);
    if (!bridge || !bridge->context) return module::Result::wrong_phase;
    if (!output || output->size < sizeof(module::EntitySnapshot) || persistent_id(id).is_nil()) return module::Result::invalid_argument;
    const auto& world = bridge->context->world;
    const auto entity = world.find(persistent_id(id));
    if (!entity) return module::Result::not_found;
    module::EntitySnapshot result;
    result.id = id;
    if (const auto spatial = world.spatial_state(*entity)) {
        result.flags |= 1U;
        result.region = spatial->region.value;
        result.position[0] = spatial->global_position.x;
        result.position[1] = spatial->global_position.y;
        result.position[2] = spatial->global_position.z;
        if (const auto region = world.region(spatial->region)) result.fidelity = static_cast<std::uint32_t>(region->fidelity);
    }
    if (const auto transform = world.authored_transform(*entity)) {
        result.rotation[0] = transform->rotation.x; result.rotation[1] = transform->rotation.y;
        result.rotation[2] = transform->rotation.z; result.rotation[3] = transform->rotation.w;
        result.scale[0] = transform->scale.x; result.scale[1] = transform->scale.y; result.scale[2] = transform->scale.z;
    }
    if (const auto box = world.rigid_box(*entity)) {
        result.flags |= 2U;
        switch (box->motion) {
        case world::RigidMotion::static_body: result.motion = module::Motion::static_body; break;
        case world::RigidMotion::kinematic_body: result.motion = module::Motion::kinematic_body; break;
        case world::RigidMotion::dynamic_body: result.motion = module::Motion::dynamic_body; break;
        }
        result.half_extents = {box->half_extents.x, box->half_extents.y, box->half_extents.z};
        result.linear_velocity = {box->linear_velocity.x, box->linear_velocity.y, box->linear_velocity.z};
        result.angular_velocity = {box->angular_velocity.x, box->angular_velocity.y, box->angular_velocity.z};
    }
    *output = result;
    return module::Result::success;
}
module::Result host_set_body_velocity(void* value, module::EntityId id, module::Vector3 linear, module::Vector3 angular) noexcept {
    const auto* bridge = current_bridge(value);
    if (!bridge || !bridge->context || !bridge->writable) return module::Result::wrong_phase;
    if (persistent_id(id).is_nil()) return module::Result::invalid_argument;
    const auto entity = bridge->context->world.find(persistent_id(id));
    if (!entity) return module::Result::not_found;
    if (!bridge->context->world.rigid_box(*entity)) return module::Result::no_body;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if (!bridge->context->physics) return module::Result::unavailable;
    using C = rigid_body::BodyControlResult;
    switch (bridge->context->physics->set_body_velocity(persistent_id(id), {linear.x, linear.y, linear.z}, {angular.x, angular.y, angular.z})) {
    case C::applied: return module::Result::success;
    case C::not_found: return module::Result::reset_required;
    case C::invalid_argument: return module::Result::invalid_argument;
    case C::static_body: return module::Result::static_body;
    case C::inactive_region: return module::Result::inactive_region;
    case C::reset_required: return module::Result::reset_required;
    }
#else
    (void)linear; (void)angular;
#endif
    return module::Result::unavailable;
}

module::Result host_read_body_support(void* value, module::EntityId id, module::Vector3 up, float min_up_dot, module::SupportSnapshot* output) noexcept {
    const auto* bridge = current_bridge(value);
    if (!bridge || !bridge->context) return module::Result::wrong_phase;
    const double length = std::hypot(static_cast<double>(up.x), static_cast<double>(up.y), static_cast<double>(up.z));
    if (!output || output->size < sizeof(module::SupportSnapshot) || persistent_id(id).is_nil() ||
        !std::isfinite(length) || length <= 0 || !std::isfinite(min_up_dot) || min_up_dot < -1 || min_up_dot > 1)
        return module::Result::invalid_argument;
    const auto entity = bridge->context->world.find(persistent_id(id));
    if (!entity) return module::Result::not_found;
    if (!bridge->context->world.rigid_box(*entity)) return module::Result::no_body;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if (!bridge->context->physics) return module::Result::unavailable;
    const auto support = bridge->context->physics->body_support(persistent_id(id), {up.x, up.y, up.z}, min_up_dot);
    if (!support) return module::Result::reset_required;
    module::SupportSnapshot result;
    result.supported = support->supported ? 1U : 0U;
    for (std::size_t i = 0; i < 16; ++i) result.supporting_entity.bytes[i] = support->supporting_entity.bytes()[i];
    result.point[0] = support->point.x; result.point[1] = support->point.y; result.point[2] = support->point.z;
    result.normal = {support->normal.x, support->normal.y, support->normal.z};
    result.velocity = {support->velocity.x, support->velocity.y, support->velocity.z};
    *output = result;
    return module::Result::success;
#else
    return module::Result::unavailable;
#endif
}

module::Result host_read_distance_link(void* value, module::EntityId id, module::DistanceLinkSnapshot* output) noexcept {
    const auto* bridge = current_bridge(value);
    if (!bridge || !bridge->context) return module::Result::wrong_phase;
    if (!output || output->size < sizeof(module::DistanceLinkSnapshot) || persistent_id(id).is_nil()) return module::Result::invalid_argument;
    const auto entity = bridge->context->world.find(persistent_id(id));
    if (!entity) return module::Result::not_found;
    const auto link = bridge->context->world.distance_link(*entity);
    if (!link) return module::Result::no_link;
    module::DistanceLinkSnapshot result;
    for (std::size_t i = 0; i < 16; ++i) { result.first.bytes[i] = link->first.bytes()[i]; result.second.bytes[i] = link->second.bytes()[i]; }
    result.min_distance = link->min_distance; result.max_distance = link->max_distance;
    result.frequency_hz = link->frequency_hz; result.damping_ratio = link->damping_ratio;
    result.break_threshold = link->break_threshold; result.last_break_impulse = link->last_break_impulse;
    result.broken = link->broken ? 1U : 0U; result.break_step = link->break_step;
    *output = result;
    return module::Result::success;
}

[[nodiscard]] std::size_t host_entity_count(void* const value) noexcept {
    const auto* const bridge = current_bridge(value);
    return bridge != nullptr && bridge->context != nullptr
        ? bridge->context->world.entity_count()
        : 0U;
}

[[nodiscard]] std::size_t host_resource_count(void* const value) noexcept {
    const auto* const bridge = current_bridge(value);
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

module::Key module_key(platform::Key key) noexcept {
    switch (key) {
    case platform::Key::unknown: return module::Key::unknown;
    case platform::Key::escape: return module::Key::escape;
    case platform::Key::enter: return module::Key::enter;
    case platform::Key::tab: return module::Key::tab;
    case platform::Key::backspace: return module::Key::backspace;
    case platform::Key::space: return module::Key::space;
    case platform::Key::left: return module::Key::left;
    case platform::Key::right: return module::Key::right;
    case platform::Key::up: return module::Key::up;
    case platform::Key::down: return module::Key::down;
    case platform::Key::home: return module::Key::home;
    case platform::Key::end: return module::Key::end;
    case platform::Key::page_up: return module::Key::page_up;
    case platform::Key::page_down: return module::Key::page_down;
    case platform::Key::insert: return module::Key::insert;
    case platform::Key::delete_key: return module::Key::delete_key;
    case platform::Key::a: return module::Key::a;
    case platform::Key::b: return module::Key::b;
    case platform::Key::c: return module::Key::c;
    case platform::Key::d: return module::Key::d;
    case platform::Key::e: return module::Key::e;
    case platform::Key::f: return module::Key::f;
    case platform::Key::g: return module::Key::g;
    case platform::Key::h: return module::Key::h;
    case platform::Key::i: return module::Key::i;
    case platform::Key::j: return module::Key::j;
    case platform::Key::k: return module::Key::k;
    case platform::Key::l: return module::Key::l;
    case platform::Key::m: return module::Key::m;
    case platform::Key::n: return module::Key::n;
    case platform::Key::o: return module::Key::o;
    case platform::Key::p: return module::Key::p;
    case platform::Key::q: return module::Key::q;
    case platform::Key::r: return module::Key::r;
    case platform::Key::s: return module::Key::s;
    case platform::Key::t: return module::Key::t;
    case platform::Key::u: return module::Key::u;
    case platform::Key::v: return module::Key::v;
    case platform::Key::w: return module::Key::w;
    case platform::Key::x: return module::Key::x;
    case platform::Key::y: return module::Key::y;
    case platform::Key::z: return module::Key::z;
    case platform::Key::digit_0: return module::Key::digit_0;
    case platform::Key::digit_1: return module::Key::digit_1;
    case platform::Key::digit_2: return module::Key::digit_2;
    case platform::Key::digit_3: return module::Key::digit_3;
    case platform::Key::digit_4: return module::Key::digit_4;
    case platform::Key::digit_5: return module::Key::digit_5;
    case platform::Key::digit_6: return module::Key::digit_6;
    case platform::Key::digit_7: return module::Key::digit_7;
    case platform::Key::digit_8: return module::Key::digit_8;
    case platform::Key::digit_9: return module::Key::digit_9;
    case platform::Key::f1: return module::Key::f1;
    case platform::Key::f2: return module::Key::f2;
    case platform::Key::f3: return module::Key::f3;
    case platform::Key::f4: return module::Key::f4;
    case platform::Key::f5: return module::Key::f5;
    case platform::Key::f6: return module::Key::f6;
    case platform::Key::f7: return module::Key::f7;
    case platform::Key::f8: return module::Key::f8;
    case platform::Key::f9: return module::Key::f9;
    case platform::Key::f10: return module::Key::f10;
    case platform::Key::f11: return module::Key::f11;
    case platform::Key::f12: return module::Key::f12;
    }
    return module::Key::unknown;
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
                translated.code = static_cast<std::uint32_t>(module_key(payload.key));
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
        host_.read_entity = &host_read_entity;
        host_.set_body_velocity = &host_set_body_velocity;
        host_.read_body_support = &host_read_body_support;
        host_.read_distance_link = &host_read_distance_link;
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
        CallbackScope scope{bridge_, context, true};
        const bool result = api_.start(api_.state, &host_);
        return result;
    }

    void on_event(GameContext& context, const platform::Event& event) override {
        CallbackScope scope{bridge_, context, true};
        const module::Event translated = translate_event(event);
        api_.event(api_.state, &host_, &translated);
    }

    void on_update(GameContext& context, const double delta_seconds) override {
        CallbackScope scope{bridge_, context, true};
        api_.update(api_.state, &host_, delta_seconds);
    }

    void on_frame(GameContext& context, render::Color& clear_color) override {
        CallbackScope scope{bridge_, context, false};
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
    }

    void on_stop(GameContext& context) override {
        CallbackScope scope{bridge_, context, false};
        api_.stop(api_.state, &host_);
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
