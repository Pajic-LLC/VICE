#include <vice/runtime/module_api.hpp>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(std::uint8_t last) {
    EntityId id{}; id.bytes[0]=0x97; id.bytes[6]=0x40; id.bytes[8]=0x80; id.bytes[14]=2; id.bytes[15]=last; return id;
}
class CausalHistoryGame final : public Game {
    double elapsed_=0, broken_at_=0;
    bool resumed_=false, driven_=false, disabled_=false, restored_=false;
public:
    bool on_start(Context& context) override {
        DistanceLinkSnapshot link;
        if(context.read_distance_link(entity(3),link)!=Result::success) return false;
        resumed_=link.broken!=0;
        CausalHistorySnapshot history;
        return context.read_causal_history(history)==Result::success && history.capturing;
    }
    void on_update(Context& context,double delta) override {
        elapsed_+=delta;
        if(resumed_) return;
        if(!driven_ && elapsed_>=1) driven_=context.set_body_velocity(entity(2),{1,-8,0})==Result::success;
        DistanceLinkSnapshot link;
        if(context.read_distance_link(entity(3),link)!=Result::success || !link.broken) return;
        if(broken_at_==0) broken_at_=elapsed_;
        if(!disabled_ && elapsed_-broken_at_>=2)
            disabled_=context.set_dependency_enabled(entity(0x21),false)==Result::success;
        if(disabled_ && !restored_ && elapsed_-broken_at_>=4)
            restored_=context.set_dependency_enabled(entity(0x21),true)==Result::success;
    }
    void on_frame(Context& context,Frame& frame) override {
        CausalHistorySnapshot history;
        if(context.read_causal_history(history)!=Result::success) return;
        // Bounded SDK history query proves physical evidence remains readable after restoration.
        for(auto sequence=history.first_sequence; sequence<history.next_sequence; ++sequence) {
            CausalEventSnapshot event;
            if(context.read_causal_event(history.history_id,sequence,event)==Result::success &&
                event.kind==CausalKind::constraint_broken && event.quantity>0)
                frame.clear_color={0.02F,0.03F,0.08F,1};
        }
    }
};
}
VICE_GAME_MODULE(CausalHistoryGame)
