#include <cstring>
#include <limits>
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
constexpr EntityId receipt_type() {
    auto out = id(1);
    out.bytes[0] = 0xc7;
    return out;
}
class QuantityGame final : public Game {
    bool valid_ = true;
    QuantityTypeSnapshot type_;
    QuantityTransactionRequest request_;
    EventRef last_{};

  public:
    bool on_start(Context &c) override {
        type_.id = id(93);
        type_.quanta_per_unit = 100;
        std::strcpy(type_.name, "ExampleUnits");
        valid_ &= c.register_quantity_type(type_) == Result::success &&
                  c.register_quantity_type(type_) == Result::success;
        QuantityAccountSnapshot from;
        const auto existing = c.read_quantity_account({id(2), id(93)}, from);
        if (existing == Result::no_quantity_account) {
            valid_ &= c.create_quantity_account({id(2), id(93)}, 100) == Result::success &&
                      c.create_quantity_account({id(3), id(93)}, 100) == Result::success;
            QuantityTransactionRequest seed;
            seed.type = id(93);
            seed.operation = QuantityOperation::produce;
            seed.target = id(2);
            seed.amount = 100;
            QuantityReceiptSnapshot r;
            valid_ &= c.transact_quantity(seed, r) == Result::success;
            last_ = r.event;
        } else
            valid_ &= existing == Result::success;
        CausalHistorySnapshot history;
        valid_ &= c.read_causal_history(history) == Result::success;
        for (auto seq = history.next_sequence; seq > history.first_sequence;) {
            WorldEventSnapshot e;
            --seq;
            valid_ &= c.read_world_event(history.history_id, seq, e) == Result::success;
            if (e.has_payload && std::memcmp(e.payload.type.bytes, receipt_type().bytes, 16) == 0) {
                last_ = e.ref;
                break;
            }
        }
        request_.type = id(93);
        request_.source = id(2);
        request_.target = id(3);
        request_.amount = 25;
        QuantityReceiptSnapshot out;
        auto bad = request_;
        bad.amount = std::numeric_limits<std::int64_t>::max();
        valid_ &= c.transact_quantity(bad, out) == Result::insufficient_quantity &&
                  out.result == Result::insufficient_quantity && out.event.sequence == 0;
        bad = request_;
        bad.type = id(99);
        valid_ &= c.transact_quantity(bad, out) == Result::invalid_quantity_type;
        bad = request_;
        bad.target = id(99);
        valid_ &= c.transact_quantity(bad, out) == Result::invalid_quantity_owner;
        bad = request_;
        bad.target = id(4);
        valid_ &= c.transact_quantity(bad, out) == Result::no_quantity_account;
        bad = request_;
        bad.size = 0;
        out.source_before = 123;
        valid_ &=
            c.transact_quantity(bad, out) == Result::invalid_argument && out.source_before == 123;
        valid_ &=
            c.remove_quantity_type(id(93)) == Result::quantity_type_in_use &&
            c.remove_quantity_account({id(2), id(93)}) == Result::quantity_not_empty &&
            c.set_quantity_capacity({id(2), id(93)}, 0) == Result::quantity_capacity_exceeded &&
            c.create_quantity_account({id(2), id(93)}, 100) == Result::quantity_account_exists;
        auto conflict = type_;
        conflict.version = 2;
        valid_ &= c.register_quantity_type(conflict) == Result::schema_conflict;
        QuantityTypePageSnapshot types;
        valid_ &= c.read_quantity_types(types) == Result::success && types.count == 1 &&
                  c.read_quantity_types(types) == Result::success && types.count == 0;
        QuantityAccountsSnapshot accounts;
        valid_ &=
            c.read_quantity_accounts(id(3), accounts) == Result::success && accounts.count == 1;
        QuantityTypeSnapshot detail;
        valid_ &= c.read_quantity_type(id(93), detail) == Result::success &&
                  detail.quanta_per_unit == 100;
        detail.size = 0;
        valid_ &= c.read_quantity_type(id(93), detail) == Result::invalid_argument;
        Result other = Result::success;
        std::thread t{[&] {
            QuantityAccountsSnapshot a;
            other = c.read_quantity_accounts(id(2), a);
        }};
        t.join();
        valid_ &= other == Result::wrong_phase;
        WorldEventSnapshot fact;
        valid_ &= c.read_world_event(last_.history, last_.sequence, fact) == Result::success;
        WorldEventRequest forged;
        forged.subject = fact.subject;
        forged.target = fact.target;
        forged.payload = fact.payload;
        EventRef ref;
        valid_ &= c.emit_world_event(forged, ref) == Result::invalid_argument;
        ScheduleRequest timer;
        timer.event = forged;
        EntityId handle;
        valid_ &= c.schedule_event(timer, handle) == Result::invalid_argument;
        HostApi old;
        old.size = static_cast<std::uint32_t>(offsetof(HostApi, register_quantity_type));
        Context legacy{&old};
        valid_ &= legacy.register_quantity_type(type_) == Result::unavailable &&
                  legacy.transact_quantity(request_, out) == Result::unavailable;
        return valid_;
    }
    void on_event(Context &c, const Event &e) override {
        if (e.kind == EventKind::key && (e.flags & 1U)) {
            request_.parent_count = 1;
            request_.parents[0] = last_;
            QuantityReceiptSnapshot result;
            valid_ &= c.transact_quantity(request_, result) == Result::success &&
                      result.source_before - result.source_after == 25 &&
                      result.target_after - result.target_before == 25;
            last_ = result.event;
        }
    }
    void on_frame(Context &c, Frame &f) override {
        QuantityReceiptSnapshot receipt;
        QuantityAccountSnapshot account;
        valid_ &= c.read_quantity_account({id(2), id(93)}, account) == Result::success &&
                  c.transact_quantity(request_, receipt) == Result::wrong_phase &&
                  c.register_quantity_type(type_) == Result::wrong_phase &&
                  c.set_quantity_capacity({id(2), id(93)}, 100) == Result::wrong_phase;
        f.clear_color = {valid_ ? 1.0F : 0.0F, 1, 1, 1};
    }
};
} // namespace
VICE_GAME_MODULE(QuantityGame)
