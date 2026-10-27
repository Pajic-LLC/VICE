#include <vice/runtime/module_api.hpp>
#include <cmath>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(std::uint8_t n){EntityId id{};id.bytes[0]=0x9e;id.bytes[6]=0x40;id.bytes[8]=0x80;id.bytes[15]=n;return id;}
class MaterialGame final:public Game {
    double elapsed_=0;bool driven_=false;
public:
    bool on_start(Context& c) override {
        DistanceLinkSnapshot link;
        if(c.read_distance_link(entity(8),link)!=Result::success)return false;
        driven_=link.broken!=0;
        MaterialSnapshot material;
        return c.read_material(entity(2),material)==Result::success && material.has_native_mass &&
            std::abs(material.mass-material.native_mass)<0.01*material.mass;
    }
    void on_update(Context& c,double delta) override {
        elapsed_+=delta;
        if(!driven_ && elapsed_>=1.5)driven_=c.set_body_velocity(entity(7),{0,-8,0})==Result::success;
    }
    void on_frame(Context& c,Frame& frame) override {
        MaterialSnapshot material;
        if(c.read_material(entity(2),material)==Result::success && material.friction==0)
            frame.clear_color={0.02F,0.03F,0.08F,1};
    }
};
}
VICE_GAME_MODULE(MaterialGame)
