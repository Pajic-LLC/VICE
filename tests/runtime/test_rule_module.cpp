#include <vice/runtime/module_api.hpp>
#include <cstring>
#include <thread>
namespace {
using namespace vice::runtime::module;
constexpr EntityId id(unsigned n){EntityId id{};id.bytes[0]=0xa2;id.bytes[6]=0x40;id.bytes[8]=0x80;id.bytes[15]=static_cast<std::uint8_t>(n);return id;}
class RuleGame final:public Game {
    bool valid_=true;
    RuleDefinition rule_;
public:
    bool on_start(Context &c)override {
        ComponentSchema wake;wake.type=id(94);std::strcpy(wake.name,"TestWake");
        ComponentSchema state;state.type=id(95);std::strcpy(state.name,"TestState");state.field_count=1;state.fields[0].id=1;state.fields[0].kind=ComponentKind::integer;std::strcpy(state.fields[0].name,"value");
        valid_ &= c.register_component_schema(wake)==Result::success&&c.register_component_schema(state)==Result::success;
        rule_.id=id(96);rule_.trigger_type=wake.type;rule_.trigger_subject=id(2);rule_.condition_count=2;
        rule_.conditions[0].operation=RuleConditionOperation::entity_exists;rule_.conditions[0].entity=id(3);
        rule_.conditions[1].operation=RuleConditionOperation::component_field;rule_.conditions[1].entity=id(2);rule_.conditions[1].type=state.type;rule_.conditions[1].field.id=1;rule_.conditions[1].field.kind=ComponentKind::integer;rule_.conditions[1].field.integer=3;rule_.conditions[1].comparison=RuleCompare::greater;
        rule_.effect.operation=RuleEffectOperation::set_component;rule_.effect.entity=id(3);rule_.effect.component.type=state.type;rule_.effect.component.field_count=1;rule_.effect.component.fields[0].id=1;rule_.effect.component.fields[0].kind=ComponentKind::integer;rule_.effect.component.fields[0].integer=9;
        RuleSnapshot existing;
        const auto read=c.read_rule(rule_.id,existing);
        if(read==Result::not_found){auto initial=rule_.effect.component;initial.fields[0].integer=4;valid_ &= c.set_component(id(2),initial)==Result::success&&c.add_rule(rule_)==Result::success;}
        else valid_ &= read==Result::success;
        valid_ &= c.add_rule(rule_)==Result::rule_exists;
        auto bad=rule_;bad.id=id(97);bad.size=0;valid_ &= c.add_rule(bad)==Result::invalid_argument;
        bad=rule_;bad.id=id(97);bad.condition_count=9;valid_ &= c.add_rule(bad)==Result::invalid_argument;
        bad=rule_;bad.id=id(97);bad.version=2;valid_ &= c.add_rule(bad)==Result::invalid_argument;
        RulePageSnapshot page;valid_ &= c.read_rules(page)==Result::success&&page.count==1&&c.read_rules(page)==Result::success&&page.count==0;
        RuleInfoSnapshot info;valid_ &= c.read_rule_info(info)==Result::success&&info.count==1;
        RuleDispatchSnapshot dispatch;dispatch.evaluated=123;valid_ &= c.dispatch_rules(65,dispatch)==Result::invalid_argument&&dispatch.evaluated==123;
        valid_ &= c.set_rule_enabled(rule_.id,2)==Result::invalid_argument&&c.remove_rule(id(99))==Result::not_found;
        Result other=Result::success;std::thread thread{[&]{RuleSnapshot s;other=c.read_rule(rule_.id,s);}};thread.join();valid_ &= other==Result::wrong_phase;
        HostApi old;old.size=static_cast<std::uint32_t>(offsetof(HostApi,add_rule));Context legacy{&old};valid_ &= legacy.add_rule(rule_)==Result::unavailable&&legacy.read_rule(rule_.id,existing)==Result::unavailable;
        WorldEventRequest request;request.subject=id(2);request.payload.type=wake.type;EventRef ref;valid_ &= c.emit_world_event(request,ref)==Result::success;
        return valid_;
    }
    void on_event(Context &c,const Event &event)override {
        if(event.kind==EventKind::key&&(event.flags&1U)){RuleDispatchSnapshot d;valid_ &= c.dispatch_rules(64,d)==Result::success&&d.applied==1;}
    }
    void on_frame(Context &c,Frame &frame)override {
        RuleSnapshot state;RuleDispatchSnapshot d;
        valid_ &= c.read_rule(rule_.id,state)==Result::success&&state.summary.outcome==RuleOutcome::applied&&state.definition.conditions[1].field.integer==3&&state.definition.effect.component.fields[0].integer==9;
        valid_ &= c.add_rule(rule_)==Result::wrong_phase&&c.remove_rule(rule_.id)==Result::wrong_phase&&c.set_rule_enabled(rule_.id,0)==Result::wrong_phase&&c.dispatch_rules(1,d)==Result::wrong_phase;
        frame.clear_color={valid_?1.0F:0.0F,1,1,1};
    }
};
}
VICE_GAME_MODULE(RuleGame)
