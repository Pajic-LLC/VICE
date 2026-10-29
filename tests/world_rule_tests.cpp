#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <vice/tools/world_authoring.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>
namespace {
using namespace vice;
using namespace world;
void require(bool ok,const char *message){if(!ok){std::cerr<<"Rules: "<<message<<'\n';std::exit(1);}}
PersistentId id(unsigned n){PersistentId::Bytes b{};b[0]=0xca;b[6]=0x40;b[8]=0x80;for(unsigned i=0;i<4;++i)b[12+i]=static_cast<std::uint8_t>(n>>(i*8));return PersistentId{b};}
ComponentTypeId type(unsigned n){return {id(n)};}
ComponentPayload payload(std::int64_t n){return {type(101),1,{{1,n}}};}
WorldEventRequest fact(unsigned t=100){WorldEventRequest e;e.subject=id(1);e.data.payload={type(t),1,{}};return e;}
QuantityKey account(unsigned owner){return {id(owner),{id(104)}};}
WorldRule rule(unsigned n=200){WorldRule r;r.id=id(n);r.trigger_type=type(100);r.effect=RuleSetComponent{id(2),payload(7)};return r;}
struct Fixture {
    World w;
    Fixture(){for(unsigned i=1;i<=3;++i)require(w.create_entity(id(i)).has_value(),"entities");
        require(w.register_component_type({type(100),"Wake",1,{}})&&w.register_component_type({type(101),"State",1,{{1,ComponentValueKind::integer,"value"}}})&&w.register_component_type({type(102),"Then",1,{}}),"schemas");
        require(w.set_component(*w.find(id(1)),payload(4)),"initial state");
        require(w.register_quantity_type({{id(104)},"Units",1,1})&&w.create_quantity_account(account(1),100)==QuantityResult::success&&w.create_quantity_account(account(2),100)==QuantityResult::success,"accounts");
        QuantityTransaction seed;seed.operation=QuantityOperation::produce;seed.type={id(104)};seed.target=id(1);seed.amount=50;require(static_cast<bool>(w.transact_quantity(seed)),"seed");
    }
};
std::vector<std::byte> bytes(const World &w){const auto b=encode_world_snapshot(w.snapshot());if(!b)std::cerr<<b.diagnostics;require(static_cast<bool>(b),"encode");return b.bytes;}
EventRef wake(World &w){const auto e=w.emit_world_event(fact());require(static_cast<bool>(e),"wake event");return e.ref;}
void rehash(std::vector<std::byte> &b){std::uint64_t h=14695981039346656037ULL;for(std::size_t i=40;i<b.size();++i){h^=std::to_integer<unsigned char>(b[i]);h*=1099511628211ULL;}for(unsigned i=0;i<8;++i)b[32+i]=static_cast<std::byte>(h>>(8*i));}
}
int main(){
    {
        Fixture f;auto &w=f.w;auto r=rule();
        r.conditions={RuleEntityCondition{id(1)},RuleComponentCondition{id(1),type(101),1,1,RuleCompare::greater,std::int64_t{3}},RuleQuantityCondition{account(1),RuleCompare::greater_equal,25},RuleTimeCondition{10},RuleRelationCondition{{id(1),id(103),id(2)}}};
        require(w.add_relation(RelationKey{id(1),id(103),id(2)})&&w.add_rule(r)==RuleResult::success,"five condition operations");
        require(w.add_rule(r)==RuleResult::duplicate_id&&!w.unregister_component_type(type(100))&&!w.unregister_component_type(type(101)),"stable definition/type lifetime");
        require(w.dispatch_rules().evaluated==0&&w.rule(id(200))->decision.evaluations==0,"no per-tick full scan");
        const auto first=wake(w);const auto p=w.dispatch_rules();
        require(p.evaluated==1&&p.rejected==1&&w.rule(id(200))->decision.outcome==RuleOutcome::condition_false&&w.rule(id(200))->decision.condition==3&&!w.component(*w.find(id(2)),type(101)),"false time has no effect and explains index");
        wake(w);require(w.step_world_clock(10)&&w.component(*w.find(id(2)),type(101))==payload(7),"clock pumps pending rules after scheduler");
        auto decision=w.rule(id(200))->decision;require(decision.outcome==RuleOutcome::applied&&decision.evaluations==2&&decision.trigger.sequence>first.sequence,"persistent successful decision");
        require(w.remove_component(*w.find(id(1)),type(101)),"remove input");wake(w);w.dispatch_rules();
        require(w.rule(id(200))->decision.outcome==RuleOutcome::missing_state&&w.rule(id(200))->decision.condition==1,"missing state differs from false");
        require(w.destroy_entity(*w.find(id(3))),"unrelated entity deletion");
        auto broken=rule(201);std::get<RuleSetComponent>(broken.effect).entity=id(3);require(w.add_rule(broken)==RuleResult::success,"stale endpoint definition can be dormant");wake(w);w.dispatch_rules();
        require(w.rule(id(201))->decision.outcome==RuleOutcome::effect_rejected,"deleted endpoint cannot be replaced by a process handle");
        auto invalid=rule(202);invalid.version=2;require(w.add_rule(invalid)==RuleResult::invalid_argument,"unknown rule version rejected");
        invalid=rule(202);invalid.conditions={RuleComponentCondition{id(1),type(101),2,1,RuleCompare::equal,std::int64_t{4}}};require(w.add_rule(invalid)==RuleResult::invalid_argument,"schema version validated");
        invalid.conditions={RuleComponentCondition{id(1),type(101),1,1,RuleCompare::equal,true}};require(w.add_rule(invalid)==RuleResult::invalid_argument,"scalar type mismatch");
        invalid.conditions.assign(9,RuleTimeCondition{});require(w.add_rule(invalid)==RuleResult::invalid_argument,"bounded conditions");
        require(!w.snapshot(ComponentPayloadSnapshot::render).component_payloads_included&&w.snapshot(ComponentPayloadSnapshot::render).rules.empty(),"render projection excludes rules");
    }
    {
        Fixture f;auto &w=f.w;auto r=rule();QuantityTransaction t;t.type={id(104)};t.source=id(1);t.target=id(2);t.amount=25;r.effect=RuleQuantityEffect{t};
        require(w.add_rule(r)==RuleResult::success,"quantity effect");const auto cause=wake(w);wake(w);wake(w);
        require(w.rule_info().pending==1&&w.rule(id(200))->decision.coalesced==2,"explicit first-trigger coalescing with observable count");
        const auto saved=bytes(w);const auto decoded=decode_world_snapshot(saved);World next;require(decoded&&next.restore(*decoded.snapshot)&&bytes(next)==saved,"pending work/definitions deterministic binary restore");
        require(next.rule_info().pending==1&&next.quantity_account(account(1))->amount==50,"restore never auto executes");
        require(next.dispatch_rules(1).applied==1&&next.quantity_account(account(1))->amount==25&&next.quantity_account(account(2))->amount==25,"one atomic transfer after restore");
        const auto d=next.rule(id(200))->decision;const auto e=next.causal_event(d.output.sequence);require(e&&e->data&&e->data->parents==std::vector<EventRef>{cause},"protected receipt cites wake cause");
        wake(next);next.dispatch_rules();wake(next);const auto balance=next.quantity_account(account(2));const auto history=next.causal_info().next_sequence;next.dispatch_rules();
        require(next.rule(id(200))->decision.outcome==RuleOutcome::effect_rejected&&next.rule(id(200))->decision.detail==static_cast<unsigned>(QuantityResult::insufficient_quantity)&&next.quantity_account(account(2))==balance&&next.causal_info().next_sequence==history,"failed effect changes no balances or journal");
        const auto source=tools::world_cooker::serialize_source_manifest(next.snapshot());require(!source.empty(),"source serialization");const auto parsed=tools::world_cooker::parse_source_manifest(source);World source_world;require(parsed.snapshot&&source_world.restore(*parsed.snapshot)&&bytes(source_world)==bytes(next),"source record round trip");
        auto bad=next.snapshot();bad.rules.push_back(bad.rules[0]);const auto unchanged=bytes(next);require(!next.restore(bad)&&bytes(next)==unchanged,"duplicate restore transactional");
        bad=next.snapshot();bad.rules[0].decision.output.id=id(999);require(!next.restore(bad)&&bytes(next)==unchanged,"retained output identity corruption transactional");
        bad=next.snapshot();bad.rules[0].pending=EventRef{next.causal_info().id,id(999),next.causal_info().next_sequence};require(!next.restore(bad)&&bytes(next)==unchanged,"future pending identity invalid");
        require(next.set_rule_enabled(id(200),false)&&next.rule_info().pending==0,"disable cancels pending");wake(next);require(!next.rule_info().pending,"off fidelity does no work");
        require(next.set_rule_enabled(id(200),true),"reenable");wake(next);require(next.remove_rule(id(200))&&next.rule_info().pending==0,"removal clears queue");
    }
    {
        Fixture f;auto &w=f.w;auto a=rule(200);a.effect=RuleEmitEvent{fact(102)};auto b=rule(201);b.trigger_type=type(102);b.effect=RuleRelationEffect{{id(2),id(103),id(3)},true};
        require(w.add_rule(a)==RuleResult::success&&w.add_rule(b)==RuleResult::success,"chained rules");wake(w);
        const auto p=w.dispatch_rules();require(p.applied==1&&p.pending&&!w.has_relation({id(2),id(103),id(3)}),"new consequences wait for next pump frontier");
        require(w.dispatch_rules().applied==1&&w.has_relation({id(2),id(103),id(3)}),"second effect modifies authoritative relation");
        auto self=rule(202);self.effect=RuleEmitEvent{fact()};require(w.add_rule(self)==RuleResult::success,"self feedback");wake(w);
        require(w.dispatch_rules().evaluated<=3&&w.rule_info().pending>0,"feedback bounded without recursive callbacks");
        require(w.set_rule_enabled(id(202),false),"turn off feedback");w.dispatch_rules();w.dispatch_rules();
        auto schedule=rule(203);schedule.effect=RuleScheduleEvent{fact(102),100};require(w.add_rule(schedule)==RuleResult::success,"schedule consequence");const auto trigger=wake(w);w.dispatch_rules();
        const auto schedule_id=w.rule(id(203))->decision.schedule;require(!schedule_id.is_nil()&&w.schedule(schedule_id)->due_ticks==100&&w.schedule(schedule_id)->event.data.parents==std::vector<EventRef>{trigger},"future fact retains cause");
        const auto saved=decode_world_snapshot(bytes(w));World later;require(saved&&later.restore(*saved.snapshot)&&later.step_world_clock(100)&&!later.schedule(schedule_id),"future consequence survives checkpoint");
        const auto context=later.causal_context();require(std::any_of(context.events.begin(),context.events.end(),[&](const auto &e){return e.data&&e.data->schedule&&e.data->schedule->id==schedule_id;}),"scheduled event provenance persists");
    }
    {
        Fixture f;auto &w=f.w;auto r=rule();r.effect=RuleEmitEvent{fact(102)};require(w.add_rule(r)==RuleResult::success,"retention rule");wake(w);
        for(std::size_t i=0;i<causal_history_capacity+1;++i)require(static_cast<bool>(w.emit_world_event(fact(102))),"prune wake evidence");
        World next;const auto b=bytes(w);require(next.restore(*decode_world_snapshot(b).snapshot)&&next.dispatch_rules().rejected==1&&next.rule(id(200))->decision.outcome==RuleOutcome::trigger_pruned,"pruned wake is explicit failure after restore");
        auto forged=rule(201);auto e=fact();e.data.payload=quantity_receipt_schema().fields.empty()?ComponentPayload{}:ComponentPayload{quantity_receipt_type(),1,{}};forged.effect=RuleEmitEvent{e};require(w.add_rule(forged)==RuleResult::invalid_argument,"cannot forge protected receipt from rule");
        auto seeded=rule(201);seeded.pending=EventRef{};require(w.add_rule(seeded)==RuleResult::invalid_argument,"runtime definitions cannot inject pending or decisions");
    }
    {
        Fixture f;auto &w=f.w;auto r=rule();
        const auto before=bytes(w);tools::world_authoring::Transaction tx;tx.operations={tools::world_authoring::CreateRule{r},tools::world_authoring::RemoveRule{id(999)}};
        require(!tools::world_authoring::apply(w.snapshot(),tx)&&bytes(w)==before,"authoring transaction fails atomically");tx.operations.pop_back();const auto result=tools::world_authoring::apply(w.snapshot(),tx);require(result&&result.snapshot.rules.size()==1,"authoring creates rule");
        Fixture legacy;
        auto old = bytes(legacy.w);
        old.erase(old.end() - 34, old.end() - 26);
        old[8] = std::byte{21};
        old[9] = old[10] = old[11] = std::byte{0};
        rehash(old);
        require(static_cast<bool>(decode_world_snapshot(old)), "v21 backward read");
        auto malformed=bytes(legacy.w);for(unsigned i=0;i<4;++i)malformed[malformed.size()-30+i]=std::byte{255};rehash(malformed);require(!decode_world_snapshot(malformed),"unsafe rule count rejected before allocation");
        auto unknown=rule();std::ostringstream text;write_world_rule(text,unknown);std::istringstream in(text.str());WorldRule read;require(read_world_rule(in,read),"stable operation wire format");std::ostringstream round;write_world_rule(round,read);require(round.str()==text.str(),"deterministic record");
    }
    {
        Fixture f; auto &w=f.w;
        require(w.register_component_type({type(110), "Scalars", 1, {{1,ComponentValueKind::boolean,"flag"},{2,ComponentValueKind::real,"real"},{3,ComponentValueKind::text,"text"},{4,ComponentValueKind::identifier,"id"}}}), "scalar schema");
        require(w.set_component(*w.find(id(1)),{type(110),1,{{4,id(3)},{3,std::string{"text \"utf8 Ω\""}},{2,0.5},{1,true}}}), "scalar values");
        auto r=rule();r.conditions={RuleComponentCondition{id(1),type(110),1,1,RuleCompare::equal,true},RuleComponentCondition{id(1),type(110),1,2,RuleCompare::less,0.75},RuleComponentCondition{id(1),type(110),1,3,RuleCompare::equal,std::string{"text \"utf8 Ω\""}},RuleComponentCondition{id(1),type(110),1,4,RuleCompare::not_equal,id(2)}};
        require(w.add_rule(r)==RuleResult::success,"typed scalar comparisons");wake(w);require(w.dispatch_rules().applied==1,"boolean/real/text/identifier comparisons");
        auto invalid=rule(201);invalid.conditions={RuleComponentCondition{id(1),type(110),1,2,RuleCompare::equal,std::numeric_limits<double>::infinity()}};require(w.add_rule(invalid)==RuleResult::invalid_argument,"nonfinite constant rejected");
        invalid.conditions={RuleComponentCondition{id(1),type(110),1,1,RuleCompare::greater,true}};require(w.add_rule(invalid)==RuleResult::invalid_argument,"boolean ordering rejected");
        World restored;const auto encoded=bytes(w);require(restored.restore(*decode_world_snapshot(encoded).snapshot)&&bytes(restored)==encoded,"escaped scalar rule round trip");
        RegionDesc region;region.id={1};region.simulation_island={1};region.streaming_cell={1};region.origin={1e12,0,0};region.fidelity=RegionFidelity::inactive;
        require(w.register_region(region)&&w.set_spatial_state(*w.find(id(2)),{{1e12,0,0},{1}}),"inactive large coordinate rule fixture");wake(w);require(w.dispatch_rules().applied==1&&w.spatial_state(*w.find(id(2)))->global_position.x==1e12,"rules operate on inactive stored truth");
        auto delayed=rule(202);delayed.effect=RuleScheduleEvent{fact(102),std::numeric_limits<std::uint64_t>::max()};require(w.add_rule(delayed)==RuleResult::success&&w.step_world_clock(1),"overflow schedule rule");wake(w);const auto sequence=w.causal_info().next_sequence;w.dispatch_rules();require(w.rule(id(202))->decision.outcome==RuleOutcome::effect_rejected&&w.scheduler_info().count==0&&w.causal_info().next_sequence==sequence,"schedule overflow publishes no effect");
        require(w.remove_rule(id(200))&&w.remove_rule(id(202)),"remove scalar rules");
        auto quantity=rule(203);QuantityTransaction t;t.operation=QuantityOperation::consume;t.type={id(104)};t.source=id(1);t.amount=5;quantity.effect=RuleQuantityEffect{t};require(w.add_rule(quantity)==RuleResult::success,"consume effect");wake(w);require(w.dispatch_rules().applied==1&&w.quantity_account(account(1))->amount==45,"consume uses exact transaction");
        require(w.remove_rule(id(203)),"remove consume");quantity.id=id(204);t.operation=QuantityOperation::produce;t.source={};t.target=id(2);quantity.effect=RuleQuantityEffect{t};require(w.add_rule(quantity)==RuleResult::success,"produce effect");wake(w);require(w.dispatch_rules().applied==1&&w.quantity_account(account(2))->amount==5,"produce uses exact transaction");
        QuantityTypeDesc unused{{id(120)},"ReservedUnits",1,1};require(w.register_quantity_type(unused),"rule-only type");auto guard=rule(205);guard.conditions={RuleQuantityCondition{{id(1),unused.id},RuleCompare::greater_equal,0}};require(w.add_rule(guard)==RuleResult::success&&!w.remove_quantity_type(unused.id),"rule-only quantity definition lifetime");require(w.remove_rule(id(205))&&w.remove_quantity_type(unused.id),"type removable after rule");
    }
    {
        Fixture f;auto &w=f.w;
        auto a=rule(200),b=rule(201);a.trigger_subject=id(1);b.trigger_subject=id(1);b.effect=RuleSetComponent{id(2),payload(8)};
        require(w.add_rule(b)==RuleResult::success&&w.add_rule(a)==RuleResult::success,"reverse registration order");
        auto signal=fact();signal.retained=false;require(static_cast<bool>(w.emit_world_event(signal))&&w.rule_info().pending==0,"transient signals cannot wake persistent rules");
        auto other=fact();other.subject=id(3);require(static_cast<bool>(w.emit_world_event(other))&&w.rule_info().pending==0,"subject filter indexed wake eligibility");wake(w);
        require(w.dispatch_rules(1).applied==1&&w.component(*w.find(id(2)),type(101))==payload(7)&&w.rule_info().pending==1,"UUID byte tie-break independent of registration");
        require(w.dispatch_rules(1).applied==1&&w.component(*w.find(id(2)),type(101))==payload(8),"bounded backlog keeps deterministic order");
    }
    {
        // A complete cross-genre consequence chain, with a process-equivalent restore at each boundary.
        Fixture f;auto &w=f.w;const RelationKey initial{id(1),id(103),id(2)},changed{id(2),id(103),id(3)};
        require(w.add_relation(initial),"initial ownership-like generic relation");
        auto transfer=rule(200);transfer.conditions={RuleRelationCondition{initial},RuleComponentCondition{id(1),type(101),1,1,RuleCompare::greater,std::int64_t{3}}};
        QuantityTransaction t;t.type={id(104)};t.source=id(1);t.target=id(2);t.amount=25;transfer.effect=RuleQuantityEffect{t};
        auto relationship=rule(201);relationship.trigger_type=quantity_receipt_type();relationship.trigger_subject=id(1);relationship.conditions={RuleQuantityCondition{account(2),RuleCompare::greater_equal,25}};relationship.effect=RuleRelationEffect{changed,true};
        auto future=rule(202);future.trigger_type=quantity_receipt_type();future.trigger_subject=id(1);future.conditions={RuleRelationCondition{changed}};future.effect=RuleScheduleEvent{fact(102),10};
        auto consequence=rule(203);consequence.trigger_type=type(102);consequence.effect=RuleSetComponent{id(3),payload(99)};
        for(const auto &r:{transfer,relationship,future,consequence})require(w.add_rule(r)==RuleResult::success,"chain definitions");
        const auto cause=wake(w);require(w.dispatch_rules().applied==1&&w.rule_info().pending==2,"transfer queues dependent consequences");
        World middle;require(middle.restore(*decode_world_snapshot(bytes(w)).snapshot)&&middle.dispatch_rules().applied==2&&middle.has_relation(changed)&&middle.scheduler_info().count==1,"restore relation and future scheduling consequences");
        const auto scheduled=middle.rule(id(202))->decision.schedule;World end;require(end.restore(*decode_world_snapshot(bytes(middle)).snapshot)&&end.step_world_clock(10)&&end.component(*end.find(id(3)),type(101))==payload(99)&&!end.schedule(scheduled),"save close restore fires future consequence");
        const auto receipt=end.causal_event(end.rule(id(200))->decision.output.sequence);require(receipt&&receipt->data->parents==std::vector<EventRef>{cause}&&end.quantity_account(account(1))->amount==25&&end.quantity_account(account(2))->amount==25&&end.has_relation(initial)&&end.has_relation(changed),"identities balances relations and causes coherent");
        const auto final=bytes(end);World verified;require(verified.restore(*decode_world_snapshot(final).snapshot)&&bytes(verified)==final,"complete chain deterministic persistence");
    }
    {
        Fixture f;auto &w=f.w;const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<world_rule_capacity;++i)require(w.add_rule(rule(1000+i))==RuleResult::success,"stress registration");require(w.add_rule(rule(9999))==RuleResult::capacity_exceeded,"bounded storage");
        wake(w);require(w.rule_info().pending==world_rule_capacity,"subscriber index queues all matches");const auto saved=bytes(w);std::size_t evaluated=0,pumps=0;while(w.rule_info().pending){const auto d=w.dispatch_rules();require(d.evaluated<=world_rule_dispatch_budget,"dispatch cap");evaluated+=d.evaluated;++pumps;}
        require(evaluated==world_rule_capacity&&pumps==8&&w.dispatch_rules().evaluated==0,"budget/backlog exact");
        const auto page=w.rule_page({},64);require(page.entries.size()==64&&page.has_more&&w.rule_page(page.entries.back().id,64).entries.size()==64,"bounded ordered pagination");
        std::cout<<"Rules stress count="<<world_rule_capacity<<" snapshot_bytes="<<saved.size()<<" sizeof_rule="<<sizeof(WorldRule)<<" elapsed_us="<<std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count()<<'\n';
    }
    std::cout<<"Rule conditions, effects, bounded evaluation, persistence and failures passed\n";
}
