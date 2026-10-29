#include <vice/world/world.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/tools/world_authoring.hpp>
#include <vice/runtime/module_loader.hpp>
#include <vice/runtime/debug.hpp>
#include <vice/runtime/scene.hpp>
#include <vice/runtime/render_streaming.hpp>
#include <cstdio>
#if defined(VICE_DEPENDENCY_PHYSICS)
#include <vice/rigid_body/authored_simulation.hpp>
#include <vice/jobs/jobs.hpp>
#endif
#if defined(VICE_DEPENDENCY_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
#include <algorithm>
#include <cstdlib>
#include <iostream>
namespace {
void require(bool ok, const char* message) { if (!ok) { std::cerr << "Supply: " << message << '\n'; std::exit(1); } }
vice::world::PersistentId id(unsigned value) {
    char text[37]; std::snprintf(text, sizeof(text), "98000000-0000-4000-8000-%012x", value);
    return *vice::world::PersistentId::parse(text);
}
std::string source() {
    return "vice-world-source 1\nregion 1 10 1000000000000 0 0 0 0 0 full none\n"
        "entity 98000000-0000-4000-8000-000000000001 spatial 1000000000000 5 0 1 rigid_box static 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 discrete solid\n"
        "entity 98000000-0000-4000-8000-000000000002 spatial 1000000000000 3 0 1 rigid_box dynamic 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 discrete solid\n"
        "entity 98000000-0000-4000-8000-000000000003 none distance_link 98000000-0000-4000-8000-000000000001 98000000-0000-4000-8000-000000000002 2 2 0 0 1000 intact 0 0\n"
        "entity 98000000-0000-4000-8000-000000000006 spatial 1000000000000 4 0 1 light directional 1 1 1 2 enabled dependency 98000000-0000-4000-8000-000000000005 none enabled\n"
        "entity 98000000-0000-4000-8000-000000000007 spatial 1000000000000 4 0 1 light directional 1 1 1 2 enabled dependency 98000000-0000-4000-8000-000000000004 none enabled\n"
        "entity 98000000-0000-4000-8000-000000000005 none dependency 98000000-0000-4000-8000-000000000004 98000000-0000-4000-8000-000000000003 enabled\n"
        "entity 98000000-0000-4000-8000-000000000004 none dependency none none enabled\n";
}
void check_failure(const vice::world::World& world) {
    using namespace vice;
    const auto d = world.dependency_state(*world.find(id(6)));
    require(d && !d->available && d->reason == world::DependencyReason::upstream_unavailable &&
        d->root_reason == world::DependencyReason::link_broken && d->root_cause == id(3) &&
        d->path == std::vector{id(6), id(5), id(3)} && d->break_impulse > 1000 && d->break_step > 0, "physical root cause and ordered explanation path");
    require(world.dependency_state(*world.find(id(7)))->available, "independent consumer remains supplied");
    require(!world.dependency_available(*world.find(id(6))) && world.dependency_available(*world.find(id(7))), "allocation-free availability agrees with explanation");
    const auto resident = runtime::build_runtime_render_resident_scene(world);
    require(resident && resident.scene.lights.size() == 1 && resident.scene.lights[0].entity == id(7), "production resident-render path filters unsupplied light");
    const auto lights = runtime::collect_runtime_lights(world);
    require(lights.size() == 1 && lights[0].entity == id(7) && world.light(*world.find(id(6)))->enabled, "effective light gating preserves authored enabled state");
}
}
int main() {
    using namespace vice;
    const auto parsed = tools::world_cooker::parse_source_manifest(source());
    require(parsed.snapshot.has_value(), "parse forward referenced supply graph");
    const auto canonical = tools::world_cooker::serialize_source_manifest(*parsed.snapshot);
    require(tools::world_cooker::serialize_source_manifest(*tools::world_cooker::parse_source_manifest(canonical).snapshot) == canonical, "canonical dependency source round trip");
    world::World world; require(world.restore(*parsed.snapshot), "restore graph transactionally");
    require(world.dependency_state(*world.find(id(6)))->available && runtime::collect_runtime_lights(world).size() == 2, "initial shared supply and lights");
    require(!world.destroy_entity(*world.find(id(4))) && !world.clear_dependency(*world.find(id(5))) &&
        !world.clear_distance_link(*world.find(id(3))) && !world.destroy_entity(*world.find(id(3))), "provider and gate dependencies protect identities and components");
    require(!world.set_dependency(*world.find(id(4)), {id(6), {}, false}), "cycles rejected even when disabled");
    require(!world.set_dependency(*world.find(id(6)), {id(1), {}, true}), "provider component required");
    require(!world.set_dependency(*world.find(id(6)), {id(4), id(2), true}), "gate component required");
    require(!world.set_dependency(*world.find(id(6)), {id(6), {}, true}), "self provider rejected");
    require(world.set_dependency(*world.find(id(4)), {{}, {}, false}), "disable source");
    const auto disabled = world.dependency_state(*world.find(id(6)));
    require(disabled && !disabled->available && disabled->root_cause == id(4) && disabled->root_reason == world::DependencyReason::disabled && runtime::collect_runtime_lights(world).empty(), "source disable propagates without physics");
    require(world.set_dependency(*world.find(id(4)), {}), "re-enable source");
    tools::world_authoring::Transaction invalid;
    invalid.operations = {tools::world_authoring::SetDependency{id(4), world::Dependency{id(6), {}, true}}};
    require(!tools::world_authoring::apply(*parsed.snapshot, invalid), "typed cyclic batch rejected");
    invalid.operations = {tools::world_authoring::SetDistanceLink{id(3), std::nullopt}};
    require(!tools::world_authoring::apply(*parsed.snapshot, invalid), "typed dangling gate batch rejected");
    invalid.operations = {tools::world_authoring::SetDependency{id(6), std::nullopt}, tools::world_authoring::SetDependency{id(5), std::nullopt}, tools::world_authoring::SetDistanceLink{id(3), std::nullopt}};
    require(static_cast<bool>(tools::world_authoring::apply(*parsed.snapshot, invalid)), "explicit dependency removal batch allowed");
    auto module = runtime::load_game_module(VICE_DEPENDENCY_TEST_MODULE);
    require(static_cast<bool>(module), "load real SDK dependency module");
    project::Manifest manifest;
#if defined(VICE_DEPENDENCY_PHYSICS)
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count=1}};
    rigid_body::AuthoredSimulation physics; require(physics.reset(world, jobs), "instantiate linked supply fixture");
    runtime::GameModuleLifecycle lifecycle{*module.module, {manifest, world, &physics}};
    require(lifecycle.start(), "SDK reads initial supply and validates calls");
    lifecycle.event(platform::Event{platform::KeyEvent{.key=platform::Key::space, .pressed=true}});
    require(physics.step(), "real physical stress drives supply failure");
#else
    runtime::GameModuleLifecycle lifecycle{*module.module, {manifest, world}};
    require(lifecycle.start(), "SDK reads without physics backend");
    auto broken = *world.distance_link(*world.find(id(3))); broken.broken=true; broken.last_break_impulse=1200; broken.break_step=8;
    require(world.set_distance_link(*world.find(id(3)), broken), "restore-equivalent broken evidence without backend");
#endif
    check_failure(world);
    render::Color color; lifecycle.frame(color); lifecycle.stop();
    require(color.red == 1 && color.green == 1 && color.blue == 1, "SDK sees failure provenance and independent availability");
    const auto debug = runtime::serialize_runtime_debug_entity_snapshot(runtime::sample_runtime_debug_entity(world, id(6)), "supply");
    require(debug.find("upstream_unavailable") != std::string::npos && debug.find("link_broken") != std::string::npos && debug.find("break_impulse") != std::string::npos, "Runtime debug explains failure");
    const auto saved = world::encode_world_snapshot(world.snapshot());
    require(static_cast<bool>(saved), "encode format 9");
    world::World resumed; require(resumed.restore(*world::decode_world_snapshot(saved.bytes).snapshot), "resume saved graph and physical evidence");
    check_failure(resumed);
#if defined(VICE_DEPENDENCY_PHYSICS)
    rigid_body::AuthoredSimulation resumed_physics;
    require(resumed_physics.reset(resumed, jobs) && resumed_physics.step(), "resume physical graph without repairing failed supply");
    check_failure(resumed);
#endif
    require(resumed.clear_dependency(*resumed.find(id(6))) && resumed.clear_dependency(*resumed.find(id(5))) && resumed.clear_distance_link(*resumed.find(id(3))), "reverse indexes cleaned by explicit removal");
    auto bad = world.snapshot(); bad.entities.back().dependency = world::Dependency{id(999), {}, true};
    require(!world.restore(bad), "invalid restore leaves authoritative graph unchanged"); check_failure(world);
    world::WorldSnapshot chain;
    for (unsigned i=1; i<=256; ++i) { world::SnapshotEntity e; e.persistent_id=id(1000+i); e.dependency=world::Dependency{i==1 ? world::PersistentId{} : id(999+i), {}, true}; chain.entities.push_back(e); }
    world::World deep; require(deep.restore(chain) && deep.dependency_state(*deep.find(id(1256)))->path.size() == 256 && deep.dependency_available(*deep.find(id(1256))), "maximum bounded chain works");
    auto extra = chain.entities.back(); extra.persistent_id=id(1257); extra.dependency->provider=id(1256); chain.entities.push_back(extra);
    require(!deep.restore(chain) && deep.entity_count() == 256, "over-limit chain rejected transactionally");
    // Minimal old v8 file: remove only the new optional presence byte and rehash.
    world::World empty; require(empty.create_entity(id(1)).has_value(), "old fixture");
    auto bytes = world::encode_world_snapshot(empty.snapshot()).bytes; require(bytes.size() == 124, "minimal current format");
    bytes.resize(bytes.size()-54U);bytes.pop_back(); bytes.erase(bytes.begin()+64); bytes.erase(bytes.begin()+63); bytes[8]=std::byte{8};
    std::uint64_t hash=14695981039346656037ULL; for (std::size_t i=40;i<bytes.size();++i) { hash ^= std::to_integer<std::uint8_t>(bytes[i]); hash *= 1099511628211ULL; }
    for (std::size_t i=0;i<8;++i) bytes[32+i]=static_cast<std::byte>((hash>>(8U*i))&255U);
    const auto old=world::decode_world_snapshot(bytes); require(old.snapshot && !old.snapshot->entities[0].dependency, "old v8 remains readable");
#if defined(VICE_DEPENDENCY_EDITOR)
    world::World authored; require(authored.restore(*parsed.snapshot), "preview source");
    auto weak=*authored.distance_link(*authored.find(id(3))); weak.break_threshold=0.001F; require(authored.set_distance_link(*authored.find(id(3)), weak), "preview stress threshold");
    editor::PreviewRuntime preview; editor::AssetIndex assets; std::string diagnostics;
    const editor::PreviewGameConfig config{manifest, VICE_DEPENDENCY_TEST_MODULE};
    require(preview.reset(authored, jobs, assets, &diagnostics, &config) && preview.step_once(), "shared preview physics");
    require(preview.has_game_module() && preview.clear_color().red == 1 && preview.clear_color().green == 1, "preview module observes the same causal SDK state");
    require(!preview.world()->dependency_state(*preview.world()->find(id(6)))->available && authored.dependency_state(*authored.find(id(6)))->available, "preview supply failure isolated from source");
#endif
    return 0;
}
