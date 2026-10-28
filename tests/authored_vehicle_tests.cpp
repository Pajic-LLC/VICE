#include <vice/world/world.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/tools/world_authoring.hpp>
#include <vice/tools/world_edit.hpp>
#include <vice/runtime/module_loader.hpp>
#include <vice/runtime/debug.hpp>
#if defined(VICE_VEHICLE_PHYSICS)
#include <vice/rigid_body/authored_simulation.hpp>
#include <vice/jobs/jobs.hpp>
#endif
#if defined(VICE_VEHICLE_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
namespace {
using namespace vice;
void require(bool ok,const char* message){if(!ok){std::cerr<<"Authored vehicle: "<<message<<'\n';std::exit(1);}}
world::PersistentId id(unsigned n){char text[37];std::snprintf(text,sizeof(text),"a1000000-0000-4000-8000-%012x",n);return *world::PersistentId::parse(text);}
world::World load(float friction=1) {
    world::World w;require(w.register_region({.id={1},.simulation_island={10},.origin={1e12,50,0},.fidelity=world::RegionFidelity::full}),"region");
    world::MaterialDefinition road{id(100),"Road",1000,friction,0,{}};require(w.set_material(road),"road material");
    const auto floor=*w.create_entity(id(1));require(w.set_spatial_state(floor,{{1e12,49.5,0},{1}}),"floor position");
    world::RigidBox b;b.motion=world::RigidMotion::static_body;b.half_extents={100,0.5F,100};
    require(w.set_rigid_box(floor,b) && w.set_entity_material(floor,id(100)),"floor");
    const auto car=*w.create_entity(id(2));require(w.set_spatial_state(car,{{1e12,50.8,0},{1}}),"chassis pose");
    b={};b.half_extents={0.85F,0.25F,1.8F};b.friction=0;b.continuous=true;
    world::MaterialDefinition steel{id(101),"Chassis",400,0,0,{}};require(w.set_material(steel),"chassis density");
    require(w.set_rigid_box(car,b) && w.set_entity_material(car,id(101)) && w.set_vehicle(car,{}),"vehicle authoring");return w;
}
void rehash(std::vector<std::byte>& bytes){std::uint64_t h=14695981039346656037ULL;for(std::size_t i=40;i<bytes.size();++i){h^=std::to_integer<std::uint8_t>(bytes[i]);h*=1099511628211ULL;}for(std::size_t i=0;i<8;++i)bytes[32+i]=static_cast<std::byte>((h>>(8*i))&255);}
#if defined(VICE_VEHICLE_PHYSICS)
void ticks(rigid_body::AuthoredSimulation& sim,unsigned n){for(unsigned i=0;i<n;++i){std::string d;if(!sim.step(&d)){std::cerr<<d<<'\n';require(false,"shared vehicle stepping");}}}
#endif
}
int main(){
    auto w=load();const auto car=*w.find(id(2));
    require(!w.set_vehicle(*w.find(id(1)),{}),"static chassis rejected");
    auto invalid=*w.vehicle(car);invalid.spring=std::numeric_limits<float>::infinity();require(!w.set_vehicle(car,invalid),"nonfinite tuning rejected");
    invalid=*w.vehicle(car);invalid.brake=-1;require(!w.set_vehicle(car,invalid),"invalid control rejected");
    auto b=*w.rigid_box(car);b.motion=world::RigidMotion::kinematic_body;require(!w.set_rigid_box(car,b),"vehicle cannot silently become kinematic");
    b=*w.rigid_box(car);b.trigger=true;require(!w.set_rigid_box(car,b),"vehicle cannot silently become trigger");
    const auto source=tools::world_cooker::serialize_source_manifest(w.snapshot());
    const auto parsed=tools::world_cooker::parse_source_manifest(source);require(parsed.snapshot && tools::world_cooker::serialize_source_manifest(*parsed.snapshot)==source,"canonical vehicle source round trip");
    const auto bytes=world::encode_world_snapshot(w.snapshot());require(bool(bytes),"encode vehicle");
    const auto decoded=world::decode_world_snapshot(bytes.bytes);world::World restored;require(decoded.snapshot && restored.restore(*decoded.snapshot) && restored.vehicle(*restored.find(id(2)))==w.vehicle(car),"binary vehicle round trip");
    auto corrupt=w.snapshot();corrupt.entities.back().rigid_box.reset();require(!w.restore(corrupt) && w.vehicle(car).has_value(),"invalid restore leaves original World intact");
    tools::world_authoring::Transaction tx;invalid=*w.vehicle(car);invalid.drive_force=7000;tx.operations={tools::world_authoring::SetVehicle{id(2),invalid}};
    require(bool(tools::world_authoring::apply(w.snapshot(),tx)),"typed vehicle edit");
    tx.operations.push_back(tools::world_authoring::SetRigidBox{id(2),std::nullopt});require(!tools::world_authoring::apply(w.snapshot(),tx),"whole candidate rejects missing chassis");
    const auto request=tools::world_authoring::parse_edit_request("vice-world-edit 1\nrevision sha256:"+std::string(64,'a')+"\nclear_vehicle "+id(2).to_string()+"\n");
    require(request.request && tools::world_authoring::apply(w.snapshot(),request.request->transaction),"vehicle file edit parsed");
    auto legacy=world::encode_world_snapshot(load().snapshot()).bytes;
    // Strip sparse vehicle section to produce a valid v12 fixture, retaining chassis/materials.
    legacy.resize(legacy.size() - 146);
    legacy[8] = std::byte{12};
    rehash(legacy);
    const auto v12=world::decode_world_snapshot(legacy);require(v12.snapshot && !v12.snapshot->entities.back().vehicle,"v12 has no invented vehicle");
    auto truncated=bytes.bytes;truncated.pop_back();rehash(truncated);require(!world::decode_world_snapshot(truncated),"truncated vehicle record rejected");
    auto clean=load();const auto victim=*clean.find(id(2));require(clean.clear_rigid_box(victim) && !clean.vehicle(victim),"removing chassis clears dependent vehicle");
    clean=load();require(clean.clear_spatial_state(*clean.find(id(2))) && !clean.vehicle(*clean.find(id(2))),"removing spatial state clears vehicle");
    clean=load();require(clean.destroy_entity(*clean.find(id(2))) && clean.create_entity(id(2)).has_value() && !clean.vehicle(*clean.find(id(2))),"recycled identity does not retain vehicle");
    auto module=runtime::load_game_module(VICE_VEHICLE_TEST_MODULE);require(bool(module),"actual SDK vehicle module");project::Manifest manifest;
#if defined(VICE_VEHICLE_PHYSICS)
    using C=rigid_body::VehicleControlResult;
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count=1}};rigid_body::AuthoredSimulation physics;
    require(physics.reset(w,jobs) && physics.vehicle_count()==1,"shared vehicle bootstrap");ticks(physics,240);
    const auto settled=physics.vehicle(id(2));
    require(settled && std::all_of(settled->wheels.begin(),settled->wheels.end(),[](const auto& wheel){return wheel.grounded && wheel.supporting_entity==id(1) && wheel.material==id(100) && wheel.normal_force>1000;}),"four real suspension contacts with persistent road/material identities");
    const auto height=w.spatial_state(car)->global_position.y;require(height>50.6 && height<50.85 && std::abs(w.rigid_box(car)->linear_velocity.y)<0.05F,"suspension holds physical chassis at large origin");
    const auto before=*w.vehicle(car);require(physics.control_vehicle(id(2),2,0,0)==C::invalid_argument && w.vehicle(car)==before,"invalid control is atomic");
    runtime::GameModuleLifecycle lifecycle{*module.module,{manifest,w,&physics}};
    require(lifecycle.start(),"SDK size, missing component, old-host and wrong-thread guards");
    lifecycle.event(platform::Event{platform::KeyEvent{.key=platform::Key::up,.pressed=true}});ticks(physics,120);
    render::Color color;lifecycle.frame(color);require(color.red==1 && color.green==1 && color.blue==1,"SDK throttle drives physical wheels and read-only frame rejects control");lifecycle.stop();
    require(w.vehicle(car)->distance>1 && w.spatial_state(car)->global_position.z>1 && physics.vehicle(id(2))->forward_speed>3,"tyre force accelerates real chassis");
    const auto snapshot=w.snapshot();world::World resumed;require(resumed.restore(snapshot),"live vehicle checkpoint");rigid_body::AuthoredSimulation continuation;require(continuation.reset(resumed,jobs),"reconstruct vehicle without backend handles");
    require(resumed.vehicle(*resumed.find(id(2)))==w.vehicle(car),"controls and odometer survive checkpoint exactly");ticks(physics,30);ticks(continuation,30);
    require(std::abs(resumed.spatial_state(*resumed.find(id(2)))->global_position.z-w.spatial_state(car)->global_position.z)<0.05 && std::abs(continuation.vehicle(id(2))->forward_speed-physics.vehicle(id(2))->forward_speed)<0.1F,"vehicle resumes moving with saved velocity and controls");
    const float speed=physics.vehicle(id(2))->forward_speed;require(physics.control_vehicle(id(2),0,0,1)==C::applied,"brake input");ticks(physics,180);
    require(std::abs(physics.vehicle(id(2))->forward_speed)<0.1F && speed>3,"braking stops without driving backward");
    require(physics.control_vehicle(id(2),-1,0,0)==C::applied,"reverse input");ticks(physics,120);require(physics.vehicle(id(2))->forward_speed<-3,"reverse uses tyre traction");
    // Road coefficient controls actual acceleration; zero-grip road supports but cannot drive.
    float speeds[3]{};unsigned index=0;
    for(float friction:{1.0F,0.15F,0.0F}) {
        auto road=load(friction);rigid_body::AuthoredSimulation sim;require(sim.reset(road,jobs),"surface fixture");ticks(sim,120);
        require(sim.control_vehicle(id(2),1,0,0)==C::applied,"surface throttle");ticks(sim,120);speeds[index++]=sim.vehicle(id(2))->forward_speed;
        for(const auto& wheel:sim.vehicle(id(2))->wheels)require(std::hypot(wheel.longitudinal_force,wheel.lateral_force)<=friction*wheel.normal_force+0.01F,"friction circle is bounded by measured normal load");
    }
    require(speeds[0]>3 && speeds[1]>0.5F && speeds[0]>speeds[1]*2 && std::abs(speeds[2])<0.01F,"dry, slippery and zero-grip surfaces change native acceleration");
    auto turning=load();rigid_body::AuthoredSimulation turn;require(turn.reset(turning,jobs),"steering fixture");ticks(turn,120);require(turn.control_vehicle(id(2),0.5F,0.5F,0)==C::applied,"steer input");ticks(turn,360);
    require(turning.spatial_state(*turning.find(id(2)))->global_position.x>1e12+0.3 && std::abs(turning.authored_transform(*turning.find(id(2)))->rotation.y)>0.05F,"front tyre steering produces translation and yaw");
    auto flying=load();require(flying.set_spatial_state(*flying.find(id(2)),{{1e12,60,0},{1}}),"airborne start");rigid_body::AuthoredSimulation air;require(air.reset(flying,jobs) && air.control_vehicle(id(2),1,1,0)==C::applied,"airborne throttle");ticks(air,30);
    const auto airborne=*air.vehicle(id(2));
    require(std::abs(airborne.forward_speed)<0.01 && std::none_of(airborne.wheels.begin(),airborne.wheels.end(),[](const auto& wheel){return wheel.grounded;}),"airborne wheels invent no drive force");
    auto rotated=load();world::AuthoredTransform heading;heading.rotation={0,std::sqrt(0.5F),0,std::sqrt(0.5F)};
    require(rotated.set_authored_transform(*rotated.find(id(2)),heading),"rotated physical frame");rigid_body::AuthoredSimulation orientation;
    require(orientation.reset(rotated,jobs),"rotated chassis bootstrap");ticks(orientation,120);require(orientation.control_vehicle(id(2),1,0,0)==C::applied,"local forward throttle");ticks(orientation,120);
    require(rotated.spatial_state(*rotated.find(id(2)))->global_position.x>1e12+1 && std::abs(rotated.spatial_state(*rotated.find(id(2)))->global_position.z)<0.1,"suspension and tyres follow chassis orientation");
    auto collision=load();const auto wall=*collision.create_entity(id(7));world::RigidBox obstacle;obstacle.motion=world::RigidMotion::static_body;obstacle.half_extents={10,2,0.5F};
    require(collision.set_spatial_state(wall,{{1e12,52,8},{1}}) && collision.set_rigid_box(wall,obstacle),"physical wall");rigid_body::AuthoredSimulation crash;
    require(crash.reset(collision,jobs) && crash.control_vehicle(id(2),1,0,0)==C::applied,"drive into wall");ticks(crash,480);
    require(collision.spatial_state(*collision.find(id(2)))->global_position.z<7 && collision.spatial_state(*collision.find(id(2)))->global_position.z>3,"real chassis collision prevents passing through wall");
    auto compound=load();const auto anchor=*compound.create_entity(id(3));world::RigidBox fixed;fixed.motion=world::RigidMotion::static_body;fixed.half_extents={0.1F,0.1F,0.1F};
    require(compound.set_spatial_state(anchor,{{1e12,50.8,-3},{1}}) && compound.set_rigid_box(anchor,fixed),"composed anchor");
    for(unsigned n=4;n<=6;++n)require(compound.create_entity(id(n)).has_value(),"composed identities");
    world::DistanceLink tether;tether.first=id(3);tether.second=id(2);tether.min_distance=tether.max_distance=3;tether.break_threshold=0.001F;
    require(compound.set_distance_link(*compound.find(id(4)),tether) && compound.set_dependency(*compound.find(id(5)),{}) && compound.set_dependency(*compound.find(id(6)),{id(5),id(4),true}),"composed supply chain");
    rigid_body::AuthoredSimulation composition;require(composition.reset(compound,jobs) && composition.control_vehicle(id(2),1,0,0)==C::applied,"vehicle coexists with physical consequences");ticks(composition,120);
    require(compound.distance_link(*compound.find(id(4)))->broken && !compound.dependency_available(*compound.find(id(6))) && compound.vehicle(*compound.find(id(2)))->distance>0,"vehicle force breaks tether and cuts supply");
    const auto composed_save=world::encode_world_snapshot(compound.snapshot());world::World composed_resume;
    require(bool(composed_save) && composed_resume.restore(*world::decode_world_snapshot(composed_save.bytes).snapshot),"save composed vehicle/history");rigid_body::AuthoredSimulation composed_runtime;
    const auto saved_history=compound.causal_snapshot().events;require(composed_runtime.reset(composed_resume,jobs),"resume vehicle and broken tether");ticks(composed_runtime,1);
    require(!composed_resume.dependency_available(*composed_resume.find(id(6))) && composed_resume.vehicle(*composed_resume.find(id(2)))->distance>=compound.vehicle(*compound.find(id(2)))->distance &&
        std::all_of(saved_history.begin(),saved_history.end(),[&](const auto& e){return composed_resume.causal_event(e.sequence)==e;}),"vehicle resume preserves original causal failure evidence");
    const auto debug=runtime::sample_runtime_debug_entity(w,id(2),&physics);require(debug.vehicle && debug.vehicle_state && runtime::serialize_runtime_debug_entity_snapshot(debug,"vehicle").find("normal_force")!=std::string::npos,"Runtime debugging exposes physical wheel evidence");
    const auto distance=w.vehicle(car)->distance;require(w.set_region_fidelity({1},world::RegionFidelity::inactive) && physics.control_vehicle(id(2),0,0,0)==C::inactive_region,"inactive controls rejected");ticks(physics,30);require(w.vehicle(car)->distance==distance && !physics.vehicle(id(2))->wheels[0].grounded,"inactive vehicle consumes no physical work");
    require(w.set_region_fidelity({1},world::RegionFidelity::reduced) && physics.control_vehicle(id(2),0,0,0)==C::applied,"reduced vehicle uses same physical laws");ticks(physics,1);
    auto changed=*w.vehicle(car);changed.spring*=1.1F;require(w.set_vehicle(car,changed),"external tuning edit");const auto step=physics.coordinator().step_index();
    require(!physics.step() && physics.coordinator().step_index()==step && physics.control_vehicle(id(2),1,0,0)==C::reset_required && !physics.vehicle(id(2)),"stale profile rejected before advance or control/read");
    require(physics.reset(w,jobs),"rebuild tuning");const auto generation=w.vehicle_revision();require(w.restore(w.snapshot()) && w.vehicle_revision()!=generation && !physics.step(),"World replacement invalidates cached vehicle state");
    auto crossing=load().snapshot();crossing.regions[0].bounds=world::RegionBounds{{-100,-100,-100},{100,100,1}};
    auto next_region=crossing.regions[0];next_region.id={2};next_region.simulation_island={20};next_region.origin.z=10;next_region.bounds=world::RegionBounds{{-100,-100,-9},{100,100,100}};crossing.regions.push_back(next_region);
    auto next_road=crossing.entities.front();next_road.persistent_id=id(7);next_road.spatial_state->region={2};next_road.spatial_state->global_position.z=10;crossing.entities.push_back(next_road);
    world::World traveling;require(traveling.restore(crossing),"ready vehicle regions");rigid_body::AuthoredSimulation travel;
    require(travel.reset(traveling,jobs) && travel.control_vehicle(id(2),1,0,0)==C::applied,"drive toward region boundary");ticks(travel,240);
    const auto transferred=travel.vehicle(id(2));
    require(transferred && traveling.spatial_state(*traveling.find(id(2)))->region==world::RegionId{2} && transferred->state.throttle==1 && transferred->state.distance>3 && transferred->wheels[0].supporting_entity==id(7),"region handoff preserves vehicle identity/controls/distance and rebuilds wheel support");
    auto support_edit=load();rigid_body::AuthoredSimulation support_sim;require(support_sim.reset(support_edit,jobs),"support edit fixture");ticks(support_sim,120);
    require(support_edit.set_spatial_state(*support_edit.find(id(1)),{{1e12,45,0},{1}}) && !support_sim.vehicle(id(2)),"vehicle observation rejects externally moved support");
    auto surface_edit=load();rigid_body::AuthoredSimulation surface_sim;require(surface_sim.reset(surface_edit,jobs),"surface edit fixture");
    auto surface=*surface_edit.material(id(100));surface.friction=0.2F;require(surface_edit.set_material(surface) && surface_sim.control_vehicle(id(2),1,0,0)==C::reset_required && !surface_sim.vehicle(id(2)) && !surface_sim.step(),"material edits cannot leave vehicle grip stale");
    auto addition=load();rigid_body::AuthoredSimulation adding;require(adding.reset(addition,jobs),"addition fixture");require(addition.clear_vehicle(*addition.find(id(2))) && !adding.step(),"live vehicle removal requires reset");
    // Dynamic support receives equal and opposite suspension/traction impulses.
    auto raft=load();auto floor=*raft.rigid_box(*raft.find(id(1)));floor.motion=world::RigidMotion::dynamic_body;require(raft.set_rigid_box(*raft.find(id(1)),floor),"dynamic supporting body");
    rigid_body::AuthoredSimulation reaction;require(reaction.reset(raft,jobs) && reaction.control_vehicle(id(2),1,0,0)==C::applied,"reaction bootstrap");ticks(reaction,1);
    require(raft.rigid_box(*raft.find(id(1)))->linear_velocity.z<0 && raft.rigid_box(*raft.find(id(2)))->linear_velocity.z>0,"tyre reaction transfers opposite momentum to dynamic support");
#else
    runtime::GameModuleLifecycle lifecycle{*module.module,{manifest,w}};require(lifecycle.start(),"backend-disabled SDK reports unavailable");lifecycle.stop();
#endif
#if defined(VICE_VEHICLE_EDITOR)
    auto authored=load();const auto original=tools::world_cooker::serialize_source_manifest(authored.snapshot());editor::PreviewRuntime preview;
    const editor::PreviewGameConfig config{manifest,VICE_VEHICLE_TEST_MODULE};std::string diagnostics;
    require(preview.reset(authored,jobs,editor::AssetIndex{},&diagnostics,&config),"preview same SDK vehicle module");
    for(unsigned i=0;i<120;++i)require(preview.step_once(),"preview settles");preview.play();preview.set_input_focus(true);
    require(preview.handle_event(platform::Event{platform::KeyEvent{.key=platform::Key::up,.pressed=true}}),"preview drives through module input");preview.pause();
    for(unsigned i=0;i<120;++i)require(preview.step_once(),"preview drives");
    require(preview.snapshot().vehicles.size()==1 && preview.snapshot().vehicles[0].vehicle.wheels[0].grounded,"preview overlay exposes wheel load evidence");
    require(preview.clear_color().red==1 && preview.clear_color().green==1 && preview.world()->vehicle(*preview.world()->find(id(2)))->distance>1,"preview uses same vehicle simulation");
    require(tools::world_cooker::serialize_source_manifest(authored.snapshot())==original,"preview leaves authoring intact");
    auto record=authored.snapshot().entities.back();editor::CommandHistory history;
    require(history.record(editor::CommandHistory::CreateEntityEntry{record,{},"Vehicle"}) && history.undo(authored) && history.redo(authored) && authored.vehicle(*authored.find(id(2)))==record.vehicle,"Editor undo/redo recreates chassis before vehicle");
#endif
    std::cout<<"Authored vehicle contracts passed\n";
}
