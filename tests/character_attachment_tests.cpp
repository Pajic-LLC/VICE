#include <vice/world/world.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/tools/world_authoring.hpp>
#include <vice/runtime/module_loader.hpp>
#include <vice/runtime/debug.hpp>
#include <vice/runtime/scene.hpp>
#if defined(VICE_ATTACHMENT_PHYSICS)
#include <vice/rigid_body/authored_simulation.hpp>
#include <vice/jobs/jobs.hpp>
#endif
#if defined(VICE_ATTACHMENT_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
namespace {
using namespace vice;
void require(bool ok,const char* message){if(!ok){std::cerr<<"Character carrier: "<<message<<'\n';std::exit(1);}}
world::PersistentId id(unsigned n){char text[37];std::snprintf(text,sizeof(text),"a2000000-0000-4000-8000-%012x",n);return *world::PersistentId::parse(text);}
world::World load(){
    world::World w;require(w.register_region({.id={1},.simulation_island={10},.origin={1e12,50,0},.fidelity=world::RegionFidelity::full}),"region");
    for(unsigned n=1;n<=4;++n)require(w.create_entity(id(n)).has_value(),"identities");
    const auto floor=*w.find(id(1)),car=*w.find(id(2)),actor=*w.find(id(3)),camera=*w.find(id(4));
    world::RigidBox b;b.motion=world::RigidMotion::static_body;b.half_extents={100,0.5F,100};
    require(w.set_spatial_state(floor,{{1e12,49.5,0},{1}}) && w.set_rigid_box(floor,b),"floor");
    b={};b.half_extents={0.85F,0.25F,1.8F};b.friction=0;b.continuous=true;
    world::MaterialDefinition m{id(100),"Chassis",400,0,0,{}};
    require(w.set_material(m) && w.set_spatial_state(car,{{1e12,50.8,0},{1}}) && w.set_rigid_box(car,b) && w.set_entity_material(car,m.id) && w.set_vehicle(car,{}),"car");
    require(w.set_spatial_state(actor,{{1e12-2,50,0},{1}}) && w.set_character(actor,{}),"walker");
    require(w.set_spatial_state(camera,{{1e12-2,53,-6},{1}}) && w.set_camera(camera,{.primary=true}),"camera");return w;
}
#if defined(VICE_ATTACHMENT_PHYSICS)
void ticks(rigid_body::AuthoredSimulation& s,unsigned n){for(unsigned i=0;i<n;++i){std::string d;if(!s.step(&d)){std::cerr<<d<<'\n';require(false,"shared fixed step");}}}
#endif
void rehash(std::vector<std::byte>& bytes){std::uint64_t h=14695981039346656037ULL;for(std::size_t i=40;i<bytes.size();++i){h^=std::to_integer<std::uint8_t>(bytes[i]);h*=1099511628211ULL;}for(std::size_t i=0;i<8;++i)bytes[32+i]=static_cast<std::byte>((h>>(8*i))&255);}
}
int main(){
    auto w=load();const auto actor=*w.find(id(3)),car=*w.find(id(2));
    require(!w.set_character_attachment(actor,{id(3),{}}) && !w.set_character_attachment(actor,{id(99),{}}),"invalid parent rejected");
    require(w.set_character_attachment(actor,{id(2),{0,-0.4F,0}}),"World owns carrier identity and anchor");
    require(!w.destroy_entity(car) && !w.clear_rigid_box(car) && !w.clear_spatial_state(car),"referenced carrier cannot disappear");
    const auto source=tools::world_cooker::serialize_source_manifest(w.snapshot());const auto parsed=tools::world_cooker::parse_source_manifest(source);
    require(parsed.snapshot && tools::world_cooker::serialize_source_manifest(*parsed.snapshot)==source,"attachment canonical source round trip");
    const auto saved=world::encode_world_snapshot(w.snapshot());const auto decoded=world::decode_world_snapshot(saved.bytes);world::World restored;
    require(decoded.snapshot && restored.restore(*decoded.snapshot) && restored.character_attachment(*restored.find(id(3)))==w.character_attachment(actor),"binary carrier round trip");
    auto invalid=w.snapshot();invalid.entities[1].rigid_box.reset();require(!w.restore(invalid) && w.character_attachment(actor).has_value(),"invalid parent restore is atomic");
    auto old = world::encode_world_snapshot(load().snapshot()).bytes;
    old.resize(old.size() - 54);
    old[8] = std::byte{13};
    rehash(old);
    const auto v13 = world::decode_world_snapshot(old);
    require(v13.snapshot && !v13.snapshot->entities[2].character_attachment,
            "v13 remains readable");
    auto truncated=saved.bytes;truncated.pop_back();rehash(truncated);require(!world::decode_world_snapshot(truncated),"truncated attachment rejected");
    tools::world_authoring::Transaction tx;tx.operations={tools::world_authoring::SetCharacterAttachment{id(3),std::nullopt}};require(bool(tools::world_authoring::apply(w.snapshot(),tx)),"typed detach authoring");
    require(w.clear_character(actor) && !w.character_attachment(actor) && w.destroy_entity(car),"removing character releases carrier reference");
    w=load();auto module=runtime::load_game_module(VICE_INTERACTION_TEST_MODULE);require(bool(module),"SDK interaction fixture");project::Manifest manifest;
#if defined(VICE_ATTACHMENT_PHYSICS)
    using R=rigid_body::AttachmentControlResult;using C=rigid_body::CharacterControlResult;
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count=1}};rigid_body::AuthoredSimulation sim;require(sim.reset(w,jobs),"shared walking bootstrap");ticks(sim,120);
    require(sim.attach_character(id(3),id(2),{0,-0.4F,0},0.1F)==R::out_of_range && !w.character_attachment(*w.find(id(3))),"entry reach checked without mutation");
    runtime::GameModuleLifecycle lifecycle{*module.module,{manifest,w,&sim}};require(lifecycle.start(),"SDK camera/attachment size/phase/thread/old-host guards");
    lifecycle.event(platform::Event{platform::KeyEvent{.key=platform::Key::e,.pressed=true}});
    require(w.character_attachment(*w.find(id(3))).has_value() && sim.control_character(id(3),{1,0,0})==C::attached,"mounted capsule cannot walk independently");
    require(!sim.character(id(3))->id && sim.attach_character(id(3),id(2),{},3)==R::already_attached,"mounted controller has no native capsule or duplicate entry");
    lifecycle.event(platform::Event{platform::KeyEvent{.key=platform::Key::up,.pressed=true}});ticks(sim,120);lifecycle.update(0.01);
    render::Color color;lifecycle.frame(color);require(color.red==1 && color.green==1 && color.blue==1,"SDK controls chassis and following authoritative camera");
    const auto foot=w.spatial_state(*w.find(id(3)))->global_position,chassis=w.spatial_state(*w.find(id(2)))->global_position;
    require(std::abs(foot.z-chassis.z)<0.05 && std::abs(foot.x-chassis.x)<0.05,"carried identity follows actual chassis");
    const auto before=tools::world_cooker::serialize_source_manifest(w.snapshot());
    require(sim.detach_character(id(3),{2,-0.5F,0},0.1F,0.5F)==R::blocked && tools::world_cooker::serialize_source_manifest(w.snapshot())==before,"unsafe moving exit is atomic");
    const auto moving=world::encode_world_snapshot(w.snapshot());world::World resumed;require(resumed.restore(*world::decode_world_snapshot(moving.bytes).snapshot),"save while driving with attached character");
    rigid_body::AuthoredSimulation continuation;require(continuation.reset(resumed,jobs),"resume attached representation");ticks(continuation,30);
    require(resumed.character_attachment(*resumed.find(id(3)))->carrier==id(2) && std::abs(resumed.spatial_state(*resumed.find(id(3)))->global_position.z-resumed.spatial_state(*resumed.find(id(2)))->global_position.z)<0.1,"restart preserves carrier identity and follows chassis without an independent capsule");
    require(sim.control_vehicle(id(2),0,0,1)==rigid_body::VehicleControlResult::applied,"brake before exit");ticks(sim,180);
    require(sim.detach_character(id(3),{0,-0.5F,0},1,0.5F)==R::blocked,"exit inside chassis rejected");
    require(sim.detach_character(id(3),{2,5,0},1,0.5F)==R::blocked,"unsupported exit rejected");
    lifecycle.event(platform::Event{platform::KeyEvent{.key=platform::Key::e,.pressed=true}});lifecycle.frame(color);require(color.red==1 && color.green==0,"SDK exit restores walker");
    require(!w.character_attachment(*w.find(id(3))) && sim.character(id(3))->id && sim.control_character(id(3),{1,0,0})==C::applied,"detached capsule resumes physical control");ticks(sim,30);
    require(w.spatial_state(*w.find(id(3)))->global_position.x>1e12+2,"walker moves after driving");lifecycle.stop();
    // Exit into a solid obstruction must preserve both World and representation.
    auto blocked=load();const auto wall=*blocked.create_entity(id(5));world::RigidBox solid;solid.motion=world::RigidMotion::static_body;solid.half_extents={0.5F,1,2};
    require(blocked.set_spatial_state(wall,{{1e12+2,51,0},{1}}) && blocked.set_rigid_box(wall,solid),"blocked doorway");rigid_body::AuthoredSimulation blocked_sim;
    require(blocked_sim.reset(blocked,jobs) && blocked_sim.attach_character(id(3),id(2),{0,-0.4F,0},3)==R::applied,"blocked fixture entry");ticks(blocked_sim,120);
    const auto blocked_before=tools::world_cooker::serialize_source_manifest(blocked.snapshot());require(blocked_sim.detach_character(id(3),{2,-0.5F,0},1,0.5F)==R::blocked && tools::world_cooker::serialize_source_manifest(blocked.snapshot())==blocked_before,"blocked exit is atomic");
    require(blocked_sim.detach_character(id(3),{-2,-0.5F,0},1,0.5F)==R::applied,"clear alternate exit works");
    auto path_blocked=load();const auto divider=*path_blocked.create_entity(id(5));solid.half_extents={0.1F,1,2};
    require(path_blocked.set_spatial_state(divider,{{1e12+1.25,51,0},{1}}) && path_blocked.set_rigid_box(divider,solid),"intervening wall");rigid_body::AuthoredSimulation path_sim;
    require(path_sim.reset(path_blocked,jobs) && path_sim.attach_character(id(3),id(2),{0,-0.4F,0},3)==R::applied,"path fixture entry");ticks(path_sim,120);
    require(path_sim.detach_character(id(3),{2,-0.5F,0},1,0.5F)==R::blocked,"clear destination cannot bypass intervening wall");
    auto crossing=load().snapshot();crossing.regions[0].bounds=world::RegionBounds{{-100,-100,-100},{100,100,1}};
    auto second=crossing.regions[0];second.id={2};second.simulation_island={20};second.origin.z=10;second.bounds=world::RegionBounds{{-100,-100,-9},{100,100,100}};crossing.regions.push_back(second);
    auto second_road=crossing.entities.front();second_road.persistent_id=id(7);second_road.spatial_state->region={2};second_road.spatial_state->global_position.z=10;crossing.entities.push_back(second_road);
    world::World traveling;require(traveling.restore(crossing),"mounted crossing regions");rigid_body::AuthoredSimulation travel;
    require(travel.reset(traveling,jobs) && travel.attach_character(id(3),id(2),{0,-0.4F,0},3)==R::applied && travel.control_vehicle(id(2),1,0,0)==rigid_body::VehicleControlResult::applied,"mounted trip");ticks(travel,240);
    require(traveling.spatial_state(*traveling.find(id(2)))->region==world::RegionId{2} && traveling.spatial_state(*traveling.find(id(3)))->region==world::RegionId{2},"carried character follows carrier region handoff");
    require(travel.control_vehicle(id(2),0,0,1)==rigid_body::VehicleControlResult::applied,"stop after handoff");ticks(travel,240);
    require(travel.detach_character(id(3),{2,-0.5F,0},1,0.5F)==R::applied && travel.character(id(3))->id,"exit creates capsule in carrier destination domain");
    auto immediate=load();rigid_body::AuthoredSimulation immediate_sim;require(immediate_sim.reset(immediate,jobs) && immediate_sim.attach_character(id(3),id(2),{0,-0.4F,0},3)==R::applied,"immediate velocity fixture");
    require(immediate_sim.set_body_velocity(id(2),{0,0,4},{})==rigid_body::BodyControlResult::applied && immediate.character(*immediate.find(id(3)))->velocity.z==4 && immediate_sim.character(id(3))->state.velocity.z==4,"direct carrier controls immediately update carried velocity");
    require(immediate.set_spatial_state(*immediate.find(id(2)),{{1e12,52,0},{1}}) && !immediate_sim.character(id(3)),"attached reads reject externally moved carrier");
    auto structural=load();rigid_body::AuthoredSimulation structural_sim;require(structural_sim.reset(structural,jobs) && structural_sim.attach_character(id(3),id(2),{},3)==R::applied,"structural control fixture");
    require(structural.clear_character_attachment(*structural.find(id(3))),"external detach");const auto old_velocity=structural.rigid_box(*structural.find(id(2)))->linear_velocity;
    require(structural_sim.set_body_velocity(id(2),{0,0,9},{})==rigid_body::BodyControlResult::reset_required && structural.rigid_box(*structural.find(id(2)))->linear_velocity.z==old_velocity.z,"stale attachment rejects carrier control before mutation");
    auto editing=load();rigid_body::AuthoredSimulation stale;require(stale.reset(editing,jobs),"stale fixture");require(editing.set_character_attachment(*editing.find(id(3)),{id(2),{}}),"external attachment edit");const auto step=stale.coordinator().step_index();
    require(!stale.step() && stale.coordinator().step_index()==step && stale.attach_character(id(3),id(2),{},3)==R::reset_required,"external attachment requires reset");
#else
    runtime::GameModuleLifecycle lifecycle{*module.module,{manifest,w}};require(lifecycle.start(),"backend-disabled camera and attachment inspection");lifecycle.update(0.01);render::Color color;lifecycle.frame(color);require(color.red==1,"camera controls work without physics backend");lifecycle.stop();
#endif
    const auto debug=runtime::sample_runtime_debug_entity(restored,id(3));require(debug.character_attachment && runtime::serialize_runtime_debug_entity_snapshot(debug,"carrier").find("character_attachment")!=std::string::npos,"Runtime debug exposes persistent carrier");
#if defined(VICE_ATTACHMENT_EDITOR)
    auto authored=load();const auto authored_source=tools::world_cooker::serialize_source_manifest(authored.snapshot());editor::PreviewRuntime preview;const editor::PreviewGameConfig config{manifest,VICE_INTERACTION_TEST_MODULE};std::string d;
    require(preview.reset(authored,jobs,editor::AssetIndex{},&d,&config),"preview interaction module");for(unsigned i=0;i<120;++i)require(preview.step_once(),"preview settle");preview.play();preview.set_input_focus(true);
    require(preview.handle_event(platform::Event{platform::KeyEvent{.key=platform::Key::e,.pressed=true}}),"preview entry input");require(preview.handle_event(platform::Event{platform::KeyEvent{.key=platform::Key::up,.pressed=true}}),"preview drive input");preview.pause();for(unsigned i=0;i<120;++i)require(preview.step_once(),"preview driving");
    require(preview.clear_color().red==1 && preview.world()->character_attachment(*preview.world()->find(id(3))) && preview.world()->spatial_state(*preview.world()->find(id(4)))->global_position.z>-5.5,"preview shares attachment and camera controls");
    require(tools::world_cooker::serialize_source_manifest(authored.snapshot())==authored_source,"preview preserves authored source");
#endif
    std::cout<<"Character carrier and camera contracts passed\n";
}
