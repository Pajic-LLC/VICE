#include <vice/runtime/module_api.hpp>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(unsigned n){EntityId id{};id.bytes[0]=0xa1;id.bytes[6]=0x40;id.bytes[8]=0x80;id.bytes[15]=static_cast<std::uint8_t>(n);return id;}
class VehicleGame final:public Game {
    unsigned selected_=2;bool manual_=false,up_=false,down_=false,left_=false,right_=false,brake_=false;
public:
    bool on_start(Context& c) override {
        VehicleSnapshot v;const auto r=c.read_vehicle(entity(2),v);
        return r==Result::success || r==Result::unavailable;
    }
    void on_event(Context& c,const Event& e) override {
        if(e.kind==EventKind::window_focus_changed && !(e.flags&1U)) {
            up_=down_=left_=right_=brake_=false;
            if(manual_)(void)c.control_vehicle(entity(selected_),0,0,1);
        }
        if(e.kind!=EventKind::key)return;
        const auto key=static_cast<Key>(e.code);const bool pressed=(e.flags&1U)!=0;
        if(pressed && (key==Key::digit_1 || key==Key::digit_2 || key==Key::digit_3)) {
            (void)c.control_vehicle(entity(selected_),0,0,1);
            selected_=key==Key::digit_1?2U:(key==Key::digit_2?3U:4U);manual_=true;
        }
        if(key==Key::up){up_=pressed;manual_=true;}
        if(key==Key::down){down_=pressed;manual_=true;}
        if(key==Key::left){left_=pressed;manual_=true;}
        if(key==Key::right){right_=pressed;manual_=true;}
        if(key==Key::space){brake_=pressed;manual_=true;}
    }
    void on_update(Context& c,double) override {
        // The authored nonzero throttles provide a bounded automatic acceptance tour.
        // Save/restart uses the World odometer rather than unsaved module timers.
        for(unsigned n=2;n<=4;++n) {
            VehicleSnapshot v;if(c.read_vehicle(entity(n),v)!=Result::success)continue;
            if(manual_ && n==selected_)(void)c.control_vehicle(entity(n),static_cast<float>(up_)-static_cast<float>(down_),static_cast<float>(right_)-static_cast<float>(left_),brake_?1.0F:0.0F);
            else if(v.distance>=20)(void)c.control_vehicle(entity(n),0,0,1);
        }
    }
    void on_frame(Context&,Frame& f) override {f.clear_color={0.03F,0.06F,0.09F,1};}
};
}
VICE_GAME_MODULE(VehicleGame)
