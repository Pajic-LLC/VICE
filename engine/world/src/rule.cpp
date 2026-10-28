#include <vice/world/world.hpp>
#include <algorithm>
#include <charconv>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
namespace vice::world {
namespace {
template<class T> bool number(std::istream &in,T &v) {
    std::string s;if(!(in>>s))return false;
    const auto r=std::from_chars(s.data(),s.data()+s.size(),v);
    return r.ec==std::errc{}&&r.ptr==s.data()+s.size();
}
bool identifier(std::istream &in,PersistentId &v) {
    std::string s;if(!(in>>s))return false;const auto id=PersistentId::parse(s);
    if(!id)return false;v=*id;return true;
}
bool flag(std::istream &in,bool &v){unsigned n=0;if(!number(in,n)||n>1)return false;v=n!=0;return true;}
bool ref(std::istream &in,EventRef &r){return identifier(in,r.history)&&identifier(in,r.id)&&number(in,r.sequence);}
void ref(std::ostream &out,const EventRef &r){out<<r.history.to_string()<<' '<<r.id.to_string()<<' '<<r.sequence;}
bool relation(std::istream &in,RelationKey &r){return identifier(in,r.subject)&&identifier(in,r.kind)&&identifier(in,r.object);}
void relation(std::ostream &out,const RelationKey &r){out<<r.subject.to_string()<<' '<<r.kind.to_string()<<' '<<r.object.to_string();}
bool event(std::istream &in,WorldEventRequest &e){return identifier(in,e.subject)&&identifier(in,e.target)&&identifier(in,e.object)&&number(in,e.region.value)&&read_world_event_data(in,e.data);}
void event(std::ostream &out,const WorldEventRequest &e){out<<e.subject.to_string()<<' '<<e.target.to_string()<<' '<<e.object.to_string()<<' '<<e.region.value<<' ';write_world_event_data(out,e.data);}
bool comparison(std::istream &in,RuleCompare &c){unsigned n=0;if(!number(in,n)||n>5)return false;c=static_cast<RuleCompare>(n);return true;}
template<class T> bool compare(const T &a,const T &b,RuleCompare c){
    switch(c){case RuleCompare::equal:return a==b;case RuleCompare::not_equal:return a!=b;
    case RuleCompare::less:return a<b;case RuleCompare::less_equal:return a<=b;
    case RuleCompare::greater:return a>b;case RuleCompare::greater_equal:return a>=b;}return false;
}
bool scalar_compare(const ComponentScalar &a,const ComponentScalar &b,RuleCompare c){
    if(a.index()!=b.index())return false;
    return std::visit([&](const auto &v){using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,PersistentId>)return c==RuleCompare::equal?v==std::get<T>(b):c==RuleCompare::not_equal&&v!=std::get<T>(b);
        else return compare(v,std::get<T>(b),c);},a);
}
void increment(std::uint64_t &v){if(v!=std::numeric_limits<std::uint64_t>::max())++v;}
bool valid_key(const RelationKey &k){return !k.subject.is_nil()&&!k.kind.is_nil()&&!k.object.is_nil();}
}
std::vector<PersistentId> rule_entities(const WorldRule &r) {
    std::vector<PersistentId> ids;
    const auto add = [&](PersistentId id) { if (!id.is_nil()) ids.push_back(id); };
    add(r.trigger_subject);
    for (const auto &condition : r.conditions) std::visit([&](const auto &c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, RuleEntityCondition> || std::is_same_v<T, RuleComponentCondition>) add(c.entity);
        else if constexpr (std::is_same_v<T, RuleRelationCondition>) { add(c.key.subject); add(c.key.object); }
        else if constexpr (std::is_same_v<T, RuleQuantityCondition>) add(c.key.holder);
    }, condition);
    std::visit([&](const auto &e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, RuleSetComponent>) add(e.entity);
        else if constexpr (std::is_same_v<T, RuleEmitEvent> || std::is_same_v<T, RuleScheduleEvent>) { add(e.event.subject); add(e.event.target); add(e.event.object); }
        else if constexpr (std::is_same_v<T, RuleQuantityEffect>) { add(e.transaction.source); add(e.transaction.target); }
        else { add(e.key.subject); add(e.key.object); }
    }, r.effect);
    std::sort(ids.begin(), ids.end(), [](auto a, auto b) { return a.bytes() < b.bytes(); });
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end()); return ids;
}
bool read_world_rule(std::istream &in,WorldRule &out){
    in.imbue(std::locale::classic());WorldRule r;std::size_t count=0;unsigned op=0;
    if(!identifier(in,r.id)||!number(in,r.version)||!flag(in,r.enabled)||!identifier(in,r.trigger_type.value)||!identifier(in,r.trigger_subject)||!number(in,count)||count>world_rule_condition_limit)return false;
    for(std::size_t i=0;i<count;++i){
        if(!number(in,op)||op>4)return false;
        switch(op){
        case 0:{RuleEntityCondition c;if(!identifier(in,c.entity))return false;r.conditions.push_back(c);break;}
        case 1:{RuleComponentCondition c;ComponentPayload p;
            if(!identifier(in,c.entity)||!comparison(in,c.comparison)||!read_component_payload(in,p)||p.fields.size()!=1)return false;
            c.type=p.type;c.version=p.version;c.field=p.fields[0].id;c.value=p.fields[0].value;r.conditions.push_back(std::move(c));break;}
        case 2:{RuleRelationCondition c;if(!relation(in,c.key))return false;r.conditions.push_back(c);break;}
        case 3:{RuleQuantityCondition c;if(!identifier(in,c.key.holder)||!identifier(in,c.key.type.value)||!comparison(in,c.comparison)||!number(in,c.amount))return false;r.conditions.push_back(c);break;}
        case 4:{RuleTimeCondition c;if(!number(in,c.not_before))return false;r.conditions.push_back(c);break;}
        }
    }
    if(!number(in,op)||op>4)return false;
    switch(op){
    case 0:{RuleSetComponent e;if(!identifier(in,e.entity)||!read_component_payload(in,e.payload))return false;r.effect=std::move(e);break;}
    case 1:{RuleEmitEvent e;if(!event(in,e.event))return false;r.effect=std::move(e);break;}
    case 2:{RuleQuantityEffect e;unsigned mode=0;auto &t=e.transaction;
        if(!number(in,mode)||mode>2||!identifier(in,t.type.value)||!identifier(in,t.source)||!identifier(in,t.target)||!number(in,t.amount))return false;
        t.operation=static_cast<QuantityOperation>(mode);r.effect=e;break;}
    case 3:{RuleRelationEffect e;if(!relation(in,e.key)||!flag(in,e.present))return false;r.effect=e;break;}
    case 4:{RuleScheduleEvent e;if(!number(in,e.delay_ticks)||!event(in,e.event))return false;r.effect=std::move(e);break;}
    }
    bool pending=false;if(!flag(in,pending))return false;
    if(pending){EventRef p;if(!ref(in,p))return false;r.pending=p;}
    auto &d=r.decision;
    if(!number(in,op)||op>5||!number(in,d.condition)||!number(in,d.detail)||!number(in,d.evaluations)||!number(in,d.coalesced)||!ref(in,d.trigger)||!ref(in,d.output)||!identifier(in,d.schedule))return false;
    d.outcome=static_cast<RuleOutcome>(op);out=std::move(r);return true;
}
void write_world_rule(std::ostream &out,const WorldRule &r){
    out.imbue(std::locale::classic());out<<r.id.to_string()<<' '<<r.version<<' '<<r.enabled<<' '<<r.trigger_type.value.to_string()<<' '<<r.trigger_subject.to_string()<<' '<<r.conditions.size();
    for(const auto &c:r.conditions){out<<' '<<c.index()<<' ';std::visit([&](const auto &v){using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,RuleEntityCondition>)out<<v.entity.to_string();
        else if constexpr(std::is_same_v<T,RuleComponentCondition>){out<<v.entity.to_string()<<' '<<static_cast<unsigned>(v.comparison)<<' ';write_component_payload(out,{v.type,v.version,{{v.field,v.value}}});}
        else if constexpr(std::is_same_v<T,RuleRelationCondition>)relation(out,v.key);
        else if constexpr(std::is_same_v<T,RuleQuantityCondition>)out<<v.key.holder.to_string()<<' '<<v.key.type.value.to_string()<<' '<<static_cast<unsigned>(v.comparison)<<' '<<v.amount;
        else out<<v.not_before;
    },c);}
    out<<' '<<r.effect.index()<<' ';std::visit([&](const auto &v){using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,RuleSetComponent>){out<<v.entity.to_string()<<' ';write_component_payload(out,v.payload);}
        else if constexpr(std::is_same_v<T,RuleEmitEvent>)event(out,v.event);
        else if constexpr(std::is_same_v<T,RuleQuantityEffect>){const auto &t=v.transaction;out<<static_cast<unsigned>(t.operation)<<' '<<t.type.value.to_string()<<' '<<t.source.to_string()<<' '<<t.target.to_string()<<' '<<t.amount;}
        else if constexpr(std::is_same_v<T,RuleRelationEffect>){relation(out,v.key);out<<' '<<v.present;}
        else {out<<v.delay_ticks<<' ';event(out,v.event);}
    },r.effect);
    out<<' '<<r.pending.has_value();if(r.pending){out<<' ';ref(out,*r.pending);}
    const auto &d=r.decision;out<<' '<<static_cast<unsigned>(d.outcome)<<' '<<d.condition<<' '<<d.detail<<' '<<d.evaluations<<' '<<d.coalesced<<' ';ref(out,d.trigger);out<<' ';ref(out,d.output);out<<' '<<d.schedule.to_string();
}
bool World::valid_rule(const WorldRule &r,bool restoring)const{
    if(r.id.is_nil()||r.version!=1||!component_type(r.trigger_type)||r.conditions.size()>world_rule_condition_limit)return false;
    const auto valid_payload=[&](const ComponentPayload &p){const auto t=component_type(p.type);return t&&p.type!=quantity_receipt_type()&&valid_component_payload(p,*t);};
    const auto valid_event=[&](const WorldEventRequest &e){return !e.subject.is_nil()&&e.retained&&e.data.parents.empty()&&!e.data.schedule&&valid_payload(e.data.payload);};
    for(const auto &c:r.conditions)if(!std::visit([&](const auto &v)->bool{using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,RuleEntityCondition>)return !v.entity.is_nil();
        else if constexpr(std::is_same_v<T,RuleComponentCondition>){
            if(v.entity.is_nil()||static_cast<unsigned>(v.comparison)>5)return false;
            auto t=component_type(v.type);if(!t||t->version!=v.version)return false;
            const auto f=std::find_if(t->fields.begin(),t->fields.end(),[&](const auto &x){return x.id==v.field;});
            if(f==t->fields.end()||f->kind!=component_value_kind(v.value))return false;
            if(v.comparison!=RuleCompare::equal&&v.comparison!=RuleCompare::not_equal&&f->kind!=ComponentValueKind::integer&&f->kind!=ComponentValueKind::real)return false;
            t->fields={*f};return valid_component_payload({v.type,v.version,{{v.field,v.value}}},*t);
        }else if constexpr(std::is_same_v<T,RuleRelationCondition>)return valid_key(v.key);
        else if constexpr(std::is_same_v<T,RuleQuantityCondition>)return !v.key.holder.is_nil()&&quantity_type(v.key.type).has_value()&&v.amount>=0&&static_cast<unsigned>(v.comparison)<=5;
        else return true;
    },c))return false;
    if(!std::visit([&](const auto &e)->bool{using T=std::decay_t<decltype(e)>;
        if constexpr(std::is_same_v<T,RuleSetComponent>)return !e.entity.is_nil()&&valid_payload(e.payload);
        else if constexpr(std::is_same_v<T,RuleEmitEvent>||std::is_same_v<T,RuleScheduleEvent>)return valid_event(e.event);
        else if constexpr(std::is_same_v<T,RuleQuantityEffect>){const auto &t=e.transaction;const auto op=static_cast<unsigned>(t.operation);return op<=2&&t.amount>0&&quantity_type(t.type).has_value()&&t.parents.empty()&&
            (op==0?(t.source.is_nil()&&!t.target.is_nil()):op==1?(!t.source.is_nil()&&t.target.is_nil()):(!t.source.is_nil()&&!t.target.is_nil()&&t.source!=t.target));}
        else return valid_key(e.key);
    },r.effect))return false;
    const auto &d=r.decision;
    if(static_cast<unsigned>(d.outcome)>5||d.condition>r.conditions.size()||(!r.enabled&&r.pending))return false;
    const auto valid_ref=[&](EventRef p,bool empty){
        if(!p.sequence)return empty&&p.id.is_nil()&&p.history.is_nil();
        if(p.id.is_nil()||p.history!=causal_id_||p.sequence>=next_causal_sequence_)return false;
        const auto e=causal_event(p.sequence);return !e||e->id==p.id;
    };
    if(!restoring)return !r.pending&&d.outcome==RuleOutcome::never_evaluated&&!d.evaluations&&!d.coalesced&&valid_ref(d.trigger,true)&&!d.trigger.sequence&&valid_ref(d.output,true)&&!d.output.sequence&&d.schedule.is_nil()&&!d.detail&&!d.condition;
    if(!valid_ref(d.trigger,true)||!valid_ref(d.output,true))return false;
    if((d.outcome==RuleOutcome::never_evaluated)!=(!d.evaluations)||(!d.evaluations&&d.trigger.sequence)|| (d.evaluations&&!d.trigger.sequence))return false;
    if (!d.evaluations && (d.output.sequence || !d.schedule.is_nil() || d.condition || d.detail)) return false;
    if ((d.outcome == RuleOutcome::condition_false || d.outcome == RuleOutcome::missing_state) && d.condition >= r.conditions.size()) return false;
    if (d.outcome != RuleOutcome::applied && (d.output.sequence || !d.schedule.is_nil())) return false;
    if (d.output.sequence && d.output.sequence <= d.trigger.sequence) return false;
    if (d.outcome == RuleOutcome::applied) {
        const bool emits = std::holds_alternative<RuleEmitEvent>(r.effect) || std::holds_alternative<RuleQuantityEffect>(r.effect);
        const bool schedules = std::holds_alternative<RuleScheduleEvent>(r.effect);
        if (emits != (d.output.sequence != 0) || schedules != !d.schedule.is_nil()) return false;
    }
    if (d.trigger.sequence) {
        const auto e = causal_event(d.trigger.sequence);
        if (e && (!e->data || e->data->payload.type != r.trigger_type || (!r.trigger_subject.is_nil() && r.trigger_subject != e->subject))) return false;
    }
    if(r.pending){if(!valid_ref(*r.pending,false))return false;const auto e=causal_event(r.pending->sequence);if(e&&(!e->data||e->data->payload.type!=r.trigger_type||(!r.trigger_subject.is_nil()&&r.trigger_subject!=e->subject)))return false;}
    if (d.outcome != RuleOutcome::effect_rejected && d.detail) return false;
    if (d.outcome == RuleOutcome::effect_rejected) {
        if (std::holds_alternative<RuleEmitEvent>(r.effect) && d.detail > static_cast<unsigned>(WorldEventResult::sequence_exhausted)) return false;
        if (std::holds_alternative<RuleQuantityEffect>(r.effect) && d.detail > static_cast<unsigned>(QuantityResult::event_rejected)) return false;
        if (std::holds_alternative<RuleScheduleEvent>(r.effect) && d.detail > static_cast<unsigned>(ScheduleResult::event_rejected)) return false;
    }
    return true;
}
RuleResult World::install_rule(WorldRule r,bool restoring){
    if(!valid_rule(r,restoring))return RuleResult::invalid_argument;
    if(rules_.contains(r.id))return RuleResult::duplicate_id;
    if(rules_.size()>=world_rule_capacity)return RuleResult::capacity_exceeded;
    // Canonicalize payload field order via the same stable wire form on every path.
    if(auto *e=std::get_if<RuleSetComponent>(&r.effect))std::sort(e->payload.fields.begin(),e->payload.fields.end(),[](const auto &a,const auto &b){return a.id<b.id;});
    std::visit([](auto &e){using T=std::decay_t<decltype(e)>;if constexpr(std::is_same_v<T,RuleEmitEvent>||std::is_same_v<T,RuleScheduleEvent>)std::sort(e.event.data.payload.fields.begin(),e.event.data.payload.fields.end(),[](const auto &a,const auto &b){return a.id<b.id;});},r.effect);
    if(r.enabled)rule_subscribers_[r.trigger_type.value].insert(r.id);
    if(r.pending)rule_pending_.insert({r.pending->sequence,r.id});
    rules_.emplace(r.id,std::move(r));return RuleResult::success;
}
RuleResult World::add_rule(WorldRule r){return install_rule(std::move(r),false);}
bool World::remove_rule(PersistentId id){
    const auto i=rules_.find(id);if(i==rules_.end())return false;
    auto s=rule_subscribers_.find(i->second.trigger_type.value);if(s!=rule_subscribers_.end()){s->second.erase(id);if(s->second.empty())rule_subscribers_.erase(s);}
    if(i->second.pending)rule_pending_.erase({i->second.pending->sequence,id});rules_.erase(i);return true;
}
bool World::set_rule_enabled(PersistentId id,bool enabled){
    const auto i=rules_.find(id);if(i==rules_.end())return false;
    if(i->second.enabled==enabled)return true;
    auto &r=i->second;r.enabled=enabled;
    if(enabled)rule_subscribers_[r.trigger_type.value].insert(id);
    else {auto s=rule_subscribers_.find(r.trigger_type.value);if(s!=rule_subscribers_.end()){s->second.erase(id);if(s->second.empty())rule_subscribers_.erase(s);}if(r.pending){rule_pending_.erase({r.pending->sequence,id});r.pending.reset();}}
    return true;
}
std::optional<WorldRule> World::rule(PersistentId id)const{const auto i=rules_.find(id);return i==rules_.end()?std::nullopt:std::optional{i->second};}
RuleInfo World::rule_info()const noexcept{return {rules_.size(),rule_enabled_count(),rule_pending_.size()};}
std::size_t World::rule_enabled_count()const noexcept {std::size_t n=0;for(const auto &[id,s]:rule_subscribers_){(void)id;n+=s.size();}return n;}
RulePage World::rule_page(PersistentId after,std::size_t limit)const{
    RulePage p;limit=std::clamp(limit,std::size_t{1},world_rule_query_limit);
    for(auto i=rules_.upper_bound(after);i!=rules_.end();++i){if(p.entries.size()==limit){p.has_more=true;break;}p.entries.push_back(i->second);}return p;
}
void World::queue_rules(ComponentTypeId type,PersistentId subject,EventRef trigger){
    const auto s=rule_subscribers_.find(type.value);if(s==rule_subscribers_.end())return;
    for(auto id:s->second){auto &r=rules_.at(id);if(!r.trigger_subject.is_nil()&&r.trigger_subject!=subject)continue;
        if(r.pending)increment(r.decision.coalesced);else {r.pending=trigger;rule_pending_.insert({trigger.sequence,id});}}
}
bool World::rules_use_component(ComponentTypeId type)const{
    for(const auto &[id,r]:rules_){(void)id;if(r.trigger_type==type)return true;
        for(const auto &c:r.conditions)if(const auto *p=std::get_if<RuleComponentCondition>(&c);p&&p->type==type)return true;
        if(std::visit([&](const auto &e){using T=std::decay_t<decltype(e)>;if constexpr(std::is_same_v<T,RuleSetComponent>)return e.payload.type==type;else if constexpr(std::is_same_v<T,RuleEmitEvent>||std::is_same_v<T,RuleScheduleEvent>)return e.event.data.payload.type==type;else return false;},r.effect))return true;
    }return false;
}
bool World::rules_use_quantity(QuantityTypeId type)const{
    for(const auto &[id,r]:rules_){(void)id;for(const auto &c:r.conditions)if(const auto *p=std::get_if<RuleQuantityCondition>(&c);p&&p->key.type==type)return true;
        if(const auto *p=std::get_if<RuleQuantityEffect>(&r.effect);p&&p->transaction.type==type)return true;}return false;
}
RuleDispatch World::dispatch_rules(std::size_t budget){
    RuleDispatch result;budget=std::min(budget,world_rule_dispatch_budget);
    const auto frontier=next_causal_sequence_-1;
    while(result.evaluated<budget&&!rule_pending_.empty()&&rule_pending_.begin()->first<=frontier){
        const auto key=*rule_pending_.begin();rule_pending_.erase(rule_pending_.begin());auto &r=rules_.at(key.second);
        auto &d=r.decision;const auto trigger=*r.pending;r.pending.reset();
        d.trigger=trigger;d.output={};d.schedule={};d.condition=0;d.detail=0;increment(d.evaluations);++result.evaluated;
        const auto fact=causal_event(trigger.sequence);
        if(!fact||fact->id!=trigger.id){d.outcome=RuleOutcome::trigger_pruned;++result.rejected;continue;}
        d.outcome=RuleOutcome::applied;
        for(std::size_t i=0;i<r.conditions.size();++i){bool missing=false;
            const bool ok=std::visit([&](const auto &c)->bool{using T=std::decay_t<decltype(c)>;
                if constexpr(std::is_same_v<T,RuleEntityCondition>)return find(c.entity).has_value();
                else if constexpr(std::is_same_v<T,RuleComponentCondition>){const auto e=find(c.entity);const auto p=e?component(*e,c.type):std::nullopt;if(!p||p->version!=c.version){missing=true;return false;}
                    const auto f=std::find_if(p->fields.begin(),p->fields.end(),[&](const auto &v){return v.id==c.field;});if(f==p->fields.end()){missing=true;return false;}return scalar_compare(f->value,c.value,c.comparison);}
                else if constexpr(std::is_same_v<T,RuleRelationCondition>)return has_relation(c.key);
                else if constexpr(std::is_same_v<T,RuleQuantityCondition>){const auto a=quantity_account(c.key);if(!a){missing=true;return false;}return compare(a->amount,c.amount,c.comparison);}
                else return clock_.ticks>=c.not_before;
            },r.conditions[i]);
            if(!ok){d.outcome=missing?RuleOutcome::missing_state:RuleOutcome::condition_false;d.condition=static_cast<std::uint32_t>(i);break;}
        }
        if(d.outcome!=RuleOutcome::applied){++result.rejected;continue;}
        const bool applied=std::visit([&](const auto &e)->bool{using T=std::decay_t<decltype(e)>;
            if constexpr(std::is_same_v<T,RuleSetComponent>){const auto entity=find(e.entity);return entity&&set_component(*entity,e.payload);}
            else if constexpr(std::is_same_v<T,RuleEmitEvent>){auto request=e.event;request.data.parents={trigger};const auto emitted=emit_world_event(std::move(request));d.detail=static_cast<std::uint32_t>(emitted.result);d.output=emitted.ref;return static_cast<bool>(emitted);}
            else if constexpr(std::is_same_v<T,RuleQuantityEffect>){auto t=e.transaction;t.parents={trigger};const auto receipt=transact_quantity(t);d.detail=static_cast<std::uint32_t>(receipt.result);d.output=receipt.event;return static_cast<bool>(receipt);}
            else if constexpr(std::is_same_v<T,RuleRelationEffect>){if(!find(e.key.subject)||!find(e.key.object))return false;const bool exists=has_relation(e.key);return exists==e.present||(e.present?add_relation(e.key):remove_relation(e.key));}
            else {if(e.delay_ticks>std::numeric_limits<std::uint64_t>::max()-clock_.ticks){d.detail=static_cast<unsigned>(ScheduleResult::invalid_argument);return false;}
                auto request=e.event;request.data.parents={trigger};const auto scheduled=schedule_event(std::move(request),clock_.ticks+e.delay_ticks);d.detail=static_cast<std::uint32_t>(scheduled.result);d.schedule=scheduled.id;return static_cast<bool>(scheduled);}
        },r.effect);
        if(applied)++result.applied;else {d.outcome=RuleOutcome::effect_rejected;++result.rejected;}
    }
    result.pending=!rule_pending_.empty();return result;
}
const char *rule_outcome_name(RuleOutcome o)noexcept{
    switch(o){case RuleOutcome::never_evaluated:return "never_evaluated";case RuleOutcome::applied:return "applied";case RuleOutcome::condition_false:return "condition_false";case RuleOutcome::missing_state:return "missing_state";case RuleOutcome::trigger_pruned:return "trigger_pruned";case RuleOutcome::effect_rejected:return "effect_rejected";}return "invalid";
}
std::string serialize_world_rule(const WorldRule &r){
    std::ostringstream definition;write_world_rule(definition,r);
    // The full stable record is escaped as a JSON string, alongside structured decision fields.
    std::string escaped;for(const char c:definition.str()){if(c=='"'||c=='\\')escaped+='\\';escaped+=c;}
    std::ostringstream conditions; conditions.imbue(std::locale::classic()); conditions << '[';
    for (std::size_t i = 0; i < r.conditions.size(); ++i) {
        if (i) conditions << ',';
        conditions << "{\"operation\":" << r.conditions[i].index();
        std::visit([&](const auto &c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, RuleEntityCondition>) conditions << ",\"entity\":\"" << c.entity.to_string() << '\"';
            else if constexpr (std::is_same_v<T, RuleComponentCondition>) conditions << ",\"entity\":\"" << c.entity.to_string() << "\",\"comparison\":" << static_cast<unsigned>(c.comparison) << ",\"expected\":" << serialize_component_payloads({{c.type, c.version, {{c.field, c.value}}}});
            else if constexpr (std::is_same_v<T, RuleRelationCondition>) conditions << ",\"subject\":\"" << c.key.subject.to_string() << "\",\"kind\":\"" << c.key.kind.to_string() << "\",\"object\":\"" << c.key.object.to_string() << '\"';
            else if constexpr (std::is_same_v<T, RuleQuantityCondition>) conditions << ",\"holder\":\"" << c.key.holder.to_string() << "\",\"type\":\"" << c.key.type.value.to_string() << "\",\"comparison\":" << static_cast<unsigned>(c.comparison) << ",\"amount\":\"" << c.amount << '\"';
            else conditions << ",\"not_before_ticks\":\"" << c.not_before << '\"';
        }, r.conditions[i]);
        conditions << '}';
    }
    conditions << ']';
    const auto &d=r.decision;std::ostringstream out;out.imbue(std::locale::classic());
    out<<"{\"id\":\""<<r.id.to_string()<<"\",\"version\":"<<r.version<<",\"enabled\":"<<(r.enabled?"true":"false")<<",\"trigger_type\":\""<<r.trigger_type.value.to_string()<<"\",\"trigger_subject\":\""<<r.trigger_subject.to_string()<<"\",\"condition_count\":"<<r.conditions.size()<<",\"conditions\":"<<conditions.str()<<",\"effect_operation\":"<<r.effect.index()<<",\"pending_sequence\":\""<<(r.pending?r.pending->sequence:0)<<"\",\"outcome\":\""<<rule_outcome_name(d.outcome)<<"\",\"condition_index\":"<<d.condition<<",\"detail\":"<<d.detail<<",\"evaluations\":\""<<d.evaluations<<"\",\"coalesced\":\""<<d.coalesced<<"\",\"trigger_sequence\":\""<<d.trigger.sequence<<"\",\"output_sequence\":\""<<d.output.sequence<<"\",\"schedule\":\""<<d.schedule.to_string()<<"\",\"record\":\""<<escaped<<"\"}";return out.str();
}
std::string serialize_rule_page(const RulePage &p){std::string out="{\"has_more\":";out+=p.has_more?"true":"false";out+=",\"entries\":[";for(std::size_t i=0;i<p.entries.size();++i){if(i)out+=',';out+=serialize_world_rule(p.entries[i]);}return out+"]}";}
} // namespace vice::world
