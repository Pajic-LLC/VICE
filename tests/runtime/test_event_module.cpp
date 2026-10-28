#include <cstring>
#include <thread>
#include <vice/runtime/module_api.hpp>
namespace {
using namespace vice::runtime::module;
constexpr EntityId id(unsigned n) {
    EntityId out{};
    out.bytes[0] = 0xa2;
    out.bytes[6] = 0x40;
    out.bytes[8] = 0x80;
    out.bytes[15] = static_cast<std::uint8_t>(n);
    return out;
}
class EventGame final : public Game {
    bool valid_ = true;
    WorldEventRequest request_;
    EventRef fact_, signal_;

  public:
    bool on_start(Context &c) override {
        ComponentSchema s;
        s.type = id(90);
        std::strcpy(s.name, "ExampleFact");
        s.field_count = 1;
        s.fields[0].id = 1;
        s.fields[0].kind = ComponentKind::integer;
        std::strcpy(s.fields[0].name, "counter");
        valid_ &= c.register_component_schema(s) == Result::success;
        request_.subject = id(3);
        request_.target = id(2);
        request_.payload.type = id(90);
        request_.payload.field_count = 1;
        request_.payload.fields[0].id = 1;
        request_.payload.fields[0].kind = ComponentKind::integer;
        request_.payload.fields[0].integer = 1;
        CausalHistorySnapshot history;
        valid_ &= c.read_causal_history(history) == Result::success;
        for (auto seq = history.next_sequence; seq > history.first_sequence;) {
            WorldEventSnapshot prior;
            --seq;
            valid_ &= c.read_world_event(history.history_id, seq, prior) == Result::success;
            if (prior.has_payload && prior.payload.type.bytes[15] == 90) {
                request_.parent_count = 1;
                request_.parents[0] = prior.ref;
                request_.payload.fields[0].integer = prior.payload.fields[0].integer + 1;
                break;
            }
        }
        valid_ &= c.emit_world_event(request_, fact_) == Result::success;
        request_.parent_count = 1;
        request_.parents[0] = fact_;
        valid_ &= c.emit_world_event(request_, fact_) == Result::success;
        request_.parents[0] = fact_;
        request_.retained = 0;
        valid_ &= c.emit_world_event(request_, signal_) == Result::success;
        WorldEventSnapshot out;
        SignalWindowSnapshot window;
        valid_ &= c.read_world_event(fact_.history, fact_.sequence, out) == Result::success &&
                  out.has_payload == 1 && out.parent_count == 1 && out.parent_retained[0] == 1;
        valid_ &= c.read_signal_window(window) == Result::success && window.retained_count == 1 &&
                  c.read_world_signal(signal_.history, signal_.sequence, out) == Result::success &&
                  out.retained == 0 && out.parent_retained[0] == 1;
        EventRef unused;
        auto bad = request_;
        bad.size = 0;
        valid_ &= c.emit_world_event(bad, unused) == Result::invalid_argument;
        bad = request_;
        bad.retained = 2;
        valid_ &= c.emit_world_event(bad, unused) == Result::invalid_argument;
        bad = request_;
        bad.parent_count = 5;
        valid_ &= c.emit_world_event(bad, unused) == Result::invalid_argument;
        bad = request_;
        bad.parents[0].id = id(99);
        valid_ &= c.emit_world_event(bad, unused) == Result::invalid_parent;
        bad = request_;
        bad.retained = 1;
        bad.parents[0] = signal_;
        valid_ &= c.emit_world_event(bad, unused) == Result::history_mismatch;
        out.size = 0;
        valid_ &=
            c.read_world_event(fact_.history, fact_.sequence, out) == Result::invalid_argument;
        out = {};
        valid_ &= c.read_world_event(id(99), fact_.sequence, out) == Result::history_mismatch &&
                  c.read_world_event(fact_.history, fact_.sequence + 20, out) == Result::not_found;
        Result other = Result::success;
        std::thread thread{[&] {
            SignalWindowSnapshot o;
            other = c.read_signal_window(o);
        }};
        thread.join();
        valid_ &= other == Result::wrong_phase;
        HostApi old;
        old.size = static_cast<std::uint32_t>(offsetof(HostApi, emit_world_event));
        Context legacy{&old};
        valid_ &=
            legacy.emit_world_event(request_, unused) == Result::unavailable &&
            legacy.read_world_event(fact_.history, fact_.sequence, out) == Result::unavailable &&
            legacy.read_signal_window(window) == Result::unavailable;
        return valid_;
    }
    void on_event(Context &c, const Event &e) override {
        if (e.kind == EventKind::key && (e.flags & 1U)) {
            WorldEventSnapshot out;
            valid_ &=
                c.step_world_clock(1'000'000) == Result::success &&
                c.read_world_signal(signal_.history, signal_.sequence, out) == Result::event_pruned;
        }
    }
    void on_frame(Context &c, Frame &f) override {
        EventRef unused;
        WorldEventSnapshot out;
        valid_ &= c.emit_world_event(request_, unused) == Result::wrong_phase &&
                  c.read_world_event(fact_.history, fact_.sequence, out) == Result::success;
        f.clear_color = {valid_ ? 1.0F : 0.0F, 1, 1, 1};
    }
    void on_stop(Context &c) override {
        EventRef unused;
        if (c.emit_world_event(request_, unused) != Result::wrong_phase)
            c.log(LogLevel::error, "invalid event write in stop");
    }
};
} // namespace
VICE_GAME_MODULE(EventGame)
