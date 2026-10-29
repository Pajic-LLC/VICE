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
class ScheduleGame final : public Game {
    bool valid_ = true;
    ScheduleRequest request_;
    EntityId schedule_{};

  public:
    bool on_start(Context &c) override {
        ComponentSchema schema;
        schema.type = id(91);
        std::strcpy(schema.name, "TimedFact");
        valid_ &= c.register_component_schema(schema) == Result::success;
        request_.id = id(92);
        request_.due_ticks = 10;
        request_.interval_ticks = 10;
        request_.catch_up = ScheduleCatchUp::replay;
        request_.event.subject = id(3);
        request_.event.target = id(2);
        request_.event.payload.type = id(91);
        ScheduleSnapshot previous;
        const auto read = c.read_schedule(id(92), previous);
        if (read == Result::no_schedule)
            valid_ &= c.schedule_event(request_, schedule_) == Result::success;
        else {
            schedule_ = id(92);
            valid_ &= read == Result::success;
        }
        valid_ &= c.configure_world_clock(true, 1000, false) == Result::success;
        EntityId unchanged = id(99);
        auto bad = request_;
        bad.size = 0;
        valid_ &= c.schedule_event(bad, unchanged) == Result::invalid_argument &&
                  unchanged.bytes[15] == 99;
        bad = request_;
        bad.catch_up = static_cast<ScheduleCatchUp>(99);
        valid_ &= c.schedule_event(bad, unchanged) == Result::invalid_argument;
        valid_ &= c.schedule_event(request_, unchanged) == Result::duplicate_id &&
                  c.cancel_schedule(id(99)) == Result::no_schedule;
        SchedulerSnapshot info;
        valid_ &= c.read_scheduler(info) == Result::success && info.count == 1 &&
                  info.pending == 1 && info.has_next == 1;
        SchedulePageSnapshot page;
        valid_ &= c.read_schedules(page) == Result::success && page.count == 1 &&
                  page.entries[0].id.bytes[15] == 92 && c.read_schedules(page) == Result::success &&
                  page.count == 0;
        previous.size = 0;
        valid_ &= c.read_schedule(schedule_, previous) == Result::invalid_argument;
        ScheduleDispatchSnapshot dispatch;
        valid_ &= c.dispatch_schedules(0, dispatch) == Result::invalid_argument;
        Result other = Result::success;
        std::thread thread{[&] {
            SchedulerSnapshot out;
            other = c.read_scheduler(out);
        }};
        thread.join();
        valid_ &= other == Result::wrong_phase;
        HostApi old;
        old.size = static_cast<std::uint32_t>(offsetof(HostApi, schedule_event));
        Context legacy{&old};
        valid_ &= legacy.schedule_event(request_, unchanged) == Result::unavailable &&
                  legacy.read_scheduler(info) == Result::unavailable;
        return valid_;
    }
    void on_event(Context &c, const Event &e) override {
        if (e.kind != EventKind::key || !(e.flags & 1U))
            return;
        if (static_cast<Key>(e.code) == Key::e) {
            valid_ &= c.step_world_clock(25) == Result::success;
            ScheduleSnapshot s;
            valid_ &= c.read_schedule(schedule_, s) == Result::success && s.summary.occurrence >= 2;
            CausalHistorySnapshot h;
            WorldEventSnapshot fact;
            valid_ &=
                c.read_causal_history(h) == Result::success &&
                c.read_world_event(h.history_id, h.next_sequence - 1, fact) == Result::success;
            EventScheduleSnapshot cause;
            valid_ &= c.read_event_schedule(fact.ref, 1, cause) == Result::success &&
                      cause.has_schedule == 1 && cause.id.bytes[15] == 92 && cause.count == 1;
            auto forged = fact.ref;
            forged.id = id(99);
            valid_ &= c.read_event_schedule(forged, 1, cause) == Result::invalid_parent;
        } else if (static_cast<Key>(e.code) == Key::r) {
            valid_ &= c.cancel_schedule(schedule_) == Result::success &&
                      c.cancel_schedule(schedule_) == Result::no_schedule;
        }
    }
    void on_frame(Context &c, Frame &frame) override {
        SchedulerSnapshot info;
        ScheduleDispatchSnapshot dispatch;
        EntityId out;
        valid_ &= c.read_scheduler(info) == Result::success &&
                  c.schedule_event(request_, out) == Result::wrong_phase &&
                  c.cancel_schedule(schedule_) == Result::wrong_phase &&
                  c.dispatch_schedules(1, dispatch) == Result::wrong_phase;
        frame.clear_color = {valid_ ? 1.0F : 0.0F, 1, 1, 1};
    }
};
} // namespace
VICE_GAME_MODULE(ScheduleGame)
