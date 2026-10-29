#pragma once

#include <vice/world/persistent_id.hpp>

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <variant>
#include <vector>

namespace vice::world {

struct ComponentTypeId {
    PersistentId value{};

    [[nodiscard]] constexpr bool valid() const noexcept { return !value.is_nil(); }

    friend constexpr bool operator==(const ComponentTypeId &,
                                     const ComponentTypeId &) noexcept = default;
};

struct ComponentTypeIdHash {
    [[nodiscard]] std::size_t operator()(const ComponentTypeId &id) const noexcept {
        return PersistentIdHash{}(id.value);
    }
};

// These tags are persistent wire values. Never reorder or reuse them.
enum class ComponentValueKind : std::uint32_t {
    boolean = 1,
    integer = 2,
    real = 3,
    text = 4,
    identifier = 5
};
inline constexpr std::size_t max_component_fields = 16;
inline constexpr std::size_t max_entity_components = 16;
inline constexpr std::size_t max_world_component_payloads = 65536;
inline constexpr std::size_t max_component_text_bytes = 128;
struct ComponentFieldDesc {
    std::uint32_t id = 0;
    ComponentValueKind kind = ComponentValueKind::boolean;
    std::string name;
    friend bool operator==(const ComponentFieldDesc &, const ComponentFieldDesc &) = default;
};
using ComponentScalar = std::variant<bool, std::int64_t, double, std::string, PersistentId>;
struct ComponentFieldValue {
    std::uint32_t id = 0;
    ComponentScalar value = false;
    friend bool operator==(const ComponentFieldValue &, const ComponentFieldValue &) = default;
};
struct ComponentPayload {
    ComponentTypeId type;
    std::uint32_t version = 1;
    std::vector<ComponentFieldValue> fields;
    friend bool operator==(const ComponentPayload &, const ComponentPayload &) = default;
};
struct ComponentTypeDesc {
    ComponentTypeId id{};
    std::string name;
    std::uint32_t version = 1;
    std::vector<ComponentFieldDesc> fields;

    friend bool operator==(const ComponentTypeDesc &, const ComponentTypeDesc &) = default;
};

[[nodiscard]] ComponentValueKind component_value_kind(const ComponentScalar &) noexcept;
[[nodiscard]] bool valid_component_type(const ComponentTypeDesc &);
[[nodiscard]] bool valid_component_payload(const ComponentPayload &, const ComponentTypeDesc &);
bool read_component_fields(std::istream &, std::vector<ComponentFieldDesc> &);
void write_component_fields(std::ostream &, const std::vector<ComponentFieldDesc> &);
bool read_component_payload(std::istream &, ComponentPayload &);
void write_component_payload(std::ostream &, const ComponentPayload &);
[[nodiscard]] std::string serialize_component_type(const ComponentTypeDesc &);
[[nodiscard]] std::string serialize_component_payloads(const std::vector<ComponentPayload> &);
} // namespace vice::world
