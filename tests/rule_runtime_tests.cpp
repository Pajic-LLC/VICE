#include "presentation_fixture.hpp"
#include <vice/runtime/debug.hpp>
#include <vice/runtime/module_loader.hpp>
#if defined(VICE_RULE_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
int main(){
    using namespace fixture;
    Data f;auto module=runtime::load_game_module(VICE_RULE_TEST_MODULE);require(static_cast<bool>(module),"rule module load");
    project::Manifest manifest;runtime::GameModuleLifecycle lifecycle{*module.module,{manifest,f.world}};
    require(lifecycle.start()&&f.world.rule_info().pending==1,"SDK definition, schema, phase, thread, size and old-host guards");
    const auto saved=world::encode_world_snapshot(f.world.snapshot());require(static_cast<bool>(saved),"pending rule checkpoint");
    lifecycle.event(platform::Event{platform::KeyEvent{.key=platform::Key::e,.pressed=true}});render::Color color;lifecycle.frame(color);
    require(color.red==1&&f.world.rule_info().pending==0,"SDK dispatch and read-only frame");
    const world::ComponentTypeId type{id("a2000000-0000-4000-8000-00000000005f")};
    require(std::get<std::int64_t>(f.world.component(*f.world.find(actor),type)->fields[0].value)==9,"one authoritative component effect");
    const auto inspect=runtime::sample_runtime_debug_entity(f.world,actor);
    require(inspect.rules.count==1&&inspect.rule_page.entries.size()==1&&runtime::serialize_runtime_debug_entity_snapshot(inspect,"rule").find("\"outcome\":\"applied\"")!=std::string::npos,"Runtime structured rule decisions");
    world::World next;require(next.restore(*world::decode_world_snapshot(saved.bytes).snapshot)&&next.rule_info().pending==1,"restore pending without running");
    auto fresh=runtime::load_game_module(VICE_RULE_TEST_MODULE);require(static_cast<bool>(fresh),"fresh module");runtime::GameModuleLifecycle resumed{*fresh.module,{manifest,next}};
    require(resumed.start(),"module consumes restored definition");resumed.event(platform::Event{platform::KeyEvent{.key=platform::Key::e,.pressed=true}});resumed.frame(color);require(color.red==1,"fresh Runtime consumes saved wake");resumed.stop();lifecycle.stop();
#if defined(VICE_RULE_EDITOR)
    Data source;jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count=1}};editor::PreviewRuntime preview;
    const editor::PreviewGameConfig config{manifest,VICE_RULE_TEST_MODULE,source.workspace.cooked_root,false};
    require(preview.reset(source.world,jobs,editor::AssetIndex{},nullptr,&config),"Editor rule module");preview.play();preview.pause();require(preview.step_once()&&preview.world()->rule_info().pending==0&&preview.world()->rule_page().entries[0].decision.outcome==world::RuleOutcome::applied&&source.world.rule_info().count==0,"Editor automatic clock pump and authoring isolation");
    require(preview.reset(source.world,jobs,editor::AssetIndex{},nullptr,&config)&&preview.world()->rule_page().entries[0].decision.evaluations==0&&preview.world()->rule_info().pending==1,"Editor Reset discards runtime decisions");
#endif
    std::cout<<"Rule SDK, checkpoint, inspection and Editor parity passed\n";
}
