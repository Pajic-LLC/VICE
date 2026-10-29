#include <vice/runtime/module_api.hpp>
#include <limits>
#include <thread>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(std::uint8_t n){EntityId id{};id.bytes[0]=0xa1;id.bytes[6]=0x40;id.bytes[8]=0x80;id.bytes[15]=n;return id;}
class VehicleGame final:public Game {
    bool valid_=true,available_=false;
public:
    bool on_start(Context& c) override {
        VehicleSnapshot v;
        const auto read=c.read_vehicle(entity(2),v);available_=read==Result::success;
        valid_=available_ || read==Result::unavailable;
        valid_ &= c.read_vehicle(entity(1),v)==Result::no_vehicle && c.read_vehicle(entity(99),v)==Result::not_found;
        v.size=0;valid_ &= c.read_vehicle(entity(2),v)==Result::invalid_argument;
        valid_ &= c.control_vehicle(entity(2),2,0,0)==Result::invalid_argument &&
            c.control_vehicle(entity(2),0,std::numeric_limits<float>::quiet_NaN(),0)==Result::invalid_argument;
        Result r=Result::success;
        std::thread worker{[&]{VehicleSnapshot o;r=c.read_vehicle(entity(2),o);}};worker.join();valid_ &= r==Result::wrong_phase;
        HostApi old;old.size=static_cast<std::uint32_t>(offsetof(HostApi,read_vehicle));Context old_context{&old};
        valid_ &= old_context.read_vehicle(entity(2),v)==Result::unavailable && old_context.control_vehicle(entity(2),0,0,0)==Result::unavailable;
        return valid_;
    }
    void on_event(Context& c,const Event& e) override {
        if(e.kind==EventKind::key && (e.flags&1U))valid_ &= c.control_vehicle(entity(2),1,0,0)==(available_?Result::success:Result::unavailable);
    }
    void on_frame(Context& c,Frame& f) override {
        VehicleSnapshot v;const auto r=c.read_vehicle(entity(2),v);
        valid_ &= c.control_vehicle(entity(2),0,0,1)==Result::wrong_phase;
        f.clear_color={valid_?1.0F:0.0F,r==Result::success && v.forward_speed>0.1F?1.0F:0.0F,r==Result::success && v.wheels[0].grounded?1.0F:0.0F,1};
    }
};
}
VICE_GAME_MODULE(VehicleGame)
