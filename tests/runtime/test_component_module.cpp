#include <algorithm>
#include <cstring>
#include <limits>
#include <thread>
#include <vice/runtime/module_api.hpp>
namespace {
using namespace vice::runtime::module;
constexpr EntityId id(unsigned n) {
    EntityId e{};
    e.bytes[0] = 0xa2;
    e.bytes[6] = 0x40;
    e.bytes[8] = 0x80;
    e.bytes[15] = static_cast<std::uint8_t>(n);
    return e;
}
class ComponentGame final : public Game {
    bool valid_ = true;

  public:
    bool on_start(Context &c) override {
        ComponentSchema schema;
        schema.type = id(100);
        schema.version = 2;
        schema.field_count = 5;
        std::strcpy(schema.name, "ExampleState");
        const ComponentKind kinds[] = {ComponentKind::integer, ComponentKind::boolean,
                                       ComponentKind::real, ComponentKind::text,
                                       ComponentKind::identifier};
        const char *names[] = {"counter", "enabled", "measure", "label", "stable_key"};
        for (unsigned i = 0; i < 5; ++i) {
            schema.fields[i].id = i + 1;
            schema.fields[i].kind = kinds[i];
            std::strcpy(schema.fields[i].name, names[i]);
        }
        valid_ &= c.register_component_schema(schema) == Result::success &&
                  c.register_component_schema(schema) == Result::success;
        auto bad = schema;
        bad.version = 3;
        valid_ &= c.register_component_schema(bad) == Result::schema_conflict;
        bad = schema;
        bad.size = 0;
        valid_ &= c.register_component_schema(bad) == Result::invalid_argument;
        bad = {};
        valid_ &= c.read_component_schema(id(100), bad) == Result::success && bad.field_count == 5;
        ComponentData data;
        const auto result = c.read_component(id(2), id(100), data);
        if (result == Result::no_component) {
            data.type = id(100);
            data.version = 2;
            data.field_count = 5;
            for (unsigned i = 0; i < 5; ++i) {
                data.fields[i].id = i + 1;
                data.fields[i].kind = kinds[i];
            }
            data.fields[0].integer = 7;
            data.fields[1].boolean = 1;
            data.fields[2].real = .5;
            std::strcpy(data.fields[3].text, "game defined");
            data.fields[4].identifier = id(77);
            valid_ &= c.set_component(id(2), data) == Result::success;
        } else
            valid_ &= result == Result::success;
        auto invalid = data;
        invalid.fields[2].real = std::numeric_limits<double>::quiet_NaN();
        valid_ &= c.set_component(id(2), invalid) == Result::invalid_argument;
        invalid = data;
        invalid.version = 1;
        valid_ &= c.set_component(id(2), invalid) == Result::schema_conflict;
        invalid = data;
        invalid.field_count = 17;
        valid_ &= c.set_component(id(2), invalid) == Result::invalid_argument;
        invalid = data;
        std::fill(std::begin(invalid.fields[3].text), std::end(invalid.fields[3].text), 'x');
        valid_ &= c.set_component(id(2), invalid) == Result::invalid_argument;
        ComponentData checked;
        valid_ &= c.read_component(id(2), id(100), checked) == Result::success &&
                  checked.fields[0].integer == data.fields[0].integer;
        valid_ &= c.read_component(id(99), id(100), checked) == Result::not_found &&
                  c.remove_component(id(2), id(99)) == Result::no_component;
        checked.size = 0;
        valid_ &= c.read_component(id(2), id(100), checked) == Result::invalid_argument;
        Result other = Result::success;
        std::thread thread{[&] {
            ComponentData out;
            other = c.read_component(id(2), id(100), out);
        }};
        thread.join();
        valid_ &= other == Result::wrong_phase;
        HostApi old;
        old.size = static_cast<std::uint32_t>(offsetof(HostApi, register_component_schema));
        Context legacy{&old};
        valid_ &= legacy.read_component(id(2), id(100), checked) == Result::unavailable &&
                  legacy.register_component_schema(schema) == Result::unavailable;
        return valid_;
    }
    void on_event(Context &c, const Event &e) override {
        if (e.kind == EventKind::key && (e.flags & 1U)) {
            ComponentData data;
            valid_ &= c.read_component(id(2), id(100), data) == Result::success;
            valid_ &= c.remove_component(id(2), id(100)) == Result::success;
            ++data.fields[0].integer;
            valid_ &= c.set_component(id(2), data) == Result::success;
        }
    }
    void on_frame(Context &c, Frame &f) override {
        ComponentData d;
        valid_ &= c.read_component(id(2), id(100), d) == Result::success;
        valid_ &= c.set_component(id(2), d) == Result::wrong_phase &&
                  c.remove_component(id(2), id(100)) == Result::wrong_phase;
        ComponentSchema s;
        valid_ &= c.register_component_schema(s) == Result::wrong_phase;
        f.clear_color = {valid_ ? 1.0F : 0.0F, d.fields[0].integer >= 8 ? 1.0F : 0.0F, 1, 1};
    }
};
} // namespace
VICE_GAME_MODULE(ComponentGame)
