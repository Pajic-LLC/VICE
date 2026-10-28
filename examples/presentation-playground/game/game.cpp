#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vice/runtime/module_api.hpp>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(unsigned n) {
    EntityId id{};
    id.bytes[0] = 0xa2;
    id.bytes[6] = 0x40;
    id.bytes[8] = 0x80;
    id.bytes[15] = static_cast<std::uint8_t>(n);
    return id;
}
constexpr EntityId session_type() {
    auto id = entity(0);
    id.bytes[0] = 0xa4;
    id.bytes[14] = 4;
    return id;
}
bool session(Context &c, bool launch) {
    ComponentData data;
    const auto result = c.read_component(entity(2), session_type(), data);
    if (result == Result::no_component) {
        data.type = session_type();
        data.field_count = 2;
        for (unsigned i = 0; i < 2; ++i) {
            data.fields[i].id = i + 1;
            data.fields[i].kind = ComponentKind::integer;
        }
    } else if (result != Result::success)
        return false;
    auto &counter = data.fields[launch ? 0 : 1].integer;
    if (counter < std::numeric_limits<std::int64_t>::max())
        ++counter;
    return c.set_component(entity(2), data) == Result::success;
}
constexpr EntityId connection_kind() {
    auto id = session_type();
    id.bytes[14] = 5;
    return id;
}
constexpr EntityId connection_type() {
    auto id = connection_kind();
    id.bytes[15] = 1;
    return id;
}
constexpr RelationKey connection() { return {entity(3), connection_kind(), entity(2)}; }
bool relation_session(Context &c, bool launch) {
    RelationInfo info;
    const auto edge = c.read_relation(connection(), info);
    if (edge == Result::no_relation) {
        if (c.add_relation(connection()) != Result::success)
            return false;
    } else if (edge != Result::success)
        return false;
    ComponentData data;
    const auto result = c.read_relation_component(connection(), connection_type(), data);
    if (result == Result::no_component) {
        data.type = connection_type();
        data.field_count = 2;
        for (unsigned i = 0; i < 2; ++i) {
            data.fields[i].id = i + 1;
            data.fields[i].kind = ComponentKind::integer;
        }
    } else if (result != Result::success)
        return false;
    auto &counter = data.fields[launch ? 0 : 1].integer;
    if (counter < std::numeric_limits<std::int64_t>::max())
        ++counter;
    return c.set_relation_component(connection(), data) == Result::success;
}
constexpr EntityId lifecycle_type() {
    auto id = session_type();
    id.bytes[14] = 6;
    return id;
}
bool lifecycle_fact(Context &c) {
    ComponentSchema schema;
    schema.type = lifecycle_type();
    schema.field_count = 2;
    std::strcpy(schema.name, "LifecycleFact");
    for (unsigned i = 0; i < 2; ++i) {
        schema.fields[i].id = i + 1;
        schema.fields[i].kind = ComponentKind::integer;
    }
    std::strcpy(schema.fields[0].name, "launch_count");
    std::strcpy(schema.fields[1].name, "interaction_count");
    if (c.register_component_schema(schema) != Result::success)
        return false;
    WorldEventRequest request;
    request.subject = entity(3);
    request.target = entity(2);
    if (c.read_component(entity(2), session_type(), request.payload) != Result::success)
        return false;
    request.payload.type = lifecycle_type();
    CausalHistorySnapshot history;
    if (c.read_causal_history(history) != Result::success)
        return false;
    for (auto seq = history.next_sequence; seq > history.first_sequence;) {
        WorldEventSnapshot prior;
        --seq;
        if (c.read_world_event(history.history_id, seq, prior) != Result::success)
            return false;
        if (prior.has_payload &&
            std::memcmp(prior.payload.type.bytes, request.payload.type.bytes, 16) == 0) {
            request.parent_count = 1;
            request.parents[0] = prior.ref;
            break;
        }
    }
    EventRef fact;
    if (c.emit_world_event(request, fact) != Result::success)
        return false;
    request.retained = 0;
    request.parent_count = 1;
    request.parents[0] = fact;
    EventRef signal;
    return c.emit_world_event(request, signal) == Result::success;
}
constexpr EntityId pulse_type() {
    auto id = lifecycle_type();
    id.bytes[14] = 7;
    return id;
}
constexpr EntityId pulse_schedule() {
    auto id = lifecycle_type();
    id.bytes[14] = 8;
    return id;
}
bool start_pulse(Context &c) {
    ComponentSchema schema;
    schema.type = pulse_type();
    std::strcpy(schema.name, "PulseFact");
    if (c.register_component_schema(schema) != Result::success)
        return false;
    ScheduleSnapshot existing;
    const auto prior = c.read_schedule(pulse_schedule(), existing);
    if (prior == Result::success)
        return true;
    if (prior != Result::no_schedule)
        return false;
    WorldClockSnapshot clock;
    if (c.read_world_clock(clock) != Result::success ||
        clock.ticks > std::numeric_limits<std::uint64_t>::max() - 1'000'000)
        return false;
    ScheduleRequest request;
    request.id = pulse_schedule();
    request.due_ticks = clock.ticks + 1'000'000;
    request.interval_ticks = 3'000'000;
    request.catch_up = ScheduleCatchUp::coalesce;
    request.event.subject = entity(2);
    request.event.target = entity(3);
    request.event.payload.type = pulse_type();
    CausalHistorySnapshot history;
    if (c.read_causal_history(history) != Result::success)
        return false;
    for (auto seq = history.next_sequence; seq > history.first_sequence;) {
        WorldEventSnapshot fact;
        --seq;
        if (c.read_world_event(history.history_id, seq, fact) != Result::success)
            return false;
        if (fact.has_payload &&
            std::memcmp(fact.payload.type.bytes, lifecycle_type().bytes, 16) == 0) {
            request.event.parent_count = 1;
            request.event.parents[0] = fact.ref;
            break;
        }
    }
    EntityId id;
    return c.schedule_event(request, id) == Result::success;
}
constexpr EntityId units_type() {
    auto id = lifecycle_type();
    id.bytes[14] = 9;
    return id;
}
constexpr EntityId transaction_type() {
    EntityId id{};
    id.bytes[0] = 0xc7;
    id.bytes[6] = 0x40;
    id.bytes[8] = 0x80;
    id.bytes[15] = 1;
    return id;
}
bool start_quantities(Context &c) {
    QuantityTypeSnapshot type;
    type.id = units_type();
    type.quanta_per_unit = 100;
    std::strcpy(type.name, "ExampleUnits");
    if (c.register_quantity_type(type) != Result::success)
        return false;
    QuantityAccountSnapshot from, to;
    const auto a = c.read_quantity_account({entity(2), type.id}, from),
               b = c.read_quantity_account({entity(3), type.id}, to);
    const bool fresh = a == Result::no_quantity_account && b == Result::no_quantity_account;
    if (a == Result::no_quantity_account) {
        if (c.create_quantity_account({entity(2), type.id}, 1000) != Result::success)
            return false;
    } else if (a != Result::success)
        return false;
    if (b == Result::no_quantity_account) {
        if (c.create_quantity_account({entity(3), type.id}, 1000) != Result::success)
            return false;
    } else if (b != Result::success)
        return false;
    EventRef cause, prior;
    CausalHistorySnapshot history;
    if (c.read_causal_history(history) != Result::success)
        return false;
    for (auto seq = history.next_sequence; seq > history.first_sequence;) {
        WorldEventSnapshot event;
        --seq;
        if (c.read_world_event(history.history_id, seq, event) != Result::success)
            return false;
        if (!event.has_payload)
            continue;
        if (!cause.sequence &&
            std::memcmp(event.payload.type.bytes, lifecycle_type().bytes, 16) == 0)
            cause = event.ref;
        if (!prior.sequence &&
            std::memcmp(event.payload.type.bytes, transaction_type().bytes, 16) == 0)
            for (std::uint32_t i = 0; i < event.payload.field_count; ++i)
                if (event.payload.fields[i].id == 1 &&
                    std::memcmp(event.payload.fields[i].identifier.bytes, type.id.bytes, 16) == 0)
                    prior = event.ref;
        if (cause.sequence && prior.sequence)
            break;
    }
    QuantityTransactionRequest request;
    request.type = type.id;
    request.amount = 100;
    request.operation = QuantityOperation::produce;
    request.target = entity(2);
    if (cause.sequence) {
        request.parent_count = 1;
        request.parents[0] = cause;
    }
    QuantityReceiptSnapshot receipt;
    if (fresh) {
        if (c.transact_quantity(request, receipt) != Result::success)
            return false;
        prior = receipt.event;
    }
    request.operation = QuantityOperation::transfer;
    request.source = entity(2);
    request.target = entity(3);
    request.amount = 25;
    if (prior.sequence)
        request.parents[request.parent_count++] = prior;
    const auto moved = c.transact_quantity(request, receipt);
    return moved == Result::success || moved == Result::insufficient_quantity ||
           moved == Result::quantity_capacity_exceeded;
}
constexpr EntityId rule_identity(unsigned n) {
    auto id = lifecycle_type(); id.bytes[14] = 10; id.bytes[15] = static_cast<std::uint8_t>(n); return id;
}
bool start_rules(Context &c) {
    ComponentSchema schema; schema.type = rule_identity(1); schema.field_count = 1;
    std::strcpy(schema.name, "PulseObserved"); schema.fields[0].id = 1;
    schema.fields[0].kind = ComponentKind::integer; std::strcpy(schema.fields[0].name, "value");
    if (c.register_component_schema(schema) != Result::success) return false;
    ComponentData payload; payload.type = schema.type; payload.field_count = 1;
    payload.fields[0].id = 1; payload.fields[0].kind = ComponentKind::integer; payload.fields[0].integer = 1;
    for (unsigned n = 2; n <= 3; ++n) {
        RuleSnapshot existing;
        const auto prior = c.read_rule(rule_identity(n), existing);
        if (prior == Result::success) continue;
        if (prior != Result::not_found) return false;
        RuleDefinition rule; rule.id = rule_identity(n); rule.trigger_type = n == 2 ? pulse_type() : schema.type;
        rule.trigger_subject = entity(2); rule.condition_count = 1;
        rule.conditions[0].operation = RuleConditionOperation::relation_exists; rule.conditions[0].relation = connection();
        if (n == 2) {
            rule.effect.operation = RuleEffectOperation::emit_event; rule.effect.event.subject = entity(2);
            rule.effect.event.target = entity(3); rule.effect.event.payload = payload;
        } else {
            rule.effect.operation = RuleEffectOperation::set_component; rule.effect.entity = entity(3);
            rule.effect.component = payload;
        }
        if (c.add_rule(rule) != Result::success) return false;
    }
    return true;
}
class InteractionGame final : public Game {
    bool automatic_ = false, up_ = false, down_ = false, left_ = false, right_ = false,
         brake_ = false;
    float yaw_ = 0, pitch_ = -0.35F;
    void camera(Context &c, const EntitySnapshot &target, bool carried, const EntitySnapshot &car) {
        const float heading = carried ? std::atan2(2 * (car.rotation[3] * car.rotation[1] +
                                                        car.rotation[0] * car.rotation[2]),
                                                   1 - 2 * (car.rotation[1] * car.rotation[1] +
                                                            car.rotation[0] * car.rotation[0]))
                                      : 0;
        const float angle = heading + yaw_;
        CameraPose pose;
        pose.position[0] = target.position[0] - 6 * std::sin(angle);
        pose.position[1] = target.position[1] + 3;
        pose.position[2] = target.position[2] - 6 * std::cos(angle);
        const float sy = std::sin((angle + 3.141592654F) * 0.5F),
                    cy = std::cos((angle + 3.141592654F) * 0.5F), sp = std::sin(pitch_ * 0.5F),
                    cp = std::cos(pitch_ * 0.5F);
        pose.rotation[0] = cy * sp;
        pose.rotation[1] = sy * cp;
        pose.rotation[2] = -sy * sp;
        pose.rotation[3] = cy * cp;
        (void)c.set_camera_pose(entity(4), pose);
    }
    void present(Context &c) {
        CharacterAttachmentSnapshot attachment;
        CharacterSnapshot actor;
        EntitySnapshot car;
        VehicleSnapshot vehicle;
        AnimationSnapshot previous;
        if (c.read_character_attachment(entity(3), attachment) != Result::success ||
            c.read_character(entity(3), actor) != Result::success ||
            c.read_entity(entity(2), car) != Result::success ||
            c.read_vehicle(entity(2), vehicle) != Result::success ||
            c.read_animation(entity(3), previous) != Result::success)
            return;
        const bool mounted = attachment.attached != 0;
        const float walk = std::hypot(actor.horizontal_velocity.x, actor.horizontal_velocity.z);
        const std::uint32_t clip = mounted ? 2U : (walk > 0.1F ? 1U : 0U);
        (void)c.control_animation(entity(3), clip,
                                  clip == 1 ? std::clamp(walk / 1.5F, 0.6F, 1.8F) : 1);
        if (mounted)
            (void)c.set_character_facing(
                entity(3), {car.rotation[0], car.rotation[1], car.rotation[2], car.rotation[3]});
        else if (walk > 0.1F) {
            const float yaw = std::atan2(actor.horizontal_velocity.x, actor.horizontal_velocity.z);
            (void)c.set_character_facing(entity(3),
                                         {0, std::sin(yaw * .5F), 0, std::cos(yaw * .5F)});
        }
        const bool stepping = clip == 1 && actor.ground_state == CharacterGroundState::grounded;
        (void)c.control_audio(entity(3), 0.15F, 1, stepping, stepping && previous.clip != 1);
        const float speed = std::abs(vehicle.forward_speed);
        (void)c.control_audio(entity(2), std::clamp(0.04F + speed * 0.006F, 0.04F, 0.15F),
                              std::clamp(0.75F + speed * 0.08F, 0.75F, 2.5F),
                              mounted || speed > 0.2F);
    }

  public:
    bool on_start(Context &c) override {
        ComponentSchema schema;
        schema.type = session_type();
        schema.field_count = 2;
        std::strcpy(schema.name, "SessionState");
        for (unsigned i = 0; i < 2; ++i) {
            schema.fields[i].id = i + 1;
            schema.fields[i].kind = ComponentKind::integer;
        }
        std::strcpy(schema.fields[0].name, "launch_count");
        std::strcpy(schema.fields[1].name, "interaction_count");
        if (c.register_component_schema(schema) != Result::success || !session(c, true))
            return false;
        auto edge_schema = schema;
        edge_schema.type = connection_type();
        std::strcpy(edge_schema.name, "ConnectionState");
        if (c.register_component_schema(edge_schema) != Result::success ||
            !relation_session(c, true) || !lifecycle_fact(c) || !start_pulse(c) ||
            !start_quantities(c) || !start_rules(c))
            return false;
        CharacterSnapshot character;
        CharacterAttachmentSnapshot a;
        if (c.read_character_attachment(entity(3), a) != Result::success)
            return false;
        if (c.read_character(entity(3), character) == Result::success)
            automatic_ = a.attached || character.horizontal_velocity.x > 0;
        return true;
    }
    void on_event(Context &c, const Event &e) override {
        if (e.kind == EventKind::window_focus_changed && !(e.flags & 1U)) {
            up_ = down_ = left_ = right_ = brake_ = false;
            (void)c.control_vehicle(entity(2), 0, 0, 1);
            (void)c.control_character(entity(3), {});
        }
        if (e.kind == EventKind::mouse_move) {
            yaw_ += static_cast<float>(e.delta_x) * 0.003F;
            pitch_ = std::clamp(pitch_ + static_cast<float>(e.delta_y) * 0.002F, -1.1F, -0.1F);
        }
        if (e.kind != EventKind::key)
            return;
        const auto key = static_cast<Key>(e.code);
        const bool pressed = (e.flags & 1U) != 0;
        if (key == Key::e && pressed) {
            (void)session(c, false);
            (void)relation_session(c, false);
            automatic_ = false;
            CharacterAttachmentSnapshot a;
            (void)c.read_character_attachment(entity(3), a);
            const auto r = a.attached ? c.detach_character(entity(3), {2, -0.5F, 0}, 1, 0.5F)
                                      : c.attach_character(entity(3), entity(2), {0, -0.4F, 0}, 3);
            if (r == Result::blocked)
                c.log(LogLevel::warning, "Exit blocked: stop the car and choose clear ground.");
            if (r == Result::out_of_range)
                c.log(LogLevel::warning, "Move closer to the car before entering.");
        }
        if (key == Key::up) {
            up_ = pressed;
            automatic_ = false;
        }
        if (key == Key::down) {
            down_ = pressed;
            automatic_ = false;
        }
        if (key == Key::left) {
            left_ = pressed;
            automatic_ = false;
        }
        if (key == Key::right) {
            right_ = pressed;
            automatic_ = false;
        }
        if (key == Key::space) {
            brake_ = pressed;
            automatic_ = false;
        }
    }
    void on_update(Context &c, double) override {
        CharacterAttachmentSnapshot attachment;
        EntitySnapshot player, car;
        VehicleSnapshot vehicle;
        if (c.read_character_attachment(entity(3), attachment) != Result::success ||
            c.read_entity(entity(3), player) != Result::success ||
            c.read_entity(entity(2), car) != Result::success)
            return;
        const bool mounted = attachment.attached != 0;
        if (automatic_ && c.read_vehicle(entity(2), vehicle) == Result::success) {
            if (!mounted && vehicle.distance < 10) {
                if (player.position[0] < car.position[0] - 1.5)
                    (void)c.control_character(entity(3), {0.8F, 0, 0});
                else
                    (void)c.attach_character(entity(3), entity(2), {0, -0.4F, 0}, 3);
            } else if (mounted && vehicle.distance < 10)
                (void)c.control_vehicle(entity(2), 0.7F, 0, 0);
            else if (mounted) {
                (void)c.control_vehicle(entity(2), 0, 0, 1);
                if (std::abs(vehicle.forward_speed) < 0.15F &&
                    c.detach_character(entity(3), {2, -0.5F, 0}, 1, 0.5F) == Result::success)
                    automatic_ = false;
            }
        } else if (mounted)
            (void)c.control_vehicle(entity(2), static_cast<float>(up_) - static_cast<float>(down_),
                                    static_cast<float>(left_) - static_cast<float>(right_),
                                    brake_ ? 1.0F : 0.0F);
        else {
            const float forward = static_cast<float>(up_) - static_cast<float>(down_),
                        side = static_cast<float>(right_) - static_cast<float>(left_);
            (void)c.control_character(entity(3),
                                      {2 * (forward * std::sin(yaw_) - side * std::cos(yaw_)), 0,
                                       2 * (forward * std::cos(yaw_) + side * std::sin(yaw_))},
                                      brake_ ? 5.0F : 0.0F);
        }
        (void)c.read_entity(entity(3), player);
        camera(c, player, mounted, car);
        present(c);
    }
    void on_frame(Context &, Frame &frame) override {
        frame.clear_color = {0.025F, 0.045F, 0.07F, 1};
    }
};
} // namespace
VICE_GAME_MODULE(InteractionGame)
