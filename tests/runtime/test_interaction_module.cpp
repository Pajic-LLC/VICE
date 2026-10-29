#include <vice/runtime/module_api.hpp>
#include <thread>
#include <limits>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(std::uint8_t n){EntityId id{};id.bytes[0]=0xa2;id.bytes[6]=0x40;id.bytes[8]=0x80;id.bytes[15]=n;return id;}
class InteractionGame final:public Game {
    bool valid_=true;
public:
    bool on_start(Context& c) override {
        CameraPose pose;
        valid_=c.read_camera_pose(entity(4),pose)==Result::success && c.read_camera_pose(entity(2),pose)==Result::no_camera;
        const auto original=pose;pose.rotation[3]=2;valid_ &= c.set_camera_pose(entity(4),pose)==Result::invalid_argument;
        pose=original;pose.size=0;valid_ &= c.set_camera_pose(entity(4),pose)==Result::invalid_argument;
        CharacterAttachmentSnapshot a;valid_ &= c.read_character_attachment(entity(3),a)==Result::success && c.read_character_attachment(entity(2),a)==Result::no_character;
        a.size=0;valid_ &= c.read_character_attachment(entity(3),a)==Result::invalid_argument;
        Result other=Result::success;std::thread worker{[&]{CameraPose v;other=c.read_camera_pose(entity(4),v);}};worker.join();valid_ &= other==Result::wrong_phase;
        HostApi old;old.size=static_cast<std::uint32_t>(offsetof(HostApi,read_character_attachment));Context legacy{&old};
        valid_ &= legacy.read_camera_pose(entity(4),pose)==Result::unavailable && legacy.attach_character(entity(3),entity(2),{},3)==Result::unavailable;
        return valid_;
    }
    void on_event(Context& c,const Event& e) override {
        if(e.kind!=EventKind::key || !(e.flags&1U))return;
        if(e.code==static_cast<std::uint32_t>(Key::e)) {
            CharacterAttachmentSnapshot a;valid_ &= c.read_character_attachment(entity(3),a)==Result::success;
            const auto r=a.attached?c.detach_character(entity(3),{2,-0.5F,0},1,0.5F):c.attach_character(entity(3),entity(2),{0,-0.4F,0},3);
            valid_ &= r==Result::success || r==Result::unavailable;
        } else if(e.code==static_cast<std::uint32_t>(Key::up))valid_ &= c.control_vehicle(entity(2),1,0,0)==Result::success;
    }
    void on_update(Context& c,double) override {
        EntitySnapshot target;if(c.read_entity(entity(3),target)!=Result::success){valid_=false;return;}
        CameraPose pose;pose.position[0]=target.position[0];pose.position[1]=target.position[1]+3;pose.position[2]=target.position[2]-6;
        pose.rotation[1]=0.9847265F;pose.rotation[2]=0.1741081F;pose.rotation[3]=0;
        valid_ &= c.set_camera_pose(entity(4),pose)==Result::success;
    }
    void on_frame(Context& c,Frame& f) override {
        CameraPose pose;CharacterAttachmentSnapshot a;
        valid_ &= c.read_camera_pose(entity(4),pose)==Result::success && c.set_camera_pose(entity(4),pose)==Result::wrong_phase;
        valid_ &= c.attach_character(entity(3),entity(2),{},3)==Result::wrong_phase;
        valid_ &= c.read_character_attachment(entity(3),a)==Result::success;
        f.clear_color={valid_?1.0F:0.0F,a.attached?1.0F:0.0F,pose.position[2]>-5.5?1.0F:0.0F,1};
    }
};
}
VICE_GAME_MODULE(InteractionGame)
