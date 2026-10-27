#include <vice/dev/cli.hpp>
#include <vice/runtime/checkpoint.hpp>
#include <vice/runtime/debug_session.hpp>
#include <vice/world/world.hpp>
#include <vice/platform/file.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
namespace {
void require(bool ok, const char* message) { if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
}
int main() {
    using namespace vice;
    const auto root=std::filesystem::temp_directory_path()/world::PersistentId::random().to_string();
    const runtime::RuntimeDevSession session{.id="checkpoint-test",.root=root};
    std::string diagnostics;
    require(runtime::prepare_runtime_dev_session(session,&diagnostics),"prepare session");
    require(runtime::publish_runtime_dev_snapshot(session,1,"{}",&diagnostics),"publish session");
    world::World world;
    require(world.begin_causal_recording(),"capture begins");
    const auto snapshot=world.snapshot();
    auto descriptor=runtime::publish_runtime_world_checkpoint(session,snapshot,&diagnostics);
    require(diagnostics.empty(),"export world");
    auto read=runtime::read_runtime_world_checkpoint(session,descriptor);
    require(bool(read),"read verified checkpoint");
    auto restored=world::decode_world_snapshot(read.bytes);
    require(restored.snapshot->causal_history.id==snapshot.causal_history.id && restored.snapshot->causal_history.events==snapshot.causal_history.events,"event identities survive checkpoint");
    require(!runtime::read_runtime_world_checkpoint(session,"{}"),"reject invalid descriptor");
    require(world.begin_causal_recording(true),"new segment");
    const auto newer=runtime::publish_runtime_world_checkpoint(session,world.snapshot());
    require(newer!=descriptor,"new snapshot digest differs");
    require(!runtime::read_runtime_world_checkpoint(session,descriptor),"stale descriptor fails");
    { std::ofstream output{runtime::runtime_dev_session_path(session)/"checkpoint.vworldc",std::ios::binary}; output << "damaged"; }
    require(!runtime::read_runtime_world_checkpoint(session,newer),"corrupt snapshot fails");
    const auto invalid_descriptor=std::string{"{\"schema\":\"vice.dev.runtime.checkpoint.v1\",\"sha256\":\""}+platform::text_revision("damaged")+"\"}";
    require(!runtime::read_runtime_world_checkpoint(session,invalid_descriptor),"matching digest cannot bypass World decode");
    require(!runtime::publish_runtime_checkpoint_request(session,"../escape"),"invalid request rejected");
    std::thread worker{[&] {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{5};
        while(std::chrono::steady_clock::now()<deadline) {
            const auto request=runtime::read_runtime_debug_request(session);
            if(request) {
                require(request.kind==runtime::RuntimeDebugRequestKind::world_checkpoint,"request kind");
                const auto payload=runtime::publish_runtime_world_checkpoint(session,snapshot);
                require(runtime::publish_runtime_debug_response(session,request.request_id,payload),"publish response");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        require(false,"request timed out");
    }};
    const auto destination=root/"saved.vworldc";
    const auto result=dev::execute_runtime_checkpoint(runtime::runtime_dev_session_path(session),destination);
    worker.join();
    require(result.exit_code==0,"CLI checkpoint succeeds");
    require(result.json.find("runtime.checkpoint")!=std::string::npos,"CLI schema command");
    const auto saved=world::load_world_snapshot(destination);
    require(saved && saved.snapshot->causal_history.id==snapshot.causal_history.id && saved.snapshot->causal_history.events==snapshot.causal_history.events,"CLI saved exact world");
    require(dev::execute_runtime_checkpoint(runtime::runtime_dev_session_path(session),destination).exit_code==3,"existing output rejected");
    require(world::load_world_snapshot(destination).snapshot->causal_history.events==snapshot.causal_history.events,"existing output preserved");
    std::filesystem::remove_all(root);
}
