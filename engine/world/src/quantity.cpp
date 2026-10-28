#include <algorithm>
#include <charconv>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <vice/world/world.hpp>
namespace vice::world {
namespace {
template <class T> bool number(std::istream &in, T &out) {
    std::string text;
    if (!(in >> text))
        return false;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    return r.ec == std::errc{} && r.ptr == text.data() + text.size();
}
bool identifier(std::istream &in, PersistentId &out) {
    std::string text;
    if (!(in >> text))
        return false;
    const auto id = PersistentId::parse(text);
    if (!id || id->is_nil())
        return false;
    out = *id;
    return true;
}
const ComponentFieldValue *field(const ComponentPayload &p, std::uint32_t id) {
    const auto found =
        std::find_if(p.fields.begin(), p.fields.end(), [&](const auto &f) { return f.id == id; });
    return found == p.fields.end() ? nullptr : &*found;
}
std::optional<QuantityTypeId> receipt_type(const ComponentPayload &p) {
    if (p.type != quantity_receipt_type())
        return {};
    const auto *f = field(p, 1);
    if (!f || !std::holds_alternative<PersistentId>(f->value))
        return {};
    return QuantityTypeId{std::get<PersistentId>(f->value)};
}
} // namespace
ComponentTypeId quantity_receipt_type() noexcept {
    static const ComponentTypeId id{*PersistentId::parse("c7000000-0000-4000-8000-000000000001")};
    return id;
}
const ComponentTypeDesc &quantity_receipt_schema() {
    using K = ComponentValueKind;
    static const ComponentTypeDesc schema{quantity_receipt_type(),
                                          "vice.quantity.transaction",
                                          1,
                                          {{1, K::identifier, "resource_type"},
                                           {2, K::integer, "resource_version"},
                                           {3, K::integer, "operation"},
                                           {4, K::integer, "quantity"},
                                           {5, K::integer, "source_before"},
                                           {6, K::integer, "source_after"},
                                           {7, K::integer, "target_before"},
                                           {8, K::integer, "target_after"}}};
    return schema;
}
bool valid_quantity_type(const QuantityTypeDesc &t) {
    return t.quanta_per_unit && t.name.size() <= 128 &&
           valid_component_type({{t.id.value}, t.name, t.version, {}});
}
const char *quantity_result_name(QuantityResult r) noexcept {
    switch (r) {
    case QuantityResult::success:
        return "success";
    case QuantityResult::invalid_argument:
        return "invalid_argument";
    case QuantityResult::invalid_type:
        return "invalid_type";
    case QuantityResult::invalid_owner:
        return "invalid_owner";
    case QuantityResult::missing_account:
        return "missing_account";
    case QuantityResult::insufficient_quantity:
        return "insufficient_quantity";
    case QuantityResult::capacity_exceeded:
        return "capacity_exceeded";
    case QuantityResult::nonempty_account:
        return "nonempty_account";
    case QuantityResult::duplicate_account:
        return "duplicate_account";
    case QuantityResult::schema_conflict:
        return "schema_conflict";
    case QuantityResult::storage_limit:
        return "storage_limit";
    case QuantityResult::event_rejected:
        return "event_rejected";
    }
    return "invalid";
}
bool World::register_quantity_type(QuantityTypeDesc t) {
    if (!valid_quantity_type(t))
        return false;
    const auto found = quantity_types_.find(t.id.value);
    if (found != quantity_types_.end())
        return found->second == t;
    if (quantity_types_.size() >= quantity_type_capacity ||
        std::any_of(quantity_types_.begin(), quantity_types_.end(),
                    [&](const auto &old) { return old.second.name == t.name; }))
        return false;
    const auto schema = quantity_receipt_schema();
    const auto registered = component_type(schema.id);
    if (registered) {
        if (*registered != schema)
            return false;
    } else if (!register_component_type(schema))
        return false;
    quantity_types_.emplace(t.id.value, std::move(t));
    return true;
}
bool World::remove_quantity_type(QuantityTypeId type) {
    if (quantity_type_uses_.contains(type.value))
        return false;
    return quantity_types_.erase(type.value) == 1;
}
std::optional<QuantityTypeDesc> World::quantity_type(QuantityTypeId type) const {
    const auto found = quantity_types_.find(type.value);
    return found == quantity_types_.end() ? std::nullopt : std::optional{found->second};
}
QuantityTypePage World::quantity_type_page(PersistentId after, std::size_t limit) const {
    QuantityTypePage out;
    limit = std::clamp(limit, std::size_t{1}, quantity_query_limit);
    auto i = after.is_nil() ? quantity_types_.begin() : quantity_types_.upper_bound(after);
    for (; i != quantity_types_.end() && out.entries.size() < limit; ++i)
        out.entries.push_back(i->second);
    out.has_more = i != quantity_types_.end();
    return out;
}
std::optional<QuantityAccount> World::quantity_account(QuantityKey key) const {
    const auto found = quantity_accounts_.find(key);
    return found == quantity_accounts_.end() ? std::nullopt : std::optional{found->second};
}
std::vector<QuantityAccount> World::quantity_accounts(PersistentId holder) const {
    std::vector<QuantityAccount> out;
    for (auto i = quantity_accounts_.lower_bound({holder, {}});
         i != quantity_accounts_.end() && i->first.holder == holder; ++i)
        out.push_back(i->second);
    return out;
}
bool World::install_quantity_account(QuantityAccount a) {
    if (a.amount < 0 || a.capacity < 0 || a.amount > a.capacity || !find(a.key.holder) ||
        !quantity_types_.contains(a.key.type.value) || quantity_accounts_.contains(a.key) ||
        quantity_accounts_.size() >= quantity_account_capacity)
        return false;
    std::size_t count = 0;
    for (auto i = quantity_accounts_.lower_bound({a.key.holder, {}});
         i != quantity_accounts_.end() && i->first.holder == a.key.holder; ++i)
        if (++count >= quantity_accounts_per_holder)
            return false;
    quantity_accounts_.emplace(a.key, a);
    ++quantity_type_uses_[a.key.type.value];
    return true;
}
QuantityResult World::create_quantity_account(QuantityKey key, std::int64_t capacity) {
    if (capacity < 0)
        return QuantityResult::invalid_argument;
    if (!quantity_types_.contains(key.type.value))
        return QuantityResult::invalid_type;
    if (!find(key.holder))
        return QuantityResult::invalid_owner;
    if (quantity_accounts_.contains(key))
        return QuantityResult::duplicate_account;
    if (!install_quantity_account({key, 0, capacity}))
        return QuantityResult::storage_limit;
    return QuantityResult::success;
}
QuantityResult World::set_quantity_capacity(QuantityKey key, std::int64_t capacity) {
    if (capacity < 0)
        return QuantityResult::invalid_argument;
    if (!quantity_types_.contains(key.type.value))
        return QuantityResult::invalid_type;
    if (!find(key.holder))
        return QuantityResult::invalid_owner;
    const auto found = quantity_accounts_.find(key);
    if (found == quantity_accounts_.end())
        return QuantityResult::missing_account;
    if (capacity < found->second.amount)
        return QuantityResult::capacity_exceeded;
    found->second.capacity = capacity;
    return QuantityResult::success;
}
void World::release_quantity_type(QuantityTypeId type) {
    const auto found = quantity_type_uses_.find(type.value);
    if (found != quantity_type_uses_.end() && --found->second == 0)
        quantity_type_uses_.erase(found);
}
QuantityResult World::remove_quantity_account(QuantityKey key) {
    if (!quantity_types_.contains(key.type.value))
        return QuantityResult::invalid_type;
    if (!find(key.holder))
        return QuantityResult::invalid_owner;
    const auto found = quantity_accounts_.find(key);
    if (found == quantity_accounts_.end())
        return QuantityResult::missing_account;
    if (found->second.amount)
        return QuantityResult::nonempty_account;
    release_quantity_type(key.type);
    quantity_accounts_.erase(found);
    return QuantityResult::success;
}
QuantityReceipt World::transact_quantity(const QuantityTransaction &t) {
    const auto fail = [](QuantityResult r, WorldEventResult e = WorldEventResult::success) {
        QuantityReceipt out;
        out.result = r;
        out.event_result = e;
        return out;
    };
    if (static_cast<unsigned>(t.operation) > 2 || t.amount <= 0 ||
        t.parents.size() > world_event_parent_capacity ||
        (t.operation == QuantityOperation::produce && !t.source.is_nil()) ||
        (t.operation == QuantityOperation::consume && !t.target.is_nil()) ||
        (t.operation == QuantityOperation::transfer && t.source == t.target))
        return fail(QuantityResult::invalid_argument);
    const auto type = quantity_types_.find(t.type.value);
    if (type == quantity_types_.end())
        return fail(QuantityResult::invalid_type);
    const bool source = t.operation != QuantityOperation::produce,
               target = t.operation != QuantityOperation::consume;
    if ((source && !find(t.source)) || (target && !find(t.target)))
        return fail(QuantityResult::invalid_owner);
    auto from = quantity_accounts_.find({t.source, t.type}),
         to = quantity_accounts_.find({t.target, t.type});
    if ((source && from == quantity_accounts_.end()) || (target && to == quantity_accounts_.end()))
        return fail(QuantityResult::missing_account);
    if (source && from->second.amount < t.amount)
        return fail(QuantityResult::insufficient_quantity);
    if (target && t.amount > to->second.capacity - to->second.amount)
        return fail(QuantityResult::capacity_exceeded);
    QuantityReceipt out;
    out.source_before = source ? from->second.amount : 0;
    out.source_after = source ? out.source_before - t.amount : 0;
    out.target_before = target ? to->second.amount : 0;
    out.target_after = target ? out.target_before + t.amount : 0;
    WorldEventRequest event;
    event.subject = source ? t.source : t.target;
    event.target = t.operation == QuantityOperation::transfer ? t.target : PersistentId{};
    event.data.parents = t.parents;
    event.data.payload = {quantity_receipt_type(),
                          1,
                          {{1, t.type.value},
                           {2, static_cast<std::int64_t>(type->second.version)},
                           {3, static_cast<std::int64_t>(t.operation)},
                           {4, t.amount},
                           {5, out.source_before},
                           {6, out.source_after},
                           {7, out.target_before},
                           {8, out.target_after}}};
    const auto emitted =
        emit_world_event_impl(std::move(event), CausalOrigin::gameplay, false, true);
    if (!emitted)
        return fail(QuantityResult::event_rejected, emitted.result);
    if (source)
        from->second.amount = out.source_after;
    if (target)
        to->second.amount = out.target_after;
    out.event = emitted.ref;
    out.result = QuantityResult::success;
    return out;
}
bool World::valid_quantity_receipt(const CausalEvent &e) const {
    if (!e.data || e.data->payload.type != quantity_receipt_type())
        return true;
    const auto &p = e.data->payload;
    if (e.kind != CausalKind::world_event || e.origin != CausalOrigin::gameplay ||
        e.data->schedule || !valid_component_payload(p, quantity_receipt_schema()))
        return false;
    const auto type = receipt_type(p);
    if (!type)
        return false;
    const auto desc = quantity_type(*type);
    if (!desc || std::get<std::int64_t>(field(p, 2)->value) != desc->version)
        return false;
    const auto n = [&](unsigned id) { return std::get<std::int64_t>(field(p, id)->value); };
    const auto op = n(3), amount = n(4), sb = n(5), sa = n(6), tb = n(7), ta = n(8);
    if (op < 0 || op > 2 || amount <= 0 || sb < 0 || sa < 0 || tb < 0 || ta < 0 ||
        !e.second.is_nil())
        return false;
    if (op == 0)
        return e.first.is_nil() && !sb && !sa &&
               amount <= std::numeric_limits<std::int64_t>::max() - tb && ta == tb + amount;
    if (op == 1)
        return e.first.is_nil() && !tb && !ta && sb >= amount && sa == sb - amount;
    return !e.first.is_nil() && e.first != e.subject && sb >= amount && sa == sb - amount &&
           amount <= std::numeric_limits<std::int64_t>::max() - tb && ta == tb + amount;
}
void World::retain_quantity_receipt(const CausalEvent &e) {
    if (e.data)
        if (const auto type = receipt_type(e.data->payload))
            ++quantity_type_uses_[type->value];
}
bool read_quantity_type(std::istream &in, QuantityTypeDesc &out) {
    QuantityTypeDesc t;
    if (!identifier(in, t.id.value) || !number(in, t.version) || !number(in, t.quanta_per_unit) ||
        !(in >> std::quoted(t.name)) || !valid_quantity_type(t))
        return false;
    out = std::move(t);
    return true;
}
void write_quantity_type(std::ostream &out, const QuantityTypeDesc &t) {
    out.imbue(std::locale::classic());
    out << t.id.value.to_string() << ' ' << t.version << ' ' << t.quanta_per_unit << ' '
        << std::quoted(t.name);
}
bool read_quantity_account(std::istream &in, QuantityAccount &out) {
    QuantityAccount a;
    if (!identifier(in, a.key.holder) || !identifier(in, a.key.type.value) ||
        !number(in, a.amount) || !number(in, a.capacity) || a.amount < 0 || a.capacity < 0 ||
        a.amount > a.capacity)
        return false;
    out = a;
    return true;
}
void write_quantity_account(std::ostream &out, const QuantityAccount &a) {
    out.imbue(std::locale::classic());
    out << a.key.holder.to_string() << ' ' << a.key.type.value.to_string() << ' ' << a.amount << ' '
        << a.capacity;
}
std::string serialize_quantity_type(const QuantityTypeDesc &t) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"id\":\"" << t.id.value.to_string() << "\",\"version\":" << t.version
        << ",\"quanta_per_unit\":" << t.quanta_per_unit << ",\"name\":\"";
    for (char c : t.name) {
        if (c == '\\' || c == '\"')
            out << '\\';
        out << c;
    }
    out << "\"}";
    return out.str();
}
std::string serialize_quantity_account(const QuantityAccount &a) {
    return "{\"holder\":\"" + a.key.holder.to_string() + "\",\"type\":\"" +
           a.key.type.value.to_string() + "\",\"amount\":\"" + std::to_string(a.amount) +
           "\",\"capacity\":\"" + std::to_string(a.capacity) + "\"}";
}
} // namespace vice::world
