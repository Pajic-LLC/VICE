#include <vice/jobs/jobs.hpp>
#include <vice/rigid_body/authored_simulation.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/tools/world_edit.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/runtime/module_loader.hpp>
#include <vice/runtime/debug.hpp>
#if defined(VICE_LINK_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
namespace {
void require(bool ok, const char* message) { if (!ok) { std::cerr << "Authored links: " << message << '\n'; std::exit(1); } }
vice::world::PersistentId id(const char* text) { return *vice::world::PersistentId::parse(text); }
std::string source() {
    return "vice-world-source 1\nregion 1 10 1000000000000 0 0 0 0 0 full none\nregion 2 20 1000000000100 0 0 0 0 0 full none\n"
        "entity 96000000-0000-4000-8000-000000000001 spatial 1000000000000 5 0 1 rigid_box static 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 discrete solid\n"
        "entity 96000000-0000-4000-8000-000000000002 spatial 1000000000000 3 0 1 rigid_box dynamic 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 discrete solid\n"
        "entity 96000000-0000-4000-8000-000000000003 none distance_link 96000000-0000-4000-8000-000000000001 96000000-0000-4000-8000-000000000002 2 2 0 0 0 intact 0 0\n";
}
void fingerprint(std::vector<std::byte>& bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 40; i < bytes.size(); ++i) { hash ^= std::to_integer<std::uint8_t>(bytes[i]); hash *= 1099511628211ULL; }
    for (std::size_t i = 0; i < 8; ++i) bytes[32+i] = static_cast<std::byte>((hash >> (8U*i)) & 255U);
}
}
int main() {
    using namespace vice;
    const auto anchor_id = id("96000000-0000-4000-8000-000000000001"), payload_id = id("96000000-0000-4000-8000-000000000002"), link_id = id("96000000-0000-4000-8000-000000000003");
    const auto parsed = tools::world_cooker::parse_source_manifest(source());
    require(parsed.snapshot.has_value(), "parse source link");
    const auto canonical = tools::world_cooker::serialize_source_manifest(*parsed.snapshot);
    require(tools::world_cooker::serialize_source_manifest(*tools::world_cooker::parse_source_manifest(canonical).snapshot) == canonical, "deterministic text round trip");
    const auto encoded = world::encode_world_snapshot(*parsed.snapshot);
    const auto decoded = world::decode_world_snapshot(encoded.bytes);
    require(decoded.snapshot && decoded.snapshot->entities.back().distance_link == parsed.snapshot->entities.back().distance_link, "v8 link round trip");
    world::World legacy; require(legacy.create_entity(payload_id).has_value(), "legacy fixture");
    auto bytes = world::encode_world_snapshot(legacy.snapshot()).bytes;
    require(bytes.size() == 136, "minimal current layout");
    bytes.resize(bytes.size() - 66U);
    bytes.pop_back();
    bytes.erase(bytes.begin() + 64);
    bytes.erase(bytes.begin() + 63);
    bytes.erase(bytes.begin() + 62);
    bytes[8] = std::byte{7};
    fingerprint(bytes);
    const auto old = world::decode_world_snapshot(bytes);
    require(old.snapshot && !old.snapshot->entities[0].distance_link, "version 7 remains readable");

    world::World authored; require(authored.restore(*parsed.snapshot), "restore link world");
    const auto anchor = *authored.find(anchor_id), payload = *authored.find(payload_id), owner = *authored.find(link_id);
    require(!authored.destroy_entity(anchor) && !authored.clear_rigid_box(payload) && !authored.clear_spatial_state(payload), "intact dependencies cannot be silently removed");
    require(!authored.handoff_spatial(payload, world::RegionId{2}), "intact linked bodies cannot be split across domains");
    auto static_payload = *authored.rigid_box(payload); static_payload.motion = world::RigidMotion::static_body;
    require(!authored.set_rigid_box(payload, static_payload), "static/static link is rejected");
    static_payload.motion = world::RigidMotion::kinematic_body;
    require(!authored.set_rigid_box(payload, static_payload), "a link needs a dynamic endpoint");
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    rigid_body::AuthoredSimulation held; require(held.reset(authored, jobs), "instantiate authored distance constraint");
    for (int tick=0;tick<240;++tick) require(held.step(), "hold under gravity");
    require(std::abs(authored.spatial_state(payload)->global_position.y - 3) < 0.05 && !authored.distance_link(owner)->broken, "unbreakable link preserves authored limit");

    world::World breaking; require(breaking.restore(*parsed.snapshot), "break fixture");
    auto weak = *breaking.distance_link(*breaking.find(link_id)); weak.break_threshold = 0.001F;
    require(breaking.set_distance_link(*breaking.find(link_id), weak), "weak threshold");
    rigid_body::AuthoredSimulation released; require(released.reset(breaking, jobs) && released.step(), "break on physical impulse");
    const auto broken = *breaking.distance_link(*breaking.find(link_id));
    require(broken.broken && broken.last_break_impulse > weak.break_threshold && broken.break_step == 1, "World records break identity, impulse and fixed-step provenance");
    for (int tick=0;tick<90;++tick) require(released.step(), "released body simulation");
    require(breaking.spatial_state(*breaking.find(payload_id))->global_position.y < 1, "broken constraint releases payload");
    const auto saved = world::encode_world_snapshot(breaking.snapshot());
    world::World resumed;
    require(resumed.restore(*world::decode_world_snapshot(saved.bytes).snapshot), "save broken world");
    rigid_body::AuthoredSimulation resumed_physics; require(resumed_physics.reset(resumed, jobs) && resumed_physics.step(), "resume broken state");
    require(resumed.distance_link(*resumed.find(link_id)) == broken && resumed.spatial_state(*resumed.find(payload_id))->global_position.y < 1, "resume does not silently rebuild broken joint");
    require(resumed.handoff_spatial(*resumed.find(payload_id), world::RegionId{2}), "broken link permits independent handoff");
    require(resumed.clear_rigid_box(*resumed.find(payload_id)), "broken evidence can survive physics removal");
    require(!resumed.destroy_entity(*resumed.find(payload_id)), "persistent evidence retains endpoint identity until link removed");
    require(resumed.clear_distance_link(*resumed.find(link_id)) && resumed.destroy_entity(*resumed.find(payload_id)), "explicit dependency removal permits deletion");

    world::World driven; require(driven.restore(*parsed.snapshot), "SDK-driven fixture");
    auto driven_link = *driven.distance_link(*driven.find(link_id)); driven_link.break_threshold = 1000;
    require(driven.set_distance_link(*driven.find(link_id), driven_link), "SDK threshold");
    rigid_body::AuthoredSimulation driven_physics; require(driven_physics.reset(driven, jobs), "SDK physics");
    auto module = runtime::load_game_module(VICE_LINK_TEST_MODULE);
    require(static_cast<bool>(module), "load actual link gameplay module");
    project::Manifest manifest;
    runtime::GameModuleLifecycle lifecycle{*module.module, {manifest, driven, &driven_physics}};
    require(lifecycle.start(), "SDK reads link and rejects wrong-thread/invalid records");
    lifecycle.event(platform::Event{platform::KeyEvent{.key = platform::Key::space, .pressed = true}});
    require(driven_physics.step(), "gameplay physical stress breaks link");
    render::Color color; lifecycle.frame(color);
    require(color.red == 1 && color.green == 1 && color.blue == 1, "module observes broken state and physical provenance in frame callback");
    const auto debug = runtime::sample_runtime_debug_entity(driven, link_id);
    require(debug.distance_link && debug.distance_link->broken &&
        runtime::serialize_runtime_debug_entity_snapshot(debug, "links").find("last_break_impulse") != std::string::npos,
        "normal Runtime entity debug exposes link failure evidence");
    lifecycle.stop();
    auto invalid = *parsed.snapshot; invalid.entities.back().distance_link->second = id("96000000-0000-4000-8000-000000000099");
    std::string diagnostics;
    require(tools::world_cooker::serialize_source_manifest(invalid, &diagnostics).empty() && diagnostics.find("missing endpoint") != std::string::npos, "endpoint diagnostics");
    require(!authored.restore(invalid) && authored.distance_link(owner).has_value(), "invalid restore is transactional");
    tools::world_authoring::Transaction transaction;
    transaction.operations.push_back(tools::world_authoring::SetDistanceLink{link_id, weak});
    const auto edited = tools::world_authoring::apply(*parsed.snapshot, transaction);
    require(edited.success && edited.snapshot.entities.back().distance_link->break_threshold == weak.break_threshold, "typed agent link edit");
    transaction.operations.push_back(tools::world_authoring::SetRigidBox{payload_id, std::nullopt});
    require(!tools::world_authoring::apply(*parsed.snapshot, transaction).success, "invalid linked-body edit rejects whole batch");
    transaction.operations = {tools::world_authoring::SetDistanceLink{link_id, std::nullopt},
        tools::world_authoring::SetRigidBox{payload_id, std::nullopt}};
    require(tools::world_authoring::apply(*parsed.snapshot, transaction).success, "explicit clear-link/body batch remains valid");
    auto self_link = weak; self_link.first = link_id; self_link.broken = true;
    require(!authored.set_distance_link(owner, self_link), "logical link owner cannot reference itself");
    const auto before = held.coordinator().step_index();
    auto changed = *authored.distance_link(owner); changed.max_distance = 3;
    require(authored.set_distance_link(owner, changed) && !held.step() && held.coordinator().step_index() == before, "live structural edit requests reset before solver advance");
#if defined(VICE_LINK_EDITOR)
    world::World preview_source; require(preview_source.restore(*parsed.snapshot), "preview source");
    require(preview_source.set_distance_link(*preview_source.find(link_id), weak), "preview weak link");
    editor::AssetIndex assets; editor::PreviewRuntime preview;
    const editor::PreviewGameConfig config{project::Manifest{}, {}};
    require(preview.reset(preview_source, jobs, assets, &diagnostics, &config) && preview.step_once(), "preview instantiates and breaks same authored link");
    require(preview.world()->distance_link(*preview.world()->find(link_id))->broken && !preview_source.distance_link(*preview_source.find(link_id))->broken, "preview break remains isolated from source");
#endif
    return 0;
}
