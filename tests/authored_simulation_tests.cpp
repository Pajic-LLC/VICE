#include <vice/rigid_body/authored_simulation.hpp>
#include <vice/jobs/jobs.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/tools/world_edit.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>
#if defined(VICE_TEST_EDITOR_PREVIEW)
#include <vice/editor/preview_runtime.hpp>
#endif

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "Authored simulation test failed: " << message << '\n'; std::exit(1); }
}
constexpr std::string_view falling_id = "91000000-0000-4000-8000-000000000002";
constexpr std::string_view moving_id = "91000000-0000-4000-8000-000000000003";
vice::world::PersistentId id(std::string_view value) { return *vice::world::PersistentId::parse(value); }
double height(const vice::world::World& world) { return world.spatial_state(*world.find(id(falling_id)))->global_position.y; }
std::string source() {
    return "vice-world-source 1\nregion 7 70 1000000000000 0 0 0 0 0 full none\n"
        "entity 91000000-0000-4000-8000-000000000001 spatial 1000000000000 -0.5 0 7 rigid_box static 10 0.5 10 0 0 0 0 0 0 0.5 0 discrete solid\n"
        "entity 91000000-0000-4000-8000-000000000002 spatial 1000000000000.25 5 0 7 rigid_box dynamic 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 continuous solid\n"
        "entity 91000000-0000-4000-8000-000000000003 spatial 1000000000020 3 0 7 rigid_box kinematic 0.5 0.5 0.5 2 0 0 0 0 0 0.5 0 discrete solid\n";
}
} // namespace

int main() {
    using namespace vice;
    const auto parsed = tools::world_cooker::parse_source_manifest(source());
    require(parsed.snapshot.has_value(), "authored source parses without Lab IDs");
    const auto canonical = tools::world_cooker::serialize_source_manifest(*parsed.snapshot);
    require(tools::world_cooker::serialize_source_manifest(*tools::world_cooker::parse_source_manifest(canonical).snapshot) == canonical,
        "source round trip deterministic");
    const auto encoded = world::encode_world_snapshot(*parsed.snapshot);
    require(static_cast<bool>(encoded), "current cooked World encoding");
    const auto decoded = world::decode_world_snapshot(encoded.bytes);
    require(decoded.snapshot && decoded.snapshot->entities[1].rigid_box &&
        decoded.snapshot->entities[1].rigid_box->continuous, "cooked physics data survives");
    // Build an independent v6 one-entity fixture by removing v8/v7 presence bytes
    // and updating the documented payload fingerprint in the fixed header.
    world::World legacy_world;
    require(legacy_world.create_entity(id(falling_id)).has_value(), "legacy entity fixture");
    auto legacy = world::encode_world_snapshot(legacy_world.snapshot()).bytes;
    require(legacy.size() == 140U, "minimal current fixture size");
    legacy.resize(legacy.size() - 70U);
    legacy.pop_back();
    legacy.erase(legacy.begin() + 64);
    legacy.erase(legacy.begin() + 63);
    legacy.erase(legacy.begin() + 62);
    legacy.erase(legacy.begin() + 61);
    legacy[8] = std::byte{6};
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 40; i < legacy.size(); ++i) {
        hash ^= std::to_integer<std::uint8_t>(legacy[i]); hash *= 1099511628211ULL;
    }
    for (std::size_t i = 0; i < 8; ++i) legacy[32 + i] = static_cast<std::byte>((hash >> (8U * i)) & 255U);
    const auto old = world::decode_world_snapshot(legacy);
    require(old.snapshot && old.snapshot->entities.size() == 1 && !old.snapshot->entities[0].rigid_box,
        "version 6 worlds remain readable without physics records");
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    world::World a, b;
    require(a.restore(*decoded.snapshot) && b.restore(*decoded.snapshot), "restore physical worlds");
    rigid_body::AuthoredSimulation first, second;
    std::string diagnostics;
    require(first.reset(a, jobs, &diagnostics) && second.reset(b, jobs, &diagnostics), "shared service initializes");
    require(first.body_count() == 3, "only authored bodies instantiated");
    for (int step = 0; step < 30; ++step) require(first.step(&diagnostics) && second.step(&diagnostics), "fixed step");
    require(height(a) < 4.9 && height(a) > 4, "dynamic body falls under gravity");
    require(std::abs(height(a) - height(b)) < 1e-5, "same service deterministic with same inputs");
    const auto falling = *a.find(id(falling_id));
    require(a.rigid_box(falling)->linear_velocity.y < 0, "velocity is authoritative World state");
    require(std::abs(a.spatial_state(falling)->global_position.x - 1000000000000.25) < 1e-6, "large-world submeter precision");

    world::World resumed;
    const auto saved = world::encode_world_snapshot(a.snapshot());
    require(resumed.restore(*world::decode_world_snapshot(saved.bytes).snapshot), "persist moving state");
    rigid_body::AuthoredSimulation restored;
    require(restored.reset(resumed, jobs, &diagnostics), "resume from saved pose and velocity");
    for (int step = 0; step < 210; ++step) {
        require(first.step(&diagnostics) && second.step(&diagnostics) && restored.step(&diagnostics), "continue/resume simulation");
    }
    require(std::abs(height(a) - 0.5) < 0.05, "dynamic body rests on authored static floor");
    require(std::abs(height(a) - height(resumed)) < 0.05, "resumed state reaches same physical outcome");
    const auto mover = *a.find(id(moving_id));
    const auto moved = a.spatial_state(mover)->global_position;
    require(std::abs(moved.x - 1000000000024.0) < 0.01 && std::abs(moved.y - 3) < 0.001,
        "kinematic body follows velocity without gravity");
    const auto debug = first.body(id(falling_id));
    require(debug && debug->box_half_extents && debug->motion_quality == rigid_body::MotionQuality::continuous,
        "generic body debug uses same VICE physics state");
#if defined(VICE_TEST_EDITOR_PREVIEW)
    world::World authored;
    require(authored.restore(*parsed.snapshot), "fresh Editor authoring fixture");
    editor::AssetIndex assets;
    editor::PreviewRuntime preview;
    require(preview.reset(authored, jobs, assets, &diagnostics), "Editor preview accepts arbitrary authored physics without Lab assets");
    preview.play();
    for (int step = 0; step < 240; ++step) require(preview.update(1.0 / 120.0), "preview fixed updates");
    require(preview.world() && std::abs(height(*preview.world()) - height(b)) < 1e-5 &&
        preview.snapshot().bodies.size() == 3 && preview.coordinator()->step_index() == 240,
        "Editor and standalone service produce the same body states");
    require(preview.set_body_velocity(id(falling_id), {2, 6, 0}) == rigid_body::BodyControlResult::applied &&
        second.set_body_velocity(id(falling_id), {2, 6, 0}, {}) == rigid_body::BodyControlResult::applied,
        "same controls accepted by preview and standalone service");
    for (int step = 0; step < 30; ++step) require(preview.update(1.0 / 120.0) && second.step(), "controlled preview parity updates");
    const auto controlled = preview.world()->spatial_state(*preview.world()->find(id(falling_id)))->global_position;
    require(std::abs(controlled.y - height(b)) < 1e-5 &&
        std::abs(controlled.x - b.spatial_state(*b.find(id(falling_id)))->global_position.x) < 1e-5,
        "controlled preview and standalone body states agree");
    require(height(authored) == 5, "preview simulation does not mutate authored source World");
    const auto authored_mover = *authored.find(id(moving_id));
    require(authored.set_reflection_probe(authored_mover, world::ReflectionProbe{}), "probe on physical entity");
    editor::CommandHistory history;
    editor::CommandHistory::CreateEntityEntry entry;
    entry.entity = authored.snapshot().entities.back();
    entry.label = "Create physical entity";
    require(history.record(entry) && history.undo(authored) && !authored.find(id(moving_id)), "undo physical entity creation");
    require(history.redo(authored), "redo physical entity creation");
    const auto restored_mover = *authored.find(id(moving_id));
    require(authored.rigid_box(restored_mover) && authored.reflection_probe(restored_mover),
        "undo/redo retains physical and probe components");
    require(editor::project_inspector(authored, id(moving_id))->rigid_box.has_value(),
        "Editor inspector exposes the same physical record");
#endif

    world::World inactive;
    require(inactive.restore(*parsed.snapshot) && inactive.set_region_fidelity(world::RegionId{7}, world::RegionFidelity::inactive), "inactive fixture");
    rigid_body::AuthoredSimulation sleeping;
    require(sleeping.reset(inactive, jobs, &diagnostics), "inactive authored domain initialized");
    for (int step = 0; step < 30; ++step) require(sleeping.step(&diagnostics), "disabled domain stepping");
    require(height(inactive) == 5, "inactive fidelity does not simulate or force activation");
    require(inactive.set_region_fidelity(world::RegionId{7}, world::RegionFidelity::full) && sleeping.step(&diagnostics) &&
        height(inactive) < 5, "explicit activation resumes simulation");

    const auto step_before_edit = sleeping.coordinator().step_index();
    const auto inactive_falling = *inactive.find(id(falling_id));
    auto changed_body = *inactive.rigid_box(inactive_falling);
    changed_body.half_extents.x = 2;
    require(inactive.set_rigid_box(inactive_falling, changed_body), "external physical edit");
    require(!sleeping.step(&diagnostics) && diagnostics.find("reset") != std::string::npos &&
        sleeping.coordinator().step_index() == step_before_edit &&
        inactive.rigid_box(inactive_falling)->half_extents.x == 2,
        "external edits are preserved and rejected before the solver advances");
    require(sleeping.reset(inactive, jobs, &diagnostics) && sleeping.step(&diagnostics), "reset adopts external physical edit");

    auto invalid = *a.rigid_box(falling);
    invalid.half_extents.x = 0;
    require(!a.set_rigid_box(falling, invalid), "zero collider extent rejected");
    invalid = *a.rigid_box(falling);
    invalid.linear_velocity.y = std::numeric_limits<float>::infinity();
    require(!a.set_rigid_box(falling, invalid), "nonfinite velocity rejected");
    auto bad_snapshot = a.snapshot();
    bad_snapshot.entities[1].rigid_box = invalid;
    const auto before = tools::world_cooker::serialize_source_manifest(a.snapshot());
    require(!a.restore(bad_snapshot) && tools::world_cooker::serialize_source_manifest(a.snapshot()) == before,
        "invalid physical snapshot rejected transactionally");
    require(a.clear_spatial_state(falling) && !a.rigid_box(falling), "clearing spatial state clears physical component");

    tools::world_authoring::Transaction edit;
    edit.operations.push_back(tools::world_authoring::SetRigidBox{id(falling_id), world::RigidBox{}});
    const auto edited = tools::world_authoring::apply(*parsed.snapshot, edit);
    require(edited.success && edited.snapshot.entities[1].rigid_box && !edited.snapshot.entities[1].rigid_box->continuous,
        "agent transaction edits physical settings through shared validator");
    require(!first.advance(std::numeric_limits<double>::quiet_NaN(), &diagnostics), "invalid simulation time rejected");
    return 0;
}
