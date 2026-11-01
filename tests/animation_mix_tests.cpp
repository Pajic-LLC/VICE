#include "presentation_fixture.hpp"
#include <limits>
int main() {
 using namespace fixture;
 presentation::Rig r;r.id={rig};
 r.nodes={{"parent",-1,{},{}},{"child",0,{{1,0,0},{},1},{}}};
 presentation::Clip a{"a",1,{}},b{"b",1,{}};
 presentation::Track track;track.node=0;track.path=presentation::Path::rotation;
 track.keys.push_back({0,{0,0,.70710678F,.70710678F},{},{}});b.tracks.push_back(track);r.clips={a,b};
 require(presentation::valid(r),"valid two-joint source");
 std::vector<presentation::Pose> pose;
 require(presentation::sample_blended_pose(r,0,0,1,0,.5F,pose),"sample midpoint");
 require(std::abs(pose[1].translation.x-.70710678F)<1e-5F &&
         std::abs(pose[1].translation.y-.70710678F)<1e-5F,
         "local blend preserves the child bone length");
 const auto previous=pose[1].translation;
 require(!presentation::sample_blended_pose(r,0,0,1,0,std::numeric_limits<float>::quiet_NaN(),pose)&&
         pose[1].translation.x==previous.x,"bad weight preserves output");
 Data f;presentation::System system;std::string d;
 require(system.reset(f.world,f.workspace.cooked_root,&d),"start shared service");
 require(system.control_animation_mix(actor,0,1,.6F,1.25F),"control mix");
 require(system.advance(.2,&d),"advance both clocks");
 const auto entity=*f.world.find(actor);const auto state=*f.world.animation(entity);
 require(state.blend_weight==.6F && state.blend_clip==1 && state.blend_time>.24,
         "blend intent and clock are World truth");
 const auto saved=world::encode_world_snapshot(f.world.snapshot());
 const auto decoded=world::decode_world_snapshot(saved.bytes);world::World resumed;
 require(bool(decoded.snapshot)&&resumed.restore(*decoded.snapshot),"decode mixed checkpoint");
 require(resumed.animation(*resumed.find(actor))==state,"exact mix clocks survive resume");
 presentation::System restored;require(restored.reset(resumed,f.workspace.cooked_root,&d),"restore service");
 require(!restored.control_animation_mix(actor,0,63,.6F,1),"reject missing clip");
 require(resumed.animation(*resumed.find(actor))==state,"invalid command leaves state intact");
 require(restored.control_animation(actor,2,1,true,true,false),"ordinary control remains compatible");
 require(resumed.animation(*resumed.find(actor))->blend_weight==0,"single clip clears blend");
 return 0;
}
