#include <vice/runtime/module_api.hpp>
#include <thread>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(std::uint8_t n){EntityId id{};id.bytes[0]=0x9c;id.bytes[6]=0x40;id.bytes[8]=0x80;id.bytes[15]=n;return id;}
bool same(EntityId a,EntityId b){for(unsigned i=0;i<16;++i)if(a.bytes[i]!=b.bytes[i])return false;return true;}
class CausalGame final:public Game {
    bool valid_=true,retention_=false;
public:
    bool on_start(Context& c) override {
        CausalHistorySnapshot h;CausalEventSnapshot e;
        valid_=c.read_causal_history(h)==Result::success && h.capturing && h.next_sequence==2 && h.capacity==1024 &&
            c.read_causal_event(h.history_id,1,e)==Result::success && e.kind==CausalKind::capture_started && same(h.capture_id,e.id);
        valid_ &= c.read_causal_event(entity(99),1,e)==Result::history_mismatch && c.read_causal_event(h.history_id,0,e)==Result::invalid_argument && c.read_causal_event(h.history_id,2,e)==Result::not_found;
        e.size=0;valid_ &= c.read_causal_event(h.history_id,1,e)==Result::invalid_argument;
        h.size=0;valid_ &= c.read_causal_history(h)==Result::invalid_argument;
        valid_ &= c.set_dependency_enabled(entity(99),true)==Result::not_found && c.set_dependency_enabled(entity(1),true)==Result::no_dependency;
        Result result=Result::success;std::thread worker{[&]{CausalHistorySnapshot info;result=c.read_causal_history(info);}};worker.join();
        return valid_ && result==Result::wrong_phase;
    }
    void on_event(Context& c,const Event& e) override {
        if(e.kind!=EventKind::key || !(e.flags&1U))return;
        switch(static_cast<Key>(e.code)) {
        case Key::space:valid_ &= c.set_body_velocity(entity(2),{0,-8,0})==Result::success;break;
        case Key::left:valid_ &= c.set_dependency_enabled(entity(4),false)==Result::success;break;
        case Key::right:valid_ &= c.set_dependency_enabled(entity(4),true)==Result::success;break;
        case Key::up:retention_=true;break;
        default:break;
        }
    }
    void on_frame(Context& c,Frame& f) override {
        CausalHistorySnapshot h;valid_ &= c.read_causal_history(h)==Result::success;
        bool chain=false;
        if(retention_) {CausalEventSnapshot e;chain=h.retained_count==1024 && c.read_causal_event(h.history_id,1,e)==Result::event_pruned;}
        else {
            CausalEventSnapshot lamp,harness,impact;
            chain=c.read_causal_event(h.history_id,4,lamp)==Result::success && c.read_causal_event(h.history_id,lamp.parent_sequence,harness)==Result::success &&
                c.read_causal_event(h.history_id,harness.parent_sequence,impact)==Result::success && lamp.subject.bytes[15]==6 && harness.subject.bytes[15]==5 &&
                impact.kind==CausalKind::constraint_broken && impact.origin==CausalOrigin::physics && impact.quantity_kind==CausalQuantity::linear_impulse && impact.quantity>1000 && impact.solver_step>0 &&
                same(lamp.parent_id,harness.id) && same(harness.parent_id,impact.id) && same(impact.capture_id,h.capture_id);
        }
        const bool guard=c.set_dependency_enabled(entity(4),false)==Result::wrong_phase;
        f.clear_color={valid_ && chain?1.0F:0.0F,h.next_sequence>=11?1.0F:0.0F,guard?1.0F:0.0F,1};
    }
};
}
VICE_GAME_MODULE(CausalGame)
