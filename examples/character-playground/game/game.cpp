#include <vice/runtime/module_api.hpp>
#include <cmath>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(std::uint8_t last) { EntityId id{}; id.bytes[0]=0x9b; id.bytes[6]=0x40; id.bytes[8]=0x80; id.bytes[14]=2; id.bytes[15]=last; return id; }
class Playground final : public Game {
    bool left_=false,right_=false,forward_=false,backward_=false,jump_=false,stress_=false;
    bool automatic_=false,saw_air_=false;
    int phase_=0;
    double timer_=0,start_x_=0,start_y_=0,previous_x_=0;
    float platform_direction_=1;
public:
    bool on_start(Context& context) override {
        CharacterSnapshot c;
        if (context.read_character(entity(2),c)!=Result::success) return false;
        start_x_=previous_x_=c.position[0]; start_y_=c.position[1];
        // Authored nonzero desired motion opts this sample into its acceptance tour.
        automatic_=c.horizontal_velocity.x>0;
        context.log(LogLevel::info,"Capsule Playground: arrows move, Space jumps, B stresses the physical supply gate.");
        return true;
    }
    void on_event(Context&,const Event& event) override {
        if (event.kind==EventKind::window_focus_changed && !(event.flags&1U)) left_=right_=forward_=backward_=jump_=stress_=false;
        if (event.kind!=EventKind::key) return;
        const bool down=(event.flags&1U)!=0;
        if (down) automatic_=false;
        switch(static_cast<Key>(event.code)) {
        case Key::left: left_=down; break; case Key::right: right_=down; break;
        case Key::up: forward_=down; break; case Key::down: backward_=down; break;
        case Key::space: if(down && !(event.flags&2U)) jump_=true; break;
        case Key::b: if(down && !(event.flags&2U)) stress_=true; break;
        default: break;
        }
    }
    void on_update(Context& context,double delta) override {
        CharacterSnapshot c;
        if(context.read_character(entity(2),c)!=Result::success) return;
        Vector3 horizontal{(static_cast<float>(right_)-static_cast<float>(left_))*4,0,(static_cast<float>(backward_)-static_cast<float>(forward_))*4};
        float jumping=jump_ ? 5.0F : 0.0F;
        if(automatic_) {
            horizontal={}; jumping=0;
            switch(phase_) {
            case 0:
                horizontal.x=2;
                if(c.position[0]>start_x_+1.15 && c.position[1]>start_y_+0.15 && c.ground_state==CharacterGroundState::grounded) { phase_=1;timer_=0;horizontal={}; }
                break;
            case 1: timer_+=delta; if(timer_>0.9) { phase_=2;timer_=0; } break;
            case 2:
                horizontal.x=2;
                if(c.position[0]>start_x_+3.2 && std::abs(c.position[0]-previous_x_)<0.002 && c.ground_state==CharacterGroundState::grounded) timer_+=delta; else timer_=0;
                if(timer_>0.65) { phase_=3;timer_=0;horizontal={}; }
                break;
            case 3:
                timer_+=delta;
                if(timer_>0.9 && c.ground_state==CharacterGroundState::grounded) { jumping=5;phase_=4; }
                break;
            case 4:
                if(c.ground_state!=CharacterGroundState::grounded && c.position[1]>start_y_+0.2) saw_air_=true;
                if(saw_air_ && c.ground_state==CharacterGroundState::grounded && c.position[1]<start_y_+0.05) { phase_=5;stress_=true; }
                break;
            default: break;
            }
        }
        if(context.control_character(entity(2),horizontal,jumping)==Result::not_grounded) (void)context.control_character(entity(2),horizontal);
        if(stress_) (void)context.set_body_velocity(entity(7),{0,-8,0});
        jump_=stress_=false; previous_x_=c.position[0];
        EntitySnapshot mover;
        if(context.read_entity(entity(5),mover)==Result::success) {
            if(mover.position[0]>start_x_-2) platform_direction_=-1;
            if(mover.position[0]<start_x_-6) platform_direction_=1;
            (void)context.set_body_velocity(entity(5),{platform_direction_,0,0});
        }
    }
    void on_frame(Context& context,Frame& frame) override {
        DependencySnapshot lamp,independent;
        if(context.read_dependency(entity(0x11),lamp)==Result::success && context.read_dependency(entity(0x12),independent)==Result::success && !lamp.available && independent.available)
            frame.clear_color={0.02F,0.03F,0.08F,1};
    }
};
}
VICE_GAME_MODULE(Playground)
