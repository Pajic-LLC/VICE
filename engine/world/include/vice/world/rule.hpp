#pragma once
#include <vice/world/quantity.hpp>
#include <vice/world/relation.hpp>
#include <vice/world/schedule.hpp>
#include <variant>
namespace vice::world {
inline constexpr std::size_t world_rule_capacity = 512, world_rule_condition_limit = 8,
                             world_rule_dispatch_budget = 64, world_rule_query_limit = 64;
// Persistent operation tags: append only, never reuse.
enum class RuleCompare : std::uint32_t { equal, not_equal, less, less_equal, greater, greater_equal };
struct RuleEntityCondition { PersistentId entity{}; };
struct RuleComponentCondition {
    PersistentId entity{};
    ComponentTypeId type;
    std::uint32_t version = 1, field = 0;
    RuleCompare comparison = RuleCompare::equal;
    ComponentScalar value = false;
};
struct RuleRelationCondition { RelationKey key; };
struct RuleQuantityCondition {
    QuantityKey key;
    RuleCompare comparison = RuleCompare::greater_equal;
    std::int64_t amount = 0;
};
struct RuleTimeCondition { std::uint64_t not_before = 0; };
using RuleCondition = std::variant<RuleEntityCondition, RuleComponentCondition,
                                   RuleRelationCondition, RuleQuantityCondition, RuleTimeCondition>;
struct RuleSetComponent { PersistentId entity{}; ComponentPayload payload; };
struct RuleEmitEvent { WorldEventRequest event; };
struct RuleQuantityEffect { QuantityTransaction transaction; };
struct RuleRelationEffect { RelationKey key; bool present = true; };
struct RuleScheduleEvent { WorldEventRequest event; std::uint64_t delay_ticks = 0; };
using RuleEffect = std::variant<RuleSetComponent, RuleEmitEvent, RuleQuantityEffect,
                                RuleRelationEffect, RuleScheduleEvent>;
enum class RuleOutcome : std::uint32_t {
    never_evaluated, applied, condition_false, missing_state, trigger_pruned, effect_rejected
};
struct RuleDecision {
    RuleOutcome outcome = RuleOutcome::never_evaluated;
    std::uint32_t condition = 0, detail = 0;
    std::uint64_t evaluations = 0, coalesced = 0;
    EventRef trigger, output;
    PersistentId schedule{};
};
struct WorldRule {
    PersistentId id{};
    std::uint32_t version = 1;
    bool enabled = true;
    ComponentTypeId trigger_type;
    PersistentId trigger_subject{}; // nil matches any subject
    std::vector<RuleCondition> conditions;
    RuleEffect effect;
    std::optional<EventRef> pending;
    RuleDecision decision;
};
enum class RuleResult : std::uint32_t { success, invalid_argument, duplicate_id, capacity_exceeded, not_found };
struct RuleDispatch { std::size_t evaluated = 0, applied = 0, rejected = 0; bool pending = false; };
struct RulePage { std::vector<WorldRule> entries; bool has_more = false; };
struct RuleInfo { std::size_t count = 0, enabled = 0, pending = 0; };
[[nodiscard]] std::vector<PersistentId> rule_entities(const WorldRule &);
bool read_world_rule(std::istream &, WorldRule &);
void write_world_rule(std::ostream &, const WorldRule &);
[[nodiscard]] const char *rule_outcome_name(RuleOutcome) noexcept;
[[nodiscard]] std::string serialize_world_rule(const WorldRule &);
[[nodiscard]] std::string serialize_rule_page(const RulePage &);
} // namespace vice::world
