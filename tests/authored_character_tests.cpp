#include <vice/world/world.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/tools/world_authoring.hpp>
#include <vice/runtime/module_loader.hpp>
#include <vice/runtime/debug.hpp>
#include <vice/rigid_body/authored_simulation.hpp>
#include <vice/jobs/jobs.hpp>
#if defined(VICE_CHARACTER_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
namespace {
using namespace vice;
void require(bool ok,const char* message) { if (!ok) { std::cerr<<"Authored character: "<<message<<'\n'; std::exit(1); } }
world::PersistentId id(unsigned value) { char text[37]; std::snprintf(text,sizeof(text),"9a000000-0000-4000-8000-%012x",value); return *world::PersistentId::parse(text); }
std::string source() { return "vice-world-source 1\nregion 1 10 1000000000000 50 0 0 0 0 full none\n"
    "entity 9a000000-0000-4000-8000-000000000001 spatial 1000000000000 49.5 0 1 rigid_box static 10 0.5 10 0 0 0 0 0 0 0.5 0 discrete solid\n"
    "entity 9a000000-0000-4000-8000-000000000002 spatial 1000000000000 50 0 1 character 1.8 0.3 70 100 0.872664626 0.4 0.5 0 0 0 0 0 0 0\n"; }
world::World load() { auto p=tools::world_cooker::parse_source_manifest(source()); require(p.snapshot.has_value(),"parse capsule source"); world::World w; require(w.restore(*p.snapshot),"restore character World"); return w; }
void ticks(rigid_body::AuthoredSimulation& simulation,int n) { for(int i=0;i<n;++i) { std::string d; if(!simulation.step(&d)) {std::cerr<<d<<'\n';require(false,"shared controller stepping");} } }
void hash(std::vector<std::byte>& bytes) { std::uint64_t h=14695981039346656037ULL; for(std::size_t i=40;i<bytes.size();++i){h^=std::to_integer<std::uint8_t>(bytes[i]);h*=1099511628211ULL;} for(std::size_t i=0;i<8;++i)bytes[32+i]=static_cast<std::byte>((h>>(8U*i))&255U); }
}
int main() {
    using C=rigid_body::CharacterControlResult;
    auto world=load(); const auto actor=*world.find(id(2));
    const auto canonical=tools::world_cooker::serialize_source_manifest(world.snapshot());
    require(tools::world_cooker::serialize_source_manifest(*tools::world_cooker::parse_source_manifest(canonical).snapshot)==canonical,"deterministic capsule source round trip");
    require(!world.set_rigid_box(actor,{}),"one physical representation per entity");
    auto invalid=*world.character(actor); invalid.height=0.5F; require(!world.set_character(actor,invalid),"invalid capsule rejected");
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count=1}};
    rigid_body::AuthoredSimulation physics; require(physics.reset(world,jobs) && physics.character_count()==1,"shared authored character bootstrap");
    ticks(physics,4);
    require(physics.character(id(2))->state.ground_state==rigid_body::CharacterGroundState::grounded && physics.character(id(2))->supporting_entity==id(1),"physical support at arbitrary height and large origin");
    const auto before=*world.character(actor);
    require(physics.control_character(id(2),{0,1,0})==C::invalid_argument && physics.control_character(id(2),{},-1)==C::invalid_argument && world.character(actor)==before,"invalid commands leave motion unchanged");
    require(physics.control_character(id(2),{2,0,0},5)==C::applied,"queue grounded jump and walk");
    const auto queued=*world.character(actor);
    require(queued.pending_jump_speed==5 && queued.horizontal_velocity.x==2,"command is immediately authoritative for saving");
    require(physics.control_character(id(2),{9,0,0},5)==C::not_grounded && world.character(actor)==queued,"duplicate jump rejected atomically");
    const auto saved=world::encode_world_snapshot(world.snapshot()); require(static_cast<bool>(saved),"encode queued character");
    world::World resumed; require(resumed.restore(*world::decode_world_snapshot(saved.bytes).snapshot),"restore character motion");
    rigid_body::AuthoredSimulation restarted; require(restarted.reset(resumed,jobs),"recreate contacts and queued jump");
    ticks(physics,1); ticks(restarted,1);
    require(world.character(actor)->velocity.y>4 && resumed.character(*resumed.find(id(2)))->velocity.y>4 && !resumed.character(*resumed.find(id(2)))->pending_jump_speed,"queued jump survives resume and consumes once");
    require(physics.control_character(id(2),{},5)==C::not_grounded,"airborne jump rejected");
    ticks(physics,15); ticks(restarted,15);
    require(std::abs(world.spatial_state(actor)->global_position.y-resumed.spatial_state(*resumed.find(id(2)))->global_position.y)<0.01 &&
        std::abs(world.spatial_state(actor)->global_position.x-resumed.spatial_state(*resumed.find(id(2)))->global_position.x)<0.01,"resumed trajectory agrees");
    // Save while airborne, then continue both simulations from the same state.
    const auto air=world::encode_world_snapshot(world.snapshot()); world::World air_world; require(air_world.restore(*world::decode_world_snapshot(air.bytes).snapshot),"airborne save");
    rigid_body::AuthoredSimulation air_sim; require(air_sim.reset(air_world,jobs),"airborne reset"); ticks(physics,12); ticks(air_sim,12);
    require(std::abs(world.spatial_state(actor)->global_position.y-air_world.spatial_state(*air_world.find(id(2)))->global_position.y)<0.02,"airborne velocity survives without a phantom ground jump");
    auto driven=load(); rigid_body::AuthoredSimulation driven_physics; require(driven_physics.reset(driven,jobs),"SDK controller fixture"); ticks(driven_physics,4);
    auto module=runtime::load_game_module(VICE_CHARACTER_TEST_MODULE); require(static_cast<bool>(module),"load actual SDK-only character module");
    project::Manifest manifest; runtime::GameModuleLifecycle lifecycle{*module.module,{manifest,driven,&driven_physics}};
    require(lifecycle.start(),"SDK query, record validation, wrong-thread guards");
    lifecycle.event(platform::Event{platform::KeyEvent{.key=platform::Key::space,.pressed=true}}); ticks(driven_physics,12);
    render::Color color; lifecycle.frame(color); require(color.red==1 && color.green==1 && color.blue==1,"SDK commands move capsule, jump and report observed state"); lifecycle.stop();
    const auto debug=runtime::sample_runtime_debug_entity(driven,id(2),&driven_physics);
    require(debug.character && debug.character_state && runtime::serialize_runtime_debug_entity_snapshot(debug,"characters").find("ground_state")!=std::string::npos,"normal Runtime debugging observes capsule and contacts");
    // Stair policy uses the same authored component and bootstrap.
    for(const auto pair : {std::pair{0.30F,true},std::pair{0.65F,false}}) {
        auto stairs=load(); const auto step=*stairs.create_entity(id(3));
        require(stairs.set_spatial_state(step,{{1000000000001.2,50+0.5*pair.first,0},{1}}),"stair spatial");
        world::RigidBox box; box.motion=world::RigidMotion::static_body; box.half_extents={0.6F,0.5F*pair.first,2}; require(stairs.set_rigid_box(step,box),"authored stair");
        rigid_body::AuthoredSimulation sim; require(sim.reset(stairs,jobs),"stair bootstrap"); ticks(sim,4); require(sim.control_character(id(2),{2,0,0})==C::applied,"walk toward stair"); ticks(sim,90);
        const auto p=stairs.spatial_state(*stairs.find(id(2)))->global_position;
        require(pair.second ? p.x>1000000000001.0 && p.y>50.15 : p.x<1000000000000.55 && p.y<50.1,"authored step-height policy");
    }
    for(const auto pair : {std::pair{30.0F,true},std::pair{70.0F,false}}) {
        auto slope=load(); const float a=pair.first*rigid_body::pi/180;
        const auto floor=*slope.find(id(1)); auto b=*slope.rigid_box(floor); b.half_extents={20,1,20}; require(slope.set_rigid_box(floor,b),"slope collider");
        require(slope.set_spatial_state(floor,{{1000000000000.0,50,0},{1}}),"slope center");
        world::AuthoredTransform t; t.rotation={0,0,std::sin(0.5F*a),std::cos(0.5F*a)}; require(slope.set_authored_transform(floor,t),"slope angle");
        const double y=(0.32+1)/std::cos(a)-0.32; require(slope.set_spatial_state(*slope.find(id(2)),{{1000000000000.0,50+y,0},{1}}),"slope foot");
        rigid_body::AuthoredSimulation sim; require(sim.reset(slope,jobs),"slope bootstrap"); ticks(sim,1);
        require((sim.character(id(2))->state.ground_state==rigid_body::CharacterGroundState::grounded)==pair.second,"authored slope limit classification");
    }
    auto platform=load(); const auto floor=*platform.find(id(1)); auto moving=*platform.rigid_box(floor); moving.motion=world::RigidMotion::kinematic_body; moving.linear_velocity={1,0,0}; require(platform.set_rigid_box(floor,moving),"moving support");
    rigid_body::AuthoredSimulation carrier; require(carrier.reset(platform,jobs),"platform bootstrap"); ticks(carrier,60);
    require(platform.spatial_state(*platform.find(id(2)))->global_position.x>1000000000000.3 && carrier.character(id(2))->state.ground_velocity.x>0.9F,"ground velocity carries authored capsule");
    auto crossing=load().snapshot();
    crossing.regions[0].bounds=world::RegionBounds{{-10,-10,-10},{1,10,10}};
    auto destination=crossing.regions[0]; destination.id={2}; destination.simulation_island={20}; destination.origin.x+=10; destination.bounds=world::RegionBounds{{-9,-10,-10},{10,10,10}}; crossing.regions.push_back(destination);
    auto target_floor=crossing.entities.front(); target_floor.persistent_id=id(3); target_floor.spatial_state->region={2}; target_floor.spatial_state->global_position.x+=10; crossing.entities.push_back(target_floor);
    world::World traveling; require(traveling.restore(crossing),"bounded character regions"); rigid_body::AuthoredSimulation travel;
    require(travel.reset(traveling,jobs) && travel.control_character(id(2),{2,0,0})==C::applied,"walk across region boundary"); ticks(travel,75);
    require(traveling.spatial_state(*traveling.find(id(2)))->region==world::RegionId{2} && traveling.character(*traveling.find(id(2)))->horizontal_velocity.x==2 &&
        travel.character(id(2))->supporting_entity==id(3) && travel.character(id(2))->state.ground_state==rigid_body::CharacterGroundState::grounded,"automatic handoff preserves identity, motion and reconstructed support");
    auto added=load(); rigid_body::AuthoredSimulation adding; require(adding.reset(added,jobs),"addition fixture"); const auto extra=*added.create_entity(id(99));
    require(added.set_spatial_state(extra,{{1000000000004.0,52,0},{1}}) && added.set_character(extra,{}),"author another controller");
    const auto before_add=adding.coordinator().step_index(); require(!adding.step() && adding.coordinator().step_index()==before_add && adding.reset(added,jobs) && adding.character_count()==2,"live additions require reset before advance");
    // Stale structural edits fail before advancing; inactive regions reject controls.
    require(platform.set_region_fidelity({1},world::RegionFidelity::inactive) && carrier.control_character(id(2),{})==C::inactive_region,"inactive controller rejects command");
    const auto step=carrier.coordinator().step_index(); auto changed=*platform.character(*platform.find(id(2))); changed.radius=0.31F;
    require(platform.set_character(*platform.find(id(2)),changed) && !carrier.step() && carrier.coordinator().step_index()==step,"stale configuration fails before advancing");
    tools::world_authoring::Transaction tx; tx.operations={tools::world_authoring::SetCharacter{id(2),world::Character{}}}; require(static_cast<bool>(tools::world_authoring::apply(load().snapshot(),tx)),"typed character authoring");
    tx.operations.push_back(tools::world_authoring::SetRigidBox{id(2),world::RigidBox{}}); require(!tools::world_authoring::apply(load().snapshot(),tx),"two physical representations reject whole batch");
    // The representative game systems coexist in one saved World, not separate fixtures.
    auto composed=load();
    for(unsigned n=3;n<=6;++n) require(composed.create_entity(id(n)).has_value(),"composed identities");
    const auto payload=*composed.find(id(3)); require(composed.set_spatial_state(payload,{{999999999996.0,53,0},{1}}) && composed.set_rigid_box(payload,{}),"composed dynamic object");
    world::DistanceLink link; link.first=id(1); link.second=id(3); link.min_distance=link.max_distance=std::hypot(4.0F,3.5F); link.break_threshold=0.001F;
    require(composed.set_distance_link(*composed.find(id(4)),link) && composed.set_dependency(*composed.find(id(5)),{}) && composed.set_dependency(*composed.find(id(6)),{id(5),id(4),true}),"composed physical supply gate");
    rigid_body::AuthoredSimulation composition; require(composition.reset(composed,jobs),"one shared bootstrap for all systems"); ticks(composition,2);
    require(composition.control_character(id(2),{2,0,0},5)==C::applied,"jump in composed World"); ticks(composition,6);
    const auto broken_gate=composed.distance_link(*composed.find(id(4))); const auto composed_save=world::encode_world_snapshot(composed.snapshot());
    world::World composed_resume; require(composed_resume.restore(*world::decode_world_snapshot(composed_save.bytes).snapshot),"save composed World");
    rigid_body::AuthoredSimulation resumed_composition; require(resumed_composition.reset(composed_resume,jobs),"resume all composed systems"); ticks(resumed_composition,1);
    require(broken_gate && broken_gate->broken && composed_resume.distance_link(*composed_resume.find(id(4)))==broken_gate &&
        !composed_resume.dependency_state(*composed_resume.find(id(6)))->available && composed_resume.character(*composed_resume.find(id(2)))->velocity.y>1,"airborne character and broken causal gate survive together");
    world::World legacy; require(legacy.create_entity(id(1)).has_value(),"legacy fixture"); auto bytes=world::encode_world_snapshot(legacy.snapshot()).bytes; require(bytes.size()==82,"current minimal layout"); bytes.resize(bytes.size()-12U);bytes.pop_back(); bytes.erase(bytes.begin()+64); bytes[8]=std::byte{9}; hash(bytes); const auto old=world::decode_world_snapshot(bytes); require(old.snapshot && !old.snapshot->entities[0].character,"v9 remains readable");
#if defined(VICE_CHARACTER_EDITOR)
    auto authored=load(); editor::AssetIndex assets; editor::PreviewRuntime preview; std::string diagnostics;
    const editor::PreviewGameConfig config{manifest,VICE_CHARACTER_TEST_MODULE};
    require(preview.reset(authored,jobs,assets,&diagnostics,&config),"preview loads same SDK module");
    require(preview.snapshot().characters.size()==1 && preview.snapshot().characters[0].character.height>1.7F,"authored capsule appears in normal preview debug overlay");
    require(preview.step_once(),"preview settles"); preview.play(); preview.set_input_focus(true);
    require(preview.handle_event(platform::Event{platform::KeyEvent{.key=platform::Key::space,.pressed=true}}),"preview gameplay input"); preview.pause();
    for(int i=0;i<12;++i) require(preview.step_once(),"preview fixed stepping");
    require(preview.clear_color().red==1 && preview.clear_color().green==1 && preview.clear_color().blue==1 && authored.spatial_state(*authored.find(id(2)))->global_position.y==50,"preview executes same capsule game without changing authoring");
    editor::PreviewRuntime composed_preview; const auto composed_before=composed.character(*composed.find(id(2)));
    require(composed_preview.reset(composed,jobs,assets,&diagnostics,&config) && composed_preview.step_once() && composed_preview.clear_color().red==1 &&
        !composed_preview.world()->dependency_state(*composed_preview.world()->find(id(6)))->available && composed.character(*composed.find(id(2)))==composed_before,"same SDK module observes composed preview without repairing failure or mutating source");
    auto only=load().snapshot(); only.entities.erase(only.entities.begin()); world::World character_only; require(character_only.restore(only),"character-only source");
    editor::PreviewRuntime no_module; require(no_module.reset(character_only,jobs,assets,&diagnostics) && no_module.step_once() && no_module.snapshot().characters.size()==1 && no_module.snapshot().bodies.empty(),"character-only preview uses authored path without implicit Lab bodies");
#endif
    return 0;
}
