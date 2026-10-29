#include <vice/world/world.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/tools/world_authoring.hpp>
#include <vice/tools/world_edit.hpp>
#include <vice/runtime/render_streaming.hpp>
#include <vice/runtime/module_loader.hpp>
#include <vice/runtime/debug.hpp>
#if defined(VICE_MATERIAL_PHYSICS)
#include <vice/rigid_body/authored_simulation.hpp>
#include <vice/jobs/jobs.hpp>
#endif
#if defined(VICE_MATERIAL_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
namespace {
using namespace vice;
void require(bool ok,const char* message){if(!ok){std::cerr<<"Shared material: "<<message<<'\n';std::exit(1);}}
world::PersistentId id(unsigned n){char text[37];std::snprintf(text,sizeof(text),"9e000000-0000-4000-8000-%012x",n);return *world::PersistentId::parse(text);}
world::World load(){std::ifstream input{VICE_MATERIAL_SOURCE};std::ostringstream source;source<<input.rdbuf();const auto parsed=tools::world_cooker::parse_source_manifest(source.str());if(!parsed.snapshot)std::cerr<<parsed.diagnostics;require(parsed.snapshot.has_value(),"parse material example");world::World w;require(w.restore(*parsed.snapshot),"restore material example");return w;}
void rehash(std::vector<std::byte>& bytes){std::uint64_t hash=14695981039346656037ULL;for(std::size_t i=40;i<bytes.size();++i){hash^=std::to_integer<std::uint8_t>(bytes[i]);hash*=1099511628211ULL;}for(std::size_t i=0;i<8;++i)bytes[32+i]=static_cast<std::byte>((hash>>(8*i))&255);}
}
int main(){using namespace vice;
 auto w=load();const auto ice=*w.find(id(2)),grip=*w.find(id(3));
 require(w.snapshot().materials.size()==4 && w.material_surface(ice)->mass==250 && w.material_surface(grip)->mass==2000,"shared densities produce different masses");
 require(w.rigid_box(ice)->friction==1 && w.material_surface(ice)->friction==0 && w.rigid_box(grip)->friction==0 && w.material_surface(grip)->friction==1,"shared surfaces override legacy inline properties");
 world::World recycled;const auto temporary=recycled.create_entity();auto definition=*w.material(id(0x102));
 require(recycled.set_material(definition) && recycled.set_entity_material(temporary,definition.id),"assign recyclable slot");
 const auto removed_revision=recycled.material_revision();require(recycled.destroy_entity(temporary) && recycled.material_revision()>removed_revision && recycled.remove_material(definition.id),"destroy releases material reference and revision");
 require(recycled.entity_material(recycled.create_entity()).is_nil(),"recycled slot has no stale material");
 auto oversized=w.snapshot();oversized.materials.resize(world::max_world_materials+1);require(!world::encode_world_snapshot(oversized),"catalog capacity enforced before encode");
 auto bad=w.snapshot();bad.materials.push_back(bad.materials[0]);require(!w.restore(bad),"duplicate catalog identity rejected atomically");
 bad=w.snapshot();bad.entities[0].material=id(999);require(!world::encode_world_snapshot(bad),"unknown material rejected");
 auto invalid=*w.material(id(0x102));invalid.density=std::numeric_limits<float>::infinity();require(!w.set_material(invalid),"nonfinite density rejected");
 invalid=*w.material(id(0x102));invalid.restitution=1.01F;require(!w.set_material(invalid),"superelastic shared material rejected");
 invalid=*w.material(id(0x102));invalid.render_material={id(0x301)};require(!w.set_material(invalid),"missing render resource rejected");
 const auto resource=*w.create_resource({id(0x301)});require(w.set_material(invalid),"shared render resource binding");
 require(w.resource_reference(ice,world::ResourceRole::render_material)==world::ResourceId{id(0x301)},"World resolves shared visual identity");
 auto resident=runtime::build_runtime_render_resident_scene(w);require(bool(resident),"build resident scene");
 const auto visual=std::find_if(resident.scene.meshes.begin(),resident.scene.meshes.end(),[&](const auto& e){return e.entity==id(2);});
 require(visual!=resident.scene.meshes.end() && visual->material==world::ResourceId{id(0x301)},"production render residency uses shared visual identity");
 const auto legacy_instances=runtime::collect_render_mesh_instances(w);const auto legacy_visual=std::find_if(legacy_instances.begin(),legacy_instances.end(),[&](const auto& e){return e.entity==id(2);});
 require(legacy_visual!=legacy_instances.end() && legacy_visual->material==world::ResourceId{id(0x301)},"direct render path uses same visual identity");
 require(!w.destroy_resource(resource) && !w.remove_material(id(0x102)),"live material and visual resources protected");
 const auto other=*w.create_resource({id(0x302)});(void)other;
 require(!w.set_resource_reference(ice,{world::ResourceRole::render_material,{id(0x302)}}),"conflicting explicit visual binding rejected");
 require(w.set_resource_reference(ice,{world::ResourceRole::render_material,{id(0x301)}}),"matching explicit binding accepted");
 auto render_conflict=*w.material(id(0x102));render_conflict.render_material={id(0x302)};const auto revision=w.material_revision();
 require(!w.set_material(render_conflict) && w.material_revision()==revision,"catalog conflict rolls back");
 require(w.clear_resource_reference(ice,world::ResourceRole::render_material),"clear redundant explicit binding");
 invalid.render_material={};require(w.set_material(invalid) && w.destroy_resource(resource),"releasing catalog reference permits resource deletion");
 const auto transform=w.authored_transform(ice);auto scale=*transform;scale.scale={2,3,4};require(w.set_authored_transform(ice,scale),"scale test box");
 require(w.material_surface(ice)->volume==24 && w.material_surface(ice)->mass==6000,"density uses scaled collider volume");
 require(w.set_authored_transform(ice,*transform),"restore scale");
#if defined(VICE_MATERIAL_PHYSICS)
 {
     jobs::Scheduler scale_jobs{jobs::SchedulerConfig{.worker_count=1}};rigid_body::AuthoredSimulation scaled;
     auto large=load();require(large.set_authored_transform(*large.find(id(2)),scale) && scaled.reset(large,scale_jobs),"instantiate scaled density collider");
     require(std::abs(scaled.body(id(2))->mass-6000)<1,"actual native mass includes authored collider scale");
 }
#endif
 // Typed file edits stage catalog and assignments together, validating only the complete candidate.
 auto request=tools::world_authoring::parse_edit_request("vice-world-edit 1\nrevision sha256:"+std::string(64,'a')+"\nset_material "+id(0x105).to_string()+" \"Foam\" 100 0.2 0.1 none\nset_entity_material "+id(3).to_string()+" "+id(0x105).to_string()+"\n");
 require(request.request.has_value(),"parse typed material edits");const auto edited=tools::world_authoring::apply(w.snapshot(),request.request->transaction);
 require(edited && edited.snapshot.materials.size()==5,"transaction creates and assigns shared material");
 auto transaction=request.request->transaction;transaction.operations.push_back(tools::world_authoring::SetWorldMaterial{id(0x102),std::nullopt});
 require(!tools::world_authoring::apply(w.snapshot(),transaction),"transaction cannot remove a material still in use");
#if defined(VICE_MATERIAL_PHYSICS)
 jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count=1}};rigid_body::AuthoredSimulation physics;require(physics.reset(w,jobs),"initialize shared native materials");
 require(std::abs(physics.body(id(2))->mass-250)<0.1F && std::abs(physics.body(id(3))->mass-2000)<0.1F,"actual native mass agrees with shared density");
#endif
 auto module=runtime::load_game_module(VICE_MATERIAL_TEST_MODULE);require(bool(module),"load actual SDK material fixture");project::Manifest manifest;
#if defined(VICE_MATERIAL_PHYSICS)
 runtime::GameModuleLifecycle lifecycle{*module.module,{manifest,w,&physics}};
#else
 runtime::GameModuleLifecycle lifecycle{*module.module,{manifest,w}};
#endif
 require(lifecycle.start(),"SDK material reads and size/thread/identity guards");
#if defined(VICE_MATERIAL_PHYSICS)
 double rebound=50.5;bool touched=false;
 for(unsigned i=0;i<150;++i){require(physics.step(),"step material physics");const auto y=w.spatial_state(*w.find(id(4)))->global_position.y;if(y<50.7)touched=true;if(touched)rebound=std::max(rebound,y);}
 require(w.rigid_box(ice)->linear_velocity.x>3.7F && std::abs(w.rigid_box(grip)->linear_velocity.x)<0.2F,"shared friction changes real sliding behavior");
 require(rebound>52 && w.spatial_state(*w.find(id(5)))->global_position.y<50.7,"shared restitution changes real rebound behavior");
 require(physics.set_body_velocity(id(7),{0,-8,0},{})==rigid_body::BodyControlResult::applied && physics.step(),"physical break with shared materials");
#else
 require(w.publish_distance_link_break(*w.find(id(8)),1200,1),"publish supplied material break evidence");
#endif
 const auto context=w.causal_context(id(8));const auto event=std::find_if(context.events.begin(),context.events.end(),[](const auto& e){return e.kind==world::CausalKind::constraint_broken;});
 require(event!=context.events.end() && event->first_material==id(0x103) && event->second_material==id(0x102),"physical root freezes both persistent material identities");
 render::Color color;lifecycle.frame(color);require(color.red==1 && color.green==1,"SDK reads material provenance without changing previous event DTO layout");lifecycle.stop();
 const auto saved=w.snapshot();const auto bytes=world::encode_world_snapshot(saved);require(bool(bytes),"save shared materials and evidence");
 const auto decoded=world::decode_world_snapshot(bytes.bytes);require(decoded.snapshot && decoded.snapshot->materials==saved.materials && decoded.snapshot->causal_history.events==saved.causal_history.events,"binary material and historical identity round trip");
 const auto text=tools::world_cooker::serialize_source_manifest(saved);const auto parsed=tools::world_cooker::parse_source_manifest(text);
 require(parsed.snapshot && parsed.snapshot->materials==saved.materials && parsed.snapshot->causal_history.events==saved.causal_history.events && tools::world_cooker::serialize_source_manifest(*parsed.snapshot)==text,"deterministic source round trip with material provenance");
 world::World resumed;require(resumed.restore(*decoded.snapshot),"restore material checkpoint");
#if defined(VICE_MATERIAL_PHYSICS)
 rigid_body::AuthoredSimulation continued;require(continued.reset(resumed,jobs) && std::abs(continued.body(id(2))->mass-250)<0.1F,"resume reconstructs shared native mass");
 auto change=*resumed.material(id(0x102));change.density=500;require(resumed.set_material(change),"live material update");const auto step=continued.coordinator().step_index();
 require(!continued.step() && continued.coordinator().step_index()==step && continued.set_body_velocity(id(2),{},{})==rigid_body::BodyControlResult::reset_required,"material edit fails before native advance or control");
 require(continued.reset(resumed,jobs) && std::abs(continued.body(id(2))->mass-500)<0.1F,"reset rebuilds changed material density");
 auto reshaped=*resumed.authored_transform(*resumed.find(id(2)));reshaped.scale.x=2;require(resumed.set_authored_transform(*resumed.find(id(2)),reshaped) && !continued.body(id(2)),"native material reads do not report stale scaled mass");
#endif
 require(resumed.set_entity_material(*resumed.find(id(6)),{}) && resumed.set_entity_material(*resumed.find(id(3)),{}) && resumed.set_entity_material(*resumed.find(id(5)),{}) && resumed.remove_material(id(0x103)),"catalog removal allowed after detaching all live users");
 require(world::validate_causal_history(resumed.causal_snapshot()) && resumed.causal_event(event->sequence)->first_material==id(0x103),"historical material UUID survives live catalog removal");
 const auto debug=runtime::sample_runtime_debug_entity(w,id(2));require(debug.material && debug.material->id==id(0x102),"Runtime debug resolves same shared material");
#if defined(VICE_MATERIAL_PHYSICS)
 auto replacement=w.snapshot();for(auto& m:replacement.materials)if(m.id==id(0x103))m.density=3000;
 const auto old_revision=w.material_revision(),old_step=physics.coordinator().step_index();
 require(w.restore(replacement) && w.material_revision()>old_revision && !physics.step() && physics.coordinator().step_index()==old_step && !physics.body(id(3)),"live restore invalidates native material state even with identical catalog/assignment counts");
 auto copied=load();rigid_body::AuthoredSimulation copy_physics;require(copy_physics.reset(copied,jobs),"copy replacement fixture");
 auto distinct=load();auto dense=*distinct.material(id(0x102));dense.density=600;require(distinct.set_material(dense),"distinct material state");
 copied=distinct;require(!copy_physics.step() && !copy_physics.body(id(2)),"World copy replacement cannot alias material generations");
#endif
 // Version 11 marker fixture predates material IDs and catalog tails.
 world::World old;require(old.begin_causal_recording(),"old capture fixture");auto legacy=world::encode_world_snapshot(old.snapshot()).bytes;
 require(legacy.size()==279,"known v12 empty capture layout");legacy.resize(legacy.size()-24);legacy.erase(legacy.begin()+223,legacy.begin()+255);legacy[8]=std::byte{11};rehash(legacy);
 const auto v11=world::decode_world_snapshot(legacy);require(v11.snapshot && v11.snapshot->materials.empty() && v11.snapshot->causal_history.events.size()==1 && v11.snapshot->causal_history.events[0].first_material.is_nil(),"v11 history loads without invented material provenance");
#if defined(VICE_MATERIAL_EDITOR)
 world::World undo_world;const auto owned=world::ResourceId{id(0x350)};require(undo_world.create_resource(owned).has_value(),"owned render resource");
 auto undo_material=*w.material(id(0x102));undo_material.render_material=owned;require(undo_world.set_material(undo_material),"catalog owns visual binding");
 const auto undo_entity=undo_world.create_entity();require(undo_world.set_entity_material(undo_entity,undo_material.id),"undo fixture material");
 world::SnapshotEntity record;record.persistent_id=*undo_world.persistent_id(undo_entity);record.material=undo_material.id;
 editor::CommandHistory history;require(history.record(editor::CommandHistory::CreateEntityEntry{record,{owned},"Create"}),"record owned creation");
 require(!history.undo(undo_world) && undo_world.alive(undo_entity) && undo_world.find_resource(owned),"catalog reference prevents partial undo deletion");
 history.clear();require(history.record(editor::CommandHistory::CreateEntityEntry{record,{},"Create"}) && history.undo(undo_world) && history.redo(undo_world),"entity recreate undo/redo");
 require(undo_world.entity_material(*undo_world.find(record.persistent_id))==record.material,"undo recreation preserves assignment");
 auto authored=load();const auto authored_source=tools::world_cooker::serialize_source_manifest(authored.snapshot());editor::PreviewRuntime preview;editor::PreviewGameConfig game{manifest,VICE_MATERIAL_TEST_MODULE};std::string diagnostics;
 require(preview.reset(authored,jobs,editor::AssetIndex{},&diagnostics,&game),"Editor shared material bootstrap");
 require(preview.world()->entity_material(*preview.world()->find(id(2)))==id(0x102),"preview retains material identity");
 for(unsigned i=0;i<120;++i)require(preview.step_once(),"step preview material behavior");
 require(preview.world()->rigid_box(*preview.world()->find(id(2)))->linear_velocity.x>3.7F && std::abs(preview.world()->rigid_box(*preview.world()->find(id(3)))->linear_velocity.x)<0.2F,"preview agrees on friction");
 require(tools::world_cooker::serialize_source_manifest(authored.snapshot())==authored_source,"preview leaves authoring unchanged");
#endif
}
