#include <vice/world/world.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/runtime/module_loader.hpp>
#include <vice/runtime/debug.hpp>
#if defined(VICE_CAUSAL_PHYSICS)
#include <vice/rigid_body/authored_simulation.hpp>
#include <vice/jobs/jobs.hpp>
#endif
#if defined(VICE_CAUSAL_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
namespace {
using namespace vice;
void require(bool ok,const char* message){if(!ok){std::cerr<<"Causal history: "<<message<<'\n';std::exit(1);}}
world::PersistentId id(unsigned n){char text[37];std::snprintf(text,sizeof(text),"9c000000-0000-4000-8000-%012x",n);return *world::PersistentId::parse(text);}
world::World load(){
 const auto parsed=tools::world_cooker::parse_source_manifest("vice-world-source 1\nregion 1 10 1000000000000 0 0 0 0 0 full none\n"
 "entity 9c000000-0000-4000-8000-000000000001 spatial 1000000000000 5 0 1 rigid_box static 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 discrete solid\n"
 "entity 9c000000-0000-4000-8000-000000000002 spatial 1000000000000 3 0 1 rigid_box dynamic 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 discrete solid\n"
 "entity 9c000000-0000-4000-8000-000000000003 none distance_link 9c000000-0000-4000-8000-000000000001 9c000000-0000-4000-8000-000000000002 2 2 0 0 1000 intact 0 0\n"
 "entity 9c000000-0000-4000-8000-000000000004 none dependency none none enabled\n"
 "entity 9c000000-0000-4000-8000-000000000005 none dependency 9c000000-0000-4000-8000-000000000004 9c000000-0000-4000-8000-000000000003 enabled\n"
 "entity 9c000000-0000-4000-8000-000000000006 none dependency 9c000000-0000-4000-8000-000000000005 none enabled\n"
 "entity 9c000000-0000-4000-8000-000000000007 none dependency 9c000000-0000-4000-8000-000000000004 none enabled\n");
 require(parsed.snapshot.has_value(),"parse source");world::World w;require(w.restore(*parsed.snapshot),"restore source");return w;
}
void verify_chain(const world::World& w){
 const auto a=w.causal_event(2),b=w.causal_event(3),c=w.causal_event(4);
 require(a && b && c && a->kind==world::CausalKind::constraint_broken && a->origin==world::CausalOrigin::physics && a->subject==id(3) && a->quantity>1000 && a->solver_step>0,"typed physical root");
 require(b->subject==id(5) && c->subject==id(6) && b->parent_id==a->id && c->parent_id==b->id && b->parent_sequence==2 && c->parent_sequence==3,"physical-to-harness-to-lamp causal parentage");
 require(a->capture_id==b->capture_id && b->capture_id==c->capture_id && w.dependency_state(*w.find(id(7)))->available,"one capture and independent supply");
 require(world::validate_causal_history(w.causal_snapshot()),"generated history validates");
 world::World derived;
 require(derived.restore(w.snapshot()) &&
             derived.register_component_type({{id(100)}, "PhysicsConsequence", 1, {}}),
         "typed consequence schema");
 world::WorldEventRequest consequence;
 consequence.subject = id(2);
 consequence.data.payload = {{id(100)}, 1, {}};
 consequence.data.parents = {{w.causal_info().id, a->id, a->sequence},
                             {w.causal_info().id, b->id, b->sequence}};
 const auto fact = derived.emit_world_event(consequence);
 require(static_cast<bool>(fact), "gameplay fact cites actual physical and propagated causes");
 const auto evidence = derived.causal_context(id(2));
 require(
     std::any_of(evidence.events.begin(), evidence.events.end(),
                 [](const auto &e) { return e.kind == world::CausalKind::constraint_broken; }) &&
         derived.causal_event(fact.ref.sequence)->data->parents.size() == 2,
     "multi-parent inspection traverses physics evidence");
}
void rehash(std::vector<std::byte>& bytes){std::uint64_t h=14695981039346656037ULL;for(std::size_t i=40;i<bytes.size();++i){h^=std::to_integer<std::uint8_t>(bytes[i]);h*=1099511628211ULL;}for(unsigned i=0;i<8;++i)bytes[32+i]=static_cast<std::byte>((h>>(8U*i))&255U);}
}
int main(){
 auto w=load();require(w.causal_info().id.is_nil() && w.causal_info().retained_count==0,"authoring does not invent runtime history");
#if defined(VICE_CAUSAL_PHYSICS)
 jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count=1}};rigid_body::AuthoredSimulation physics;require(physics.reset(w,jobs),"start physical capture");
#endif
 auto module=runtime::load_game_module(VICE_CAUSAL_TEST_MODULE);require(static_cast<bool>(module),"load real SDK journal module");project::Manifest manifest;
#if defined(VICE_CAUSAL_PHYSICS)
 runtime::GameModuleLifecycle lifecycle{*module.module,{manifest,w,&physics}};
#else
 runtime::GameModuleLifecycle lifecycle{*module.module,{manifest,w}};
#endif
 require(lifecycle.start(),"SDK metadata, namespace, bounds and thread guards");
 const auto capture=w.causal_info();require(w.begin_causal_recording() && w.causal_info().next_sequence==capture.next_sequence,"idempotent capture start");
 require(!w.publish_distance_link_break(*w.find(id(3)),999,1) && w.causal_info().next_sequence==2,"invalid measured break cannot mutate history");
#if defined(VICE_CAUSAL_PHYSICS)
 lifecycle.event(platform::Event{platform::KeyEvent{.key=platform::Key::space,.pressed=true}});require(physics.step(),"native break publishes complete chain");
#else
 require(w.publish_distance_link_break(*w.find(id(3)),1200,7),"publish supplied backend evidence without loading solver");
#endif
 verify_chain(w);render::Color color;lifecycle.frame(color);require(color.red==1 && color.blue==1,"SDK follows typed parent identities");
 lifecycle.event(platform::Event{platform::KeyEvent{.key=platform::Key::left,.pressed=true}});require(!w.dependency_state(*w.find(id(7)))->available,"live supply command creates independent outage");
 lifecycle.event(platform::Event{platform::KeyEvent{.key=platform::Key::right,.pressed=true}});require(w.dependency_state(*w.find(id(7)))->available,"live supply command restores independent device");
 lifecycle.frame(color);require(color.red==1 && color.green==1 && color.blue==1,"historical failure remains after later state changes");verify_chain(w);
 const auto before=w.causal_info().next_sequence;require(w.set_dependency_enabled(*w.find(id(4)),true) && w.causal_info().next_sequence==before,"no-op controls produce no spam");
 const auto saved=w.snapshot();const auto encoded=world::encode_world_snapshot(saved);require(static_cast<bool>(encoded),"save typed history");
 const auto decoded=world::decode_world_snapshot(encoded.bytes);require(decoded.snapshot && decoded.snapshot->causal_history.events==saved.causal_history.events,"cooked event identities and parentage round trip");
 const auto text=tools::world_cooker::serialize_source_manifest(saved);const auto reparsed=tools::world_cooker::parse_source_manifest(text);
 require(reparsed.snapshot && reparsed.snapshot->causal_history.events==saved.causal_history.events && tools::world_cooker::serialize_source_manifest(*reparsed.snapshot)==text,"canonical source preserves history");
 world::World resumed;require(resumed.restore(*decoded.snapshot) && !resumed.causal_info().capturing,"restore does not fabricate replayed events");
 require(resumed.begin_causal_recording() && resumed.causal_info().id==capture.id && resumed.causal_info().capture_id!=capture.capture_id && resumed.causal_info().next_sequence==before+1,"resume keeps lineage and sequence, starts a new segment");verify_chain(resumed);
 world::World fork;require(fork.restore(saved) && fork.begin_causal_recording(),"fork saved history");
 require(fork.causal_info().capture_id!=resumed.causal_info().capture_id && fork.causal_event(2)->id==resumed.causal_event(2)->id,"forks preserve past IDs and distinguish new capture IDs");
 require(fork.set_dependency_enabled(*fork.find(id(4)),false) && resumed.set_dependency_enabled(*resumed.find(id(4)),false),"divergent continuation");
 require(fork.causal_info().next_sequence==resumed.causal_info().next_sequence && fork.causal_event(before+1)->id!=resumed.causal_event(before+1)->id,"sequence ordinals do not alias fork event identities");
 auto bad=saved;bad.causal_history.events.back().parent_id=world::PersistentId::random();require(!w.restore(bad) && w.causal_info().next_sequence==before,"invalid parent restore is atomic");
 bad=saved;bad.causal_history.events.back().parent_sequence=bad.causal_history.events.back().sequence;require(!world::encode_world_snapshot(bad),"future/self parents rejected");
 bad=saved;bad.causal_history.events[1].origin=world::CausalOrigin::authoring;require(!world::validate_causal_history(bad.causal_history),"physical event provenance must match its kind");
 bad=saved;bad.causal_history.events[1].quantity=std::numeric_limits<float>::infinity();require(!world::validate_causal_history(bad.causal_history),"nonfinite evidence rejected");
 bad=saved;bad.causal_history.events.back().capture_id=world::PersistentId::random();require(!world::validate_causal_history(bad.causal_history),"missing unpruned capture marker rejected");
 world::World deleted;require(deleted.restore(saved) && deleted.begin_causal_recording() && deleted.clear_dependency(*deleted.find(id(6))) && deleted.clear_dependency(*deleted.find(id(5))) && deleted.clear_distance_link(*deleted.find(id(3))) && deleted.destroy_entity(*deleted.find(id(3))),"historical identities do not block explicit deletion");
 const auto debug=runtime::sample_runtime_debug_entity(deleted,id(3));require(debug.state==runtime::RuntimeDebugEntityState::not_found && std::any_of(debug.causal_history.events.begin(),debug.causal_history.events.end(),[](const auto& e){return e.kind==world::CausalKind::constraint_broken;}),"debug can explain a deleted historical identity");
 const auto limited=w.causal_context(id(6),8,1);require(limited.truncated && limited.events.size()==1,"query budget truncation is explicit");
 for(unsigned i=0;i<600;++i)require(w.set_dependency_enabled(*w.find(id(4)),i%2!=0),"bounded recording under repeated supply changes");
 const auto info=w.causal_info();require(info.retained_count==world::causal_history_capacity && info.first_sequence>1 && !w.causal_event(1) && world::validate_causal_history(w.causal_snapshot()),"bounded retention and pruned range");
 // A single wide propagation can prune its own producer before the last consumer.
 // Parent UUIDs must remain valid evidence even when those records are no longer retained.
 world::World wide;const auto supply_id=world::PersistentId::random();
 const auto supply=*wide.create_entity(supply_id);require(wide.set_dependency(supply,{}),"wide source");
 for(std::size_t i=0;i<world::causal_history_capacity+16;++i) {
     const auto consumer=wide.create_entity();
     require(wide.set_dependency(consumer,world::Dependency{.provider=supply_id}),"wide consumer");
 }
 require(wide.begin_causal_recording() && wide.set_dependency_enabled(supply,false),"wide propagation");
 const auto wide_history=wide.causal_snapshot();
 require(wide_history.events.size()==world::causal_history_capacity && world::validate_causal_history(wide_history),"wide propagation stays bounded and validates");
 for(const auto& event:wide_history.events) require(event.kind==world::CausalKind::dependency_state_changed &&
     event.parent_sequence<wide.causal_info().first_sequence && !event.parent_id.is_nil(),"pruned producer keeps its stable UUID throughout propagation");
 const auto first=*w.causal_event(info.first_sequence);require(first.parent_sequence<info.first_sequence && first.parent_sequence>0,"retention can prune an ancestor");
 const world::CausalContext slice{info,{first},true};require(world::serialize_causal_context(slice).find("pruned")!=std::string::npos,"missing parent is reported as pruned");
 lifecycle.event(platform::Event{platform::KeyEvent{.key=platform::Key::up,.pressed=true}});lifecycle.frame(color);require(color.red==1,"SDK distinguishes pruned event from future/mismatched event");lifecycle.stop();
 auto exhausted=load().snapshot();auto marker=saved.causal_history.events.front();marker.sequence=std::numeric_limits<std::uint64_t>::max()-2;
 exhausted.causal_history={world::PersistentId::random(),std::numeric_limits<std::uint64_t>::max()-1,{marker}};
 world::World full;require(full.restore(exhausted) && full.begin_causal_recording(),"sequence boundary fixture");const auto full_before=full.causal_snapshot();
 require(!full.set_dependency_enabled(*full.find(id(4)),false) && full.dependency(*full.find(id(4)))->enabled && full.causal_snapshot().events==full_before.events,"sequence exhaustion rejects before mutation");
#if defined(VICE_CAUSAL_PHYSICS)
 auto rejected=load().snapshot();rejected.entities[2].distance_link->break_threshold=0.001F;
 marker.sequence=std::numeric_limits<std::uint64_t>::max()-3;rejected.causal_history={world::PersistentId::random(),std::numeric_limits<std::uint64_t>::max()-2,{marker}};
 world::World fail;require(fail.restore(rejected),"failed publication fixture");rigid_body::AuthoredSimulation failed;require(failed.reset(fail,jobs) && !failed.step(),"event exhaustion stops solver publication");
 const auto failed_step=failed.coordinator().step_index();require(!failed.step() && failed.coordinator().step_index()==failed_step,"failed publication latches until reset");
#endif
 world::World legacy;
 require(legacy.create_entity(id(1)).has_value(), "legacy fixture");
 auto bytes = world::encode_world_snapshot(legacy.snapshot()).bytes;
 require(bytes.size() == 144, "minimal current format");
 bytes.resize(bytes.size() - 74U);
 bytes.pop_back();
 bytes[8] = std::byte{10};
 rehash(bytes);
 const auto old = world::decode_world_snapshot(bytes);
 require(old.snapshot && old.snapshot->causal_history.events.empty(),
         "v10 remains readable without invented history");
#if defined(VICE_CAUSAL_EDITOR)
 auto authored=load();auto weak=*authored.distance_link(*authored.find(id(3)));weak.break_threshold=0.001F;require(authored.set_distance_link(*authored.find(id(3)),weak),"preview threshold");
 editor::AssetIndex assets;editor::PreviewRuntime preview;std::string diagnostics;const editor::PreviewGameConfig config{manifest,{}};
 require(preview.reset(authored,jobs,assets,&diagnostics,&config) && preview.step_once() && preview.world()->causal_info().next_sequence==5 && authored.causal_info().id.is_nil(),"preview produces same physical chain without touching authoring");
#endif
 return 0;
}
