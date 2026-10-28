#include <vice/runtime/module_api.hpp>
#include <algorithm>
#include <cmath>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(unsigned n){EntityId id{};id.bytes[0]=0xa2;id.bytes[6]=0x40;id.bytes[8]=0x80;id.bytes[15]=static_cast<std::uint8_t>(n);return id;}
class InteractionGame final:public Game {
    bool automatic_=false,up_=false,down_=false,left_=false,right_=false,brake_=false;
    float yaw_=0,pitch_=-0.35F;
    void camera(Context& c,const EntitySnapshot& target,bool carried,const EntitySnapshot& car) {
        const float heading=carried?std::atan2(2*(car.rotation[3]*car.rotation[1]+car.rotation[0]*car.rotation[2]),1-2*(car.rotation[1]*car.rotation[1]+car.rotation[0]*car.rotation[0])):0;
        const float angle=heading+yaw_;
        CameraPose pose;pose.position[0]=target.position[0]-6*std::sin(angle);pose.position[1]=target.position[1]+3;pose.position[2]=target.position[2]-6*std::cos(angle);
        const float sy=std::sin((angle+3.141592654F)*0.5F),cy=std::cos((angle+3.141592654F)*0.5F),sp=std::sin(pitch_*0.5F),cp=std::cos(pitch_*0.5F);
        pose.rotation[0]=cy*sp;pose.rotation[1]=sy*cp;pose.rotation[2]=-sy*sp;pose.rotation[3]=cy*cp;
        (void)c.set_camera_pose(entity(4),pose);
    }
public:
    bool on_start(Context& c) override {
        CharacterSnapshot character;CharacterAttachmentSnapshot a;
        if(c.read_character_attachment(entity(3),a)!=Result::success)return false;
        if(c.read_character(entity(3),character)==Result::success)automatic_=a.attached || character.horizontal_velocity.x>0;
        return true;
    }
    void on_event(Context& c,const Event& e) override {
        if(e.kind==EventKind::window_focus_changed && !(e.flags&1U)) {
            up_=down_=left_=right_=brake_=false;
            (void)c.control_vehicle(entity(2),0,0,1);
            (void)c.control_character(entity(3),{});
        }
        if(e.kind==EventKind::mouse_move){yaw_+=static_cast<float>(e.delta_x)*0.003F;pitch_=std::clamp(pitch_+static_cast<float>(e.delta_y)*0.002F,-1.1F,-0.1F);}
        if(e.kind!=EventKind::key)return;
        const auto key=static_cast<Key>(e.code);const bool pressed=(e.flags&1U)!=0;
        if(key==Key::e && pressed) {
            automatic_=false;CharacterAttachmentSnapshot a;(void)c.read_character_attachment(entity(3),a);
            const auto r=a.attached?c.detach_character(entity(3),{2,-0.5F,0},1,0.5F):c.attach_character(entity(3),entity(2),{0,-0.4F,0},3);
            if(r==Result::blocked)c.log(LogLevel::warning,"Exit blocked: stop the car and choose clear ground.");
            if(r==Result::out_of_range)c.log(LogLevel::warning,"Move closer to the car before entering.");
        }
        if(key==Key::up){up_=pressed;automatic_=false;}if(key==Key::down){down_=pressed;automatic_=false;}
        if(key==Key::left){left_=pressed;automatic_=false;}if(key==Key::right){right_=pressed;automatic_=false;}
        if(key==Key::space){brake_=pressed;automatic_=false;}
    }
    void on_update(Context& c,double) override {
        CharacterAttachmentSnapshot attachment;EntitySnapshot player,car;VehicleSnapshot vehicle;
        if(c.read_character_attachment(entity(3),attachment)!=Result::success || c.read_entity(entity(3),player)!=Result::success || c.read_entity(entity(2),car)!=Result::success)return;
        const bool mounted=attachment.attached!=0;
        if(automatic_ && c.read_vehicle(entity(2),vehicle)==Result::success) {
            if(!mounted && vehicle.distance<10) {
                if(player.position[0]<car.position[0]-1.5)(void)c.control_character(entity(3),{0.8F,0,0});
                else (void)c.attach_character(entity(3),entity(2),{0,-0.4F,0},3);
            } else if(mounted && vehicle.distance<10)(void)c.control_vehicle(entity(2),0.7F,0,0);
            else if(mounted) {
                (void)c.control_vehicle(entity(2),0,0,1);
                if(std::abs(vehicle.forward_speed)<0.15F && c.detach_character(entity(3),{2,-0.5F,0},1,0.5F)==Result::success)automatic_=false;
            }
        } else if(mounted)(void)c.control_vehicle(entity(2),static_cast<float>(up_)-static_cast<float>(down_),static_cast<float>(right_)-static_cast<float>(left_),brake_?1.0F:0.0F);
        else {
            const float forward=static_cast<float>(up_)-static_cast<float>(down_),side=static_cast<float>(right_)-static_cast<float>(left_);
            (void)c.control_character(entity(3),{2*(forward*std::sin(yaw_)+side*std::cos(yaw_)),0,2*(forward*std::cos(yaw_)-side*std::sin(yaw_))},brake_?5.0F:0.0F);
        }
        (void)c.read_entity(entity(3),player);camera(c,player,mounted,car);
    }
    void on_frame(Context&,Frame& frame) override {frame.clear_color={0.025F,0.045F,0.07F,1};}
};
}
VICE_GAME_MODULE(InteractionGame)
