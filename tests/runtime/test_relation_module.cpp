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
constexpr RelationKey edge() { return {id(3), id(79), id(2)}; }
class RelationGame final : public Game {
    bool valid_ = true;
    RelationPage page_;

  public:
    bool on_start(Context &c) override {
        ComponentSchema s;
        s.type = id(80);
        s.field_count = 2;
        std::strcpy(s.name, "RelationState");
        s.fields[0].id = 1;
        s.fields[0].kind = ComponentKind::integer;
        std::strcpy(s.fields[0].name, "counter");
        s.fields[1].id = 2;
        s.fields[1].kind = ComponentKind::real;
        std::strcpy(s.fields[1].name, "weight");
        valid_ &= c.register_component_schema(s) == Result::success &&
                  c.add_relation(edge()) == Result::success;
        ComponentData d;
        auto result = c.read_relation_component(edge(), id(80), d);
        if (result == Result::no_component) {
            d.type = id(80);
            d.field_count = 2;
            d.fields[0].id = 1;
            d.fields[0].kind = ComponentKind::integer;
            d.fields[0].integer = 5;
            d.fields[1].id = 2;
            d.fields[1].kind = ComponentKind::real;
            d.fields[1].real = .8;
            valid_ &= c.set_relation_component(edge(), d) == Result::success;
        } else
            valid_ &= result == Result::success;
        valid_ &= c.read_relations(id(3), RelationDirection::outgoing, id(79), page_) ==
                      Result::success &&
                  page_.count == 64 && page_.has_more == 1;
        auto tail = page_;

        valid_ &=
            c.read_relations(id(3), RelationDirection::outgoing, id(79), tail) == Result::success &&
            tail.count == 7 && tail.has_more == 0;
        auto bad = d;
        bad.version = 2;
        valid_ &= c.set_relation_component(edge(), bad) == Result::schema_conflict;
        bad = d;
        bad.fields[1].real = std::numeric_limits<double>::quiet_NaN();
        valid_ &= c.set_relation_component(edge(), bad) == Result::invalid_argument;
        valid_ &=
            c.read_relations(id(3), RelationDirection::outgoing, id(79), page_) == Result::success;
        RelationInfo info;
        valid_ &= c.read_relation(edge(), info) == Result::success && info.component_count == 1;
        info.size = 0;
        valid_ &= c.read_relation(edge(), info) == Result::invalid_argument;
        const RelationKey absent{id(2), id(79), id(3)};
        valid_ &= c.read_relation_component(absent, id(80), d) == Result::no_relation &&
                  c.remove_relation(absent) == Result::no_relation &&
                  c.add_relation({id(99), id(79), id(2)}) == Result::not_found;
        auto small = page_;
        small.size = 0;
        valid_ &=
            c.read_relations(id(3), RelationDirection::both, {}, small) == Result::invalid_argument;
        Result other = Result::success;
        std::thread thread{[&] {
            RelationInfo out;
            other = c.read_relation(edge(), out);
        }};
        thread.join();
        valid_ &= other == Result::wrong_phase;
        HostApi old;
        old.size = static_cast<std::uint32_t>(offsetof(HostApi, read_relation));
        Context legacy{&old};
        valid_ &= legacy.read_relation(edge(), info) == Result::unavailable &&
                  legacy.add_relation(edge()) == Result::unavailable;
        return valid_;
    }
    void on_event(Context &c, const Event &e) override {
        if (e.kind != EventKind::key || !(e.flags & 1U))
            return;
        if (static_cast<Key>(e.code) == Key::r) {
            valid_ &= c.read_relations(id(3), RelationDirection::outgoing, id(79), page_) ==
                      Result::reset_required;
            page_ = {};
            valid_ &= c.read_relations(id(3), RelationDirection::outgoing, id(79), page_) ==
                      Result::success;
            return;
        }
        ComponentData d;
        valid_ &= c.read_relation_component(edge(), id(80), d) == Result::success &&
                  c.add_relation(edge()) == Result::success;
        valid_ &= c.remove_relation_component(edge(), id(80)) == Result::success;
        ++d.fields[0].integer;
        valid_ &= c.set_relation_component(edge(), d) == Result::success;
        valid_ &= c.read_relations(id(3), RelationDirection::outgoing, id(79), page_) ==
                  Result::reset_required;
        page_ = {};
        valid_ &=
            c.read_relations(id(3), RelationDirection::outgoing, id(79), page_) == Result::success;
    }
    void on_frame(Context &c, Frame &f) override {
        ComponentData d;
        valid_ &= c.read_relation_component(edge(), id(80), d) == Result::success;
        valid_ &= c.set_relation_component(edge(), d) == Result::wrong_phase &&
                  c.remove_relation(edge()) == Result::wrong_phase &&
                  c.add_relation(edge()) == Result::wrong_phase &&
                  c.remove_relation_component(edge(), id(80)) == Result::wrong_phase;
        f.clear_color = {valid_ ? 1.0F : 0.0F, d.fields[0].integer >= 6 ? 1.0F : 0.0F, 1, 1};
    }
};
} // namespace
VICE_GAME_MODULE(RelationGame)
