#include <algorithm>
#include <cmath>
#include <iomanip>
#include <istream>
#include <locale>
#include <sstream>
#include <string_view>
#include <vice/world/world.hpp>

namespace vice::world {
namespace {
bool line_text(std::string_view s, std::size_t maximum) {
    if (s.size() > maximum)
        return false;
    // Printable UTF-8 only: canonical text records never contain embedded line breaks or NUL.
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i++]);
        if (c < 32 || c == 127)
            return false;
        if (c < 128)
            continue;
        unsigned n = 0;
        std::uint32_t value = 0, minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            n = 1;
            value = c & 31U;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            n = 2;
            value = c & 15U;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            n = 3;
            value = c & 7U;
            minimum = 0x10000;
        } else
            return false;
        if (n > s.size() - i)
            return false;
        while (n--) {
            const auto next = static_cast<unsigned char>(s[i++]);
            if ((next & 0xc0U) != 0x80U)
                return false;
            value = (value << 6U) | (next & 63U);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}
bool field_name(std::string_view s) {
    return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.';
    });
}
const char *kind_name(ComponentValueKind k) {
    switch (k) {
    case ComponentValueKind::boolean:
        return "bool";
    case ComponentValueKind::integer:
        return "integer";
    case ComponentValueKind::real:
        return "real";
    case ComponentValueKind::text:
        return "text";
    case ComponentValueKind::identifier:
        return "id";
    }
    return "invalid";
}
std::optional<ComponentValueKind> read_kind(std::istream &in) {
    std::string name;
    if (!(in >> name))
        return {};
    for (auto k :
         {ComponentValueKind::boolean, ComponentValueKind::integer, ComponentValueKind::real,
          ComponentValueKind::text, ComponentValueKind::identifier})
        if (name == kind_name(k))
            return k;
    return {};
}
void quote(std::ostream &out, std::string_view s) {
    out << '"';
    for (const char raw : s) {
        const auto c = static_cast<unsigned char>(raw);
        if (c == '"' || c == '\\')
            out << '\\' << static_cast<char>(c);
        else if (c < 32)
            out << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
        else
            out << static_cast<char>(c);
    }
    out << '"';
}
void scalar(std::ostream &out, const ComponentScalar &value, bool json) {
    switch (component_value_kind(value)) {
    case ComponentValueKind::boolean:
        out << (json ? (std::get<bool>(value) ? "true" : "false")
                     : (std::get<bool>(value) ? "1" : "0"));
        break;
    case ComponentValueKind::integer:
        if (json)
            out << '"';
        out << std::get<std::int64_t>(value);
        if (json)
            out << '"';
        break;
    case ComponentValueKind::real: {
        const auto v = std::get<double>(value);
        out << (v == 0 ? 0 : v);
        break;
    }
    case ComponentValueKind::text:
        if (json)
            quote(out, std::get<std::string>(value));
        else
            out << std::quoted(std::get<std::string>(value));
        break;
    case ComponentValueKind::identifier:
        if (json)
            out << '"';
        out << std::get<PersistentId>(value).to_string();
        if (json)
            out << '"';
        break;
    }
}
} // namespace
ComponentValueKind component_value_kind(const ComponentScalar &v) noexcept {
    return static_cast<ComponentValueKind>(v.index() + 1);
}
bool valid_component_type(const ComponentTypeDesc &s) {
    if (!s.id.valid() || s.version == 0 || s.name.empty() || !line_text(s.name, 4096) ||
        s.fields.size() > max_component_fields)
        return false;
    if (!s.fields.empty() && !line_text(s.name, 128))
        return false;
    for (std::size_t i = 0; i < s.fields.size(); ++i) {
        const auto &f = s.fields[i];
        const auto k = static_cast<unsigned>(f.kind);
        if (!f.id || k < 1 || k > 5 || !field_name(f.name))
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (s.fields[j].id == f.id || s.fields[j].name == f.name)
                return false;
    }
    return true;
}
bool valid_component_payload(const ComponentPayload &p, const ComponentTypeDesc &s) {
    if (!valid_component_type(s) || p.type != s.id || p.version != s.version ||
        p.fields.size() != s.fields.size())
        return false;
    for (std::size_t i = 0; i < p.fields.size(); ++i) {
        const auto &v = p.fields[i];
        const auto f = std::find_if(s.fields.begin(), s.fields.end(),
                                    [&](const auto &field) { return field.id == v.id; });
        if (f == s.fields.end() || f->kind != component_value_kind(v.value))
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (p.fields[j].id == v.id)
                return false;
        if (const auto real = std::get_if<double>(&v.value); real && !std::isfinite(*real))
            return false;
        if (const auto text = std::get_if<std::string>(&v.value);
            text && !line_text(*text, max_component_text_bytes))
            return false;
        if (const auto id = std::get_if<PersistentId>(&v.value); id && id->is_nil())
            return false;
    }
    return true;
}
bool World::validate_payload_upsert(const std::vector<ComponentPayload> &data,
                                    const ComponentPayload &payload, std::size_t total) const {
    const auto type = component_types_.find(payload.type);
    if (type == component_types_.end() || !valid_component_payload(payload, type->second))
        return false;
    const auto found = std::lower_bound(
        data.begin(), data.end(), payload.type,
        [](const auto &a, ComponentTypeId b) { return a.type.value.bytes() < b.value.bytes(); });
    return (found != data.end() && found->type == payload.type) ||
           (data.size() < max_entity_components && total < max_world_component_payloads);
}
bool World::set_component(EntityId entity, ComponentPayload payload) {
    return alive(entity) && upsert_payload(slots_[entity.index].components, std::move(payload),
                                           component_payload_count_);
}
bool World::upsert_payload(std::vector<ComponentPayload> &data, ComponentPayload payload,
                           std::size_t &total) {
    if (!validate_payload_upsert(data, payload, total))
        return false;
    const auto found = std::lower_bound(
        data.begin(), data.end(), payload.type,
        [](const auto &a, ComponentTypeId b) { return a.type.value.bytes() < b.value.bytes(); });
    const bool replacing = found != data.end() && found->type == payload.type;
    std::sort(payload.fields.begin(), payload.fields.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    for (auto &f : payload.fields)
        if (auto v = std::get_if<double>(&f.value); v && *v == 0)
            *v = 0;
    if (replacing)
        *found = std::move(payload);
    else {
        ++component_users_[payload.type];
        ++total;
        data.insert(found, std::move(payload));
    }
    return true;
}
bool World::remove_component(EntityId entity, ComponentTypeId type) {
    return alive(entity) &&
           erase_payload(slots_[entity.index].components, type, component_payload_count_);
}
bool World::erase_payload(std::vector<ComponentPayload> &data, ComponentTypeId type,
                          std::size_t &total) {
    const auto found =
        std::lower_bound(data.begin(), data.end(), type, [](const auto &a, ComponentTypeId b) {
            return a.type.value.bytes() < b.value.bytes();
        });
    if (found == data.end() || found->type != type)
        return false;
    if (--component_users_.at(type) == 0)
        component_users_.erase(type);
    --total;
    data.erase(found);
    return true;
}
std::optional<ComponentPayload> World::component(EntityId entity, ComponentTypeId type) const {
    if (!alive(entity))
        return {};
    const auto &data = slots_[entity.index].components;
    const auto found =
        std::lower_bound(data.begin(), data.end(), type, [](const auto &a, ComponentTypeId b) {
            return a.type.value.bytes() < b.value.bytes();
        });
    return found != data.end() && found->type == type ? std::optional{*found} : std::nullopt;
}
std::vector<ComponentPayload> World::components(EntityId entity) const {
    return alive(entity) ? slots_[entity.index].components : std::vector<ComponentPayload>{};
}
void World::clear_components(EntityId entity) {
    clear_payloads(slots_[entity.index].components, component_payload_count_);
}
void World::clear_payloads(std::vector<ComponentPayload> &data, std::size_t &total) {
    for (const auto &p : data) {
        if (--component_users_.at(p.type) == 0)
            component_users_.erase(p.type);
        --total;
    }
    data.clear();
}
bool read_component_fields(std::istream &in, std::vector<ComponentFieldDesc> &fields) {
    in.imbue(std::locale::classic());
    std::size_t count = 0;
    if (!(in >> count) || count > max_component_fields)
        return false;
    std::vector<ComponentFieldDesc> parsed;
    parsed.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        ComponentFieldDesc f;
        if (!(in >> f.id))
            return false;
        const auto kind = read_kind(in);
        if (!kind || !(in >> std::quoted(f.name)))
            return false;
        f.kind = *kind;
        parsed.push_back(std::move(f));
    }
    fields = std::move(parsed);
    return true;
}
void write_component_fields(std::ostream &out, const std::vector<ComponentFieldDesc> &fields) {
    out.imbue(std::locale::classic());
    out << fields.size();
    for (const auto &f : fields)
        out << ' ' << f.id << ' ' << kind_name(f.kind) << ' ' << std::quoted(f.name);
}
bool read_component_payload(std::istream &in, ComponentPayload &p) {
    in.imbue(std::locale::classic());
    std::string id;
    std::size_t count = 0;
    ComponentPayload parsed;
    if (!(in >> id >> parsed.version >> count) || count > max_component_fields)
        return false;
    const auto type = PersistentId::parse(id);
    if (!type)
        return false;
    parsed.type = {*type};
    parsed.fields.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        ComponentFieldValue v;
        if (!(in >> v.id))
            return false;
        const auto kind = read_kind(in);
        if (!kind)
            return false;
        switch (*kind) {
        case ComponentValueKind::boolean: {
            unsigned b = 0;
            if (!(in >> b) || b > 1)
                return false;
            v.value = b != 0;
            break;
        }
        case ComponentValueKind::integer: {
            std::int64_t n = 0;
            if (!(in >> n))
                return false;
            v.value = n;
            break;
        }
        case ComponentValueKind::real: {
            double n = 0;
            if (!(in >> n) || !std::isfinite(n))
                return false;
            v.value = n == 0 ? 0 : n;
            break;
        }
        case ComponentValueKind::text: {
            std::string s;
            if (!(in >> std::quoted(s)) || !line_text(s, max_component_text_bytes))
                return false;
            v.value = std::move(s);
            break;
        }
        case ComponentValueKind::identifier: {
            std::string s;
            if (!(in >> s))
                return false;
            const auto value = PersistentId::parse(s);
            if (!value || value->is_nil())
                return false;
            v.value = *value;
            break;
        }
        }
        parsed.fields.push_back(std::move(v));
    }
    p = std::move(parsed);
    return true;
}
void write_component_payload(std::ostream &out, const ComponentPayload &p) {
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << p.type.value.to_string() << ' ' << p.version << ' '
        << p.fields.size();
    for (const auto &f : p.fields) {
        out << ' ' << f.id << ' ' << kind_name(component_value_kind(f.value)) << ' ';
        scalar(out, f.value, false);
    }
}
std::string serialize_component_type(const ComponentTypeDesc &s) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"id\":\"" << s.id.value.to_string() << "\",\"name\":";
    quote(out, s.name);
    out << ",\"version\":" << s.version << ",\"fields\":[";
    bool comma = false;
    for (const auto &f : s.fields) {
        if (comma)
            out << ',';
        comma = true;
        out << "{\"id\":" << f.id << ",\"kind\":\"" << kind_name(f.kind) << "\",\"name\":";
        quote(out, f.name);
        out << '}';
    }
    out << "]}";
    return out.str();
}
std::string serialize_component_payloads(const std::vector<ComponentPayload> &ps) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << '[';
    bool comma = false;
    for (const auto &p : ps) {
        if (comma)
            out << ',';
        comma = true;
        out << "{\"type\":\"" << p.type.value.to_string() << "\",\"version\":" << p.version
            << ",\"fields\":[";
        bool next = false;
        for (const auto &f : p.fields) {
            if (next)
                out << ',';
            next = true;
            out << "{\"id\":" << f.id << ",\"kind\":\"" << kind_name(component_value_kind(f.value))
                << "\",\"value\":";
            scalar(out, f.value, true);
            out << '}';
        }
        out << "]}";
    }
    out << ']';
    return out.str();
}
} // namespace vice::world
