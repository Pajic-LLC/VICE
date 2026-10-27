#include <vice/runtime/module_api.hpp>
#include <cmath>
#include <thread>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(std::uint8_t n){EntityId id{};id.bytes[0]=0x9e;id.bytes[6]=0x40;id.bytes[8]=0x80;id.bytes[15]=n;return id;}
class MaterialGame final:public Game {
    bool valid_=true;
public:
    bool on_start(Context& c) override {
        MaterialSnapshot ice,grip;
        valid_=c.read_material(entity(2),ice)==Result::success && c.read_material(entity(3),grip)==Result::success &&
            ice.id.bytes[14]==1 && ice.id.bytes[15]==2 && ice.friction==0 && ice.density==250 && ice.mass==250 && grip.mass==2000 && grip.friction==1;
        if(ice.has_native_mass)valid_ &= std::abs(ice.native_mass-250)<0.1F && std::abs(grip.native_mass-2000)<0.1F;
        MaterialSnapshot bad;bad.size=0;
        valid_ &= c.read_material(entity(2),bad)==Result::invalid_argument;
        valid_ &= c.read_material({},ice)==Result::invalid_argument && c.read_material(entity(99),ice)==Result::not_found;
        Result wrong=Result::success;std::thread worker{[&]{MaterialSnapshot m;wrong=c.read_material(entity(2),m);}};worker.join();
        return valid_ && wrong==Result::wrong_phase;
    }
    void on_frame(Context& c,Frame& frame) override {
        CausalHistorySnapshot history;
        bool materials=false;
        if(c.read_causal_history(history)==Result::success)for(auto sequence=history.first_sequence;sequence<history.next_sequence;++sequence) {
            CausalEventSnapshot e;CausalMaterialSnapshot pair;
            if(c.read_causal_event(history.history_id,sequence,e)==Result::success && e.kind==CausalKind::constraint_broken) {
                materials=c.read_causal_materials(history.history_id,sequence,pair)==Result::success &&
                    pair.first_material.bytes[14]==1 && pair.first_material.bytes[15]==3 && pair.second_material.bytes[15]==2;
                CausalMaterialSnapshot bad;bad.size=0;
                materials &= c.read_causal_materials(history.history_id,sequence,bad)==Result::invalid_argument &&
                    c.read_causal_materials(entity(99),sequence,pair)==Result::history_mismatch;
            }
        }
        frame.clear_color={valid_?1.0F:0.0F,materials?1.0F:0.0F,0,1};
    }
};
}
VICE_GAME_MODULE(MaterialGame)
