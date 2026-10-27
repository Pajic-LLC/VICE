#include <vice/runtime/module_api.hpp>
#include <thread>
#include <limits>
namespace {
using namespace vice::runtime::module;
constexpr EntityId entity(std::uint8_t last) { EntityId id{}; id.bytes[0]=0x9a; id.bytes[6]=0x40; id.bytes[8]=0x80; id.bytes[15]=last; return id; }
class CharacterGame final : public Game {
    bool valid_=true;
public:
    bool on_start(Context& context) override {
        CharacterSnapshot c;
        valid_ = context.read_character(entity(2),c)==Result::success && c.height>1 && c.position[1]>49;
        EntitySnapshot e;
        valid_ &= context.read_entity(entity(2),e)==Result::success && (e.flags&4U) && !(e.flags&2U);
        valid_ &= context.read_character(entity(1),c)==Result::no_character && context.read_character(entity(99),c)==Result::not_found;
        c.size=0; valid_ &= context.read_character(entity(2),c)==Result::invalid_argument;
        valid_ &= context.control_character(entity(2),{0,1,0})==Result::invalid_argument;
        valid_ &= context.control_character(entity(2),{std::numeric_limits<float>::infinity(),0,0})==Result::invalid_argument;
        Result result=Result::success;
        std::thread worker{[&] { CharacterSnapshot state; result=context.read_character(entity(2),state); }}; worker.join();
        return valid_ && result==Result::wrong_phase;
    }
    void on_event(Context& context,const Event& event) override {
        if (event.kind==EventKind::key && (event.flags&1U)) {
            if (event.code==static_cast<std::uint32_t>(Key::space)) valid_ &= context.control_character(entity(2),{2,0,0},5)==Result::success;
            else valid_ &= context.control_character(entity(2),{2,0,0})==Result::success;
        }
    }
    void on_frame(Context& context,Frame& frame) override {
        CharacterSnapshot c;
        valid_ &= context.read_character(entity(2),c)==Result::success;
        valid_ &= context.control_character(entity(2),{})==Result::wrong_phase;
        frame.clear_color={valid_ ? 1.0F:0.0F,c.velocity.y>1 ? 1.0F:0.0F,c.position[0]>1000000000000.1 ? 1.0F:0.0F,1};
    }
};
}
VICE_GAME_MODULE(CharacterGame)
