#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <vice/world/component.hpp>
#include <vice/world/entity.hpp>
#include <vice/world/persistent_id.hpp>

namespace vice::world {

// Persistent edge identity. No process-local handles are stored in this key.
struct RelationKey {
    PersistentId subject{}, kind{}, object{};
    [[nodiscard]] bool valid() const noexcept {
        return !subject.is_nil() && !kind.is_nil() && !object.is_nil();
    }
    friend bool operator==(const RelationKey &, const RelationKey &) = default;
    friend bool operator<(const RelationKey &a, const RelationKey &b) noexcept {
        if (a.subject != b.subject)
            return a.subject.bytes() < b.subject.bytes();
        if (a.kind != b.kind)
            return a.kind.bytes() < b.kind.bytes();
        return a.object.bytes() < b.object.bytes();
    }
};
struct RelationKeyHash {
    std::size_t operator()(const RelationKey &k) const noexcept {
        auto h = PersistentIdHash{}(k.subject);
        for (const auto p : {k.kind, k.object})
            h ^= PersistentIdHash{}(p) + 0x9e3779b9U + (h << 6U) + (h >> 2U);
        return h;
    }
};
enum class RelationDirection : std::uint32_t { outgoing = 1, incoming = 2, both = 3 };
inline constexpr std::size_t max_world_relations = 8'000'000;
inline constexpr std::size_t max_world_relation_payloads = 65536;
struct RelationPageSlice {
    std::vector<RelationKey> keys;
    std::optional<RelationKey> after;
    bool has_more = false;
};
struct RelationState {
    RelationKey key;
    std::vector<ComponentPayload> components;
    friend bool operator==(const RelationState &, const RelationState &) = default;
};
[[nodiscard]] std::string serialize_relation_states(const std::vector<RelationState> &);

struct Relation {
    EntityId subject;
    PersistentId kind;
    EntityId object;

    friend bool operator==(const Relation &, const Relation &) noexcept = default;
};

} // namespace vice::world
