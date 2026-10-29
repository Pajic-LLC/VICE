# Declarative World rules

See [ADR 0031](../adr/0031-declarative-world-rules.md) for ordering, failure, persistence, fidelity, complexity and deferred work.

Register a `WorldRule` with a persistent UUID, version 1, a typed event schema, optional subject filter, up to eight AND conditions and one effect. A retained event wakes matching enabled rules. An indexed bounded pump evaluates World state and applies the effect through existing World APIs. There is no game-specific rule vocabulary.

```cpp
world::WorldRule rule;
rule.id = game_rule_id;
rule.trigger_type = game_event_type;
rule.trigger_subject = source_id;
rule.conditions = {
    world::RuleQuantityCondition{{source_id, units_type},
                                 world::RuleCompare::greater_equal, 25}
};
world::QuantityTransaction move;
move.type = units_type;
move.source = source_id;
move.target = target_id;
move.amount = 25;
rule.effect = world::RuleQuantityEffect{move};
const auto added = world.add_rule(rule);
// Publish the game's retained event, then explicitly dispatch or advance World time.
```

The first wakeup stays pending; repeated matching events before evaluation coalesce and increment a visible counter. Conditions read current World state. This format reconciles state on a wakeup; it does not deliver one transaction for every event. A failed effect consumes the wakeup and records its result. Each effect is atomic through its existing contract; separate rules are separate operations.

Condition tags 0–4: entity exists, component field comparison, relation exists, quantity comparison, World time >= tick. Effect tags 0–4: component upsert, retained event, quantity transaction, relation presence, one-shot scheduled event. Tags and rule versions are persisted. New tags must append; invalid tags/versions reject. An emitted event or quantity receipt cites its wakeup; scheduled output retains it through scheduler provenance. Latest component/relation results remain inspectable in rule decisions.

`rule`, `rule_page`, `rule_info`, `dispatch_rules`, `remove_rule` and `set_rule_enabled` expose state and control. The module SDK provides corresponding fixed-layout DTOs and size-checked callbacks. Read calls work in read-only frame/stop phases; mutation/dispatch uses normal lifecycle-thread write phases. Runtime entity inspection includes global rule counts and a bounded page; use SDK cursor pages for the complete catalog. CLI World inspection exposes source definitions/decisions. Revision-safe edits support `add_rule RECORD`, `remove_rule UUID`, `set_rule_enabled UUID 0|1`.

`write_world_rule`/`read_world_rule` define the stable text record used by `world_rule` source lines and snapshot v22. Native definitions start with empty decisions/pending state; trusted source/save reconstruction restores validated state. Full snapshot/restore/checkpoint and Editor clone/reset use the same World ownership. Restore never executes rules. Deleted endpoints fail safely, and pruned trigger evidence produces an explicit rejection.

The presentation example's scheduler emits game-defined PulseFact. One game-defined rule emits PulseObserved with the pulse as parent; a second stores the observed payload as persistent component state on the character. This runs alongside physical walking/driving, rigid-node animation and spatial audio through the same Project/Runtime path.

Current limits: 512 rules, eight conditions each, one pending wakeup each, 64 evaluations per pump. Feedback consequences wait for a later pump. No automatic condition polling, arbitrary executable scripting, multi-effect transactions, parameterized bindings or lossless per-event obligation queue are included.
