// Included inside module_loader.cpp's private namespace, after shared value codecs.
std::optional<world::WorldRule> decode_rule(const module::RuleDefinition *input) {
    if (!input || input->size < sizeof(*input) || input->enabled > 1 || input->condition_count > world::world_rule_condition_limit) return {};
    world::WorldRule r;
    r.id = persistent_id(input->id); r.version = input->version; r.enabled = input->enabled != 0;
    r.trigger_type = {persistent_id(input->trigger_type)}; r.trigger_subject = persistent_id(input->trigger_subject);
    for (std::uint32_t i = 0; i < input->condition_count; ++i) {
        const auto &c = input->conditions[i];
        switch (c.operation) {
        case module::RuleConditionOperation::entity_exists: r.conditions.push_back(world::RuleEntityCondition{persistent_id(c.entity)}); break;
        case module::RuleConditionOperation::component_field: {
            module::ComponentData data; data.type=c.type;data.version=c.version;data.field_count=1;data.fields[0]=c.field;
            const auto p=decode_component_data(&data);if(!p)return {};
            r.conditions.push_back(world::RuleComponentCondition{persistent_id(c.entity),p->type,p->version,p->fields[0].id,static_cast<world::RuleCompare>(c.comparison),p->fields[0].value});break;
        }
        case module::RuleConditionOperation::relation_exists: r.conditions.push_back(world::RuleRelationCondition{{persistent_id(c.relation.subject),persistent_id(c.relation.kind),persistent_id(c.relation.object)}});break;
        case module::RuleConditionOperation::quantity: r.conditions.push_back(world::RuleQuantityCondition{quantity_key(c.quantity),static_cast<world::RuleCompare>(c.comparison),c.amount});break;
        case module::RuleConditionOperation::time_at_least: r.conditions.push_back(world::RuleTimeCondition{c.not_before_ticks});break;
        default:return {};
        }
    }
    const auto &e=input->effect;
    switch(e.operation){
    case module::RuleEffectOperation::set_component:{const auto p=decode_component_data(&e.component);if(!p)return {};r.effect=world::RuleSetComponent{persistent_id(e.entity),*p};break;}
    case module::RuleEffectOperation::emit_event:{const auto p=decode_event_request(&e.event);if(!p)return {};r.effect=world::RuleEmitEvent{*p};break;}
    case module::RuleEffectOperation::quantity:{const auto &q=e.quantity;if(q.size<sizeof(q)||q.parent_count)return {};world::QuantityTransaction t;
        t.type={persistent_id(q.type)};t.source=persistent_id(q.source);t.target=persistent_id(q.target);t.operation=static_cast<world::QuantityOperation>(q.operation);t.amount=q.amount;r.effect=world::RuleQuantityEffect{t};break;}
    case module::RuleEffectOperation::relation:if(e.relation_present>1)return {};r.effect=world::RuleRelationEffect{{persistent_id(e.relation.subject),persistent_id(e.relation.kind),persistent_id(e.relation.object)},e.relation_present!=0};break;
    case module::RuleEffectOperation::schedule_event:{const auto p=decode_event_request(&e.event);if(!p)return {};r.effect=world::RuleScheduleEvent{*p,e.delay_ticks};break;}
    default:return {};
    }
    return r;
}
module::RuleDefinition encode_rule(const world::WorldRule &r){
    module::RuleDefinition out;out.id=module_id(r.id);out.version=r.version;out.enabled=r.enabled?1U:0U;
    out.trigger_type=module_id(r.trigger_type.value);out.trigger_subject=module_id(r.trigger_subject);out.condition_count=static_cast<std::uint32_t>(r.conditions.size());
    for(std::size_t i=0;i<r.conditions.size();++i){auto &c=out.conditions[i];c.operation=static_cast<module::RuleConditionOperation>(r.conditions[i].index());
        std::visit([&](const auto &v){using T=std::decay_t<decltype(v)>;
            if constexpr(std::is_same_v<T,world::RuleEntityCondition>)c.entity=module_id(v.entity);
            else if constexpr(std::is_same_v<T,world::RuleComponentCondition>){c.entity=module_id(v.entity);c.type=module_id(v.type.value);c.version=v.version;c.comparison=static_cast<module::RuleCompare>(v.comparison);c.field=encode_component_data({v.type,v.version,{{v.field,v.value}}}).fields[0];}
            else if constexpr(std::is_same_v<T,world::RuleRelationCondition>)c.relation={module_id(v.key.subject),module_id(v.key.kind),module_id(v.key.object)};
            else if constexpr(std::is_same_v<T,world::RuleQuantityCondition>){c.quantity={module_id(v.key.holder),module_id(v.key.type.value)};c.comparison=static_cast<module::RuleCompare>(v.comparison);c.amount=v.amount;}
            else c.not_before_ticks=v.not_before;
        },r.conditions[i]);
    }
    auto &e=out.effect;e.operation=static_cast<module::RuleEffectOperation>(r.effect.index());
    std::visit([&](const auto &v){using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,world::RuleSetComponent>){e.entity=module_id(v.entity);e.component=encode_component_data(v.payload);}
        else if constexpr(std::is_same_v<T,world::RuleEmitEvent>)e.event=encode_event_request(v.event);
        else if constexpr(std::is_same_v<T,world::RuleQuantityEffect>){e.quantity.operation=static_cast<module::QuantityOperation>(v.transaction.operation);e.quantity.type=module_id(v.transaction.type.value);e.quantity.source=module_id(v.transaction.source);e.quantity.target=module_id(v.transaction.target);e.quantity.amount=v.transaction.amount;}
        else if constexpr(std::is_same_v<T,world::RuleRelationEffect>){e.relation={module_id(v.key.subject),module_id(v.key.kind),module_id(v.key.object)};e.relation_present=v.present?1U:0U;}
        else {e.event=encode_event_request(v.event);e.delay_ticks=v.delay_ticks;}
    },r.effect);return out;
}
module::RuleSummary encode_rule_summary(const world::WorldRule &r){
    module::RuleSummary s;s.id=module_id(r.id);s.trigger_type=module_id(r.trigger_type.value);s.trigger_subject=module_id(r.trigger_subject);s.enabled=r.enabled?1U:0U;s.condition_count=static_cast<std::uint32_t>(r.conditions.size());s.effect_operation=static_cast<std::uint32_t>(r.effect.index());
    const auto &d=r.decision;s.condition_index=d.condition;s.detail=d.detail;s.outcome=static_cast<module::RuleOutcome>(d.outcome);s.evaluations=d.evaluations;s.coalesced=d.coalesced;s.trigger=encode_event_ref(d.trigger);s.output=encode_event_ref(d.output);s.schedule=module_id(d.schedule);if(r.pending)s.pending=encode_event_ref(*r.pending);return s;
}
module::Result host_add_rule(void *value,const module::RuleDefinition *input)noexcept{
    const auto *b=current_bridge(value);if(!b||!b->context||!b->writable)return module::Result::wrong_phase;
    const auto r=decode_rule(input);if(!r)return module::Result::invalid_argument;
    switch(b->context->world.add_rule(*r)){
    case world::RuleResult::success:return module::Result::success;
    case world::RuleResult::capacity_exceeded:return module::Result::capacity_exceeded;
    case world::RuleResult::duplicate_id:return module::Result::rule_exists;
    default:return module::Result::invalid_argument;
    }
}
module::Result host_remove_rule(void *value,module::EntityId id)noexcept{
    const auto *b=current_bridge(value);if(!b||!b->context||!b->writable)return module::Result::wrong_phase;
    if(persistent_id(id).is_nil())return module::Result::invalid_argument;
    return b->context->world.remove_rule(persistent_id(id))?module::Result::success:module::Result::not_found;
}
module::Result host_set_rule_enabled(void *value,module::EntityId id,std::uint32_t enabled)noexcept{
    const auto *b=current_bridge(value);if(!b||!b->context||!b->writable)return module::Result::wrong_phase;
    if(enabled>1||persistent_id(id).is_nil())return module::Result::invalid_argument;
    return b->context->world.set_rule_enabled(persistent_id(id),enabled!=0)?module::Result::success:module::Result::not_found;
}
module::Result host_read_rule(void *value,module::EntityId id,module::RuleSnapshot *output)noexcept{
    const auto *b=current_bridge(value);if(!b||!b->context)return module::Result::wrong_phase;
    if(!output||output->size<sizeof(*output)||persistent_id(id).is_nil())return module::Result::invalid_argument;
    const auto r=b->context->world.rule(persistent_id(id));if(!r)return module::Result::not_found;
    module::RuleSnapshot s;s.definition=encode_rule(*r);s.summary=encode_rule_summary(*r);*output=s;return module::Result::success;
}
module::Result host_read_rules(void *value,module::RulePageSnapshot *output)noexcept{
    const auto *b=current_bridge(value);if(!b||!b->context)return module::Result::wrong_phase;
    if(!output||output->size<sizeof(*output))return module::Result::invalid_argument;
    const auto p=b->context->world.rule_page(persistent_id(output->after));module::RulePageSnapshot s;s.after=output->after;s.count=static_cast<std::uint32_t>(p.entries.size());s.has_more=p.has_more?1U:0U;
    for(std::size_t i=0;i<p.entries.size();++i)s.entries[i]=encode_rule_summary(p.entries[i]);if(!p.entries.empty())s.after=s.entries[p.entries.size()-1].id;*output=s;return module::Result::success;
}
module::Result host_read_rule_info(void *value,module::RuleInfoSnapshot *output)noexcept{
    const auto *b=current_bridge(value);if(!b||!b->context)return module::Result::wrong_phase;
    if(!output||output->size<sizeof(*output))return module::Result::invalid_argument;
    const auto p=b->context->world.rule_info();module::RuleInfoSnapshot s;s.count=static_cast<std::uint32_t>(p.count);s.enabled=static_cast<std::uint32_t>(p.enabled);s.pending=static_cast<std::uint32_t>(p.pending);*output=s;return module::Result::success;
}
module::Result host_dispatch_rules(void *value,std::uint32_t budget,module::RuleDispatchSnapshot *output)noexcept{
    const auto *b=current_bridge(value);if(!b||!b->context||!b->writable)return module::Result::wrong_phase;
    if(!output||output->size<sizeof(*output)||!budget||budget>world::world_rule_dispatch_budget)return module::Result::invalid_argument;
    const auto p=b->context->world.dispatch_rules(budget);module::RuleDispatchSnapshot s;s.evaluated=static_cast<std::uint32_t>(p.evaluated);s.applied=static_cast<std::uint32_t>(p.applied);s.rejected=static_cast<std::uint32_t>(p.rejected);s.pending=p.pending?1U:0U;*output=s;return module::Result::success;
}
