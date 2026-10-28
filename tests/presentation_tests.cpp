#include "presentation_fixture.hpp"
#include <algorithm>
#include <limits>
#include <vice/tools/world_edit.hpp>
int main() {
    using namespace fixture;
    Data f;
    auto &w = f.world;
    const auto root = *w.find(actor), vehicle = *w.find(car);
    const auto source = tools::world_cooker::serialize_source_manifest(w.snapshot());
    const auto parsed = tools::world_cooker::parse_source_manifest(source);
    require(parsed.snapshot &&
                tools::world_cooker::serialize_source_manifest(*parsed.snapshot) == source,
            "canonical presentation source round trip");
    require(!w.clear_animation(root) && !w.destroy_entity(root) &&
                !w.destroy_resource(*w.find_resource({rig})),
            "live rig/parts and direct resource references are protected");
    auto bad = w.snapshot();
    for (auto &e : bad.entities)
        if (e.rig_part) {
            e.rig_part->root = beacon;
            break;
        }
    require(!w.restore(bad) && w.animation(root).has_value(), "bad binding restore is atomic");
    world::AuthoredTransform scale;
    scale.scale = {1, 2, 1};
    require(!w.set_authored_transform(root, scale),
            "nonuniform animated-root scale rejected before shear");
    auto part = std::find_if(parsed.snapshot->entities.begin(), parsed.snapshot->entities.end(),
                             [](const auto &e) { return e.rig_part && e.rig_part->node == 7; });
    require(part != parsed.snapshot->entities.end() &&
                !w.set_rigid_box(*w.find(part->persistent_id), {}) &&
                !w.set_character(*w.find(part->persistent_id), {}),
            "animated visual parts cannot independently become physical");
    tools::world_authoring::Transaction tx;
    tx.operations = {tools::world_authoring::SetAnimation{
        actor, world::AnimationPlayer{{rig}, 2, 0.125, 0.5F, true, true}}};
    require(static_cast<bool>(tools::world_authoring::apply(w.snapshot(), tx)),
            "typed animation edit");
    const auto request = tools::world_authoring::parse_edit_request(
        "vice-world-edit 1\nrevision sha256:" + std::string(64, 'a') + "\nset_audio_emitter " +
        car.to_string() + " a4000000-0000-4000-8000-000000000301 0 0.2 1 2 40 1 1\n");
    require(request.request &&
                tools::world_authoring::apply(w.snapshot(), request.request->transaction),
            "typed audio file edit");
    auto asset = presentation::read_rig(f.workspace.cooked_root / "character/character.vrigc");
    require(asset && asset->nodes.size() == 13 && asset->clips.size() == 3,
            "actual imported rig and three clips");
    std::vector<presentation::Pose> pose;
    require(presentation::sample_pose(*asset, 1, 0.125, pose) &&
                std::abs(pose[7].rotation.x - std::sin(-0.125F)) < 0.002F,
            "quaternion interpolation samples between imported walk keys");
    auto short_arc = *asset;
    auto &keys = short_arc.clips[0].tracks[1].keys;
    keys[0].value = {0, 0, 0, 1};
    keys[1].value = {0, 0, 0, -1};
    keys[2].value = {0, 0, 0, 1};
    require(presentation::sample_pose(short_arc, 0, 0.25, pose) &&
                std::abs(pose[3].rotation.w) > 0.99F,
            "quaternion antipodes follow the short arc without collapsing");
    auto truncated_rig = presentation::serialize_rig(*asset);
    truncated_rig.resize(truncated_rig.rfind(' '));
    {
        std::ofstream out(f.path / "truncated.vrig");
        out << truncated_rig;
    }
    require(!presentation::read_rig(f.path / "truncated.vrig"),
            "truncated last track must not silently become a partial clip");
    auto invalid = *asset;
    invalid.nodes[1].parent = 1;
    require(!presentation::valid(invalid), "rig parent cycle rejected");
    invalid = *asset;
    invalid.clips[1].tracks[0].keys[1].time = 0;
    require(!presentation::valid(invalid), "nonmonotonic animation keys rejected");
    const auto imported = f.path / "imported";
    std::string d;
    require(tools::import_rig(std::filesystem::path{VICE_PRESENTATION_EXAMPLE} /
                                  "sources/humanoid.gltf",
                              imported, {rig}, actor, &d),
            "real glTF import");
    require(text(imported / "character.vrig") ==
                text(f.workspace.asset_root / "character/character.vrig"),
            "import is deterministic against committed rig output");
    require(!tools::import_rig(std::filesystem::path{VICE_PRESENTATION_EXAMPLE} /
                                   "sources/humanoid.gltf",
                               imported, {rig}, actor, &d),
            "import does not overwrite an existing destination");
    auto input = text(std::filesystem::path{VICE_PRESENTATION_EXAMPLE} / "sources/humanoid.gltf");
    input.insert(1, "\"skins\":[{}],");
    {
        std::ofstream out(f.path / "unsupported.gltf");
        out << input;
    }
    require(
        !tools::import_rig(f.path / "unsupported.gltf", f.path / "bad-import", {rig}, actor, &d) &&
            !std::filesystem::exists(f.path / "bad-import"),
        "unsupported skins fail without partial publication");
    input = text(std::filesystem::path{VICE_PRESENTATION_EXAMPLE} / "sources/humanoid.gltf");
    const auto uri = input.find("data:application/octet-stream;base64,");
    require(uri != std::string::npos, "embedded fixture buffer");
    input.replace(uri, input.find('"', uri) - uri, "../outside.bin");
    {
        std::ofstream out(f.path / "external.gltf");
        out << input;
    }
    require(
        !tools::import_rig(f.path / "external.gltf", f.path / "external-import", {rig}, actor, &d),
        "external buffer paths rejected before loading");
    presentation::System system;
    require(system.reset(w, f.workspace.cooked_root, &d), "shared presentation bootstrap");
    const auto original_part = w.authored_transform(*w.find(part->persistent_id));
    require(system.control_animation(actor, 2, 0.5F, true, true, true) && system.advance(0.25, &d),
            "play imported riding clip");
    require(w.animation(root)->time == 0.125 &&
                w.authored_transform(*w.find(part->persistent_id)) != original_part,
            "animation clock and posed visual parts publish to World");
    const auto save = world::encode_world_snapshot(w.snapshot());
    const auto decoded = world::decode_world_snapshot(save.bytes);
    world::World resumed;
    require(decoded.snapshot && resumed.restore(*decoded.snapshot),
            "save animation and audio state");
    presentation::System continuation;
    require(continuation.reset(resumed, f.workspace.cooked_root, &d) &&
                resumed.animation(*resumed.find(actor)) == w.animation(root),
            "resume exact clip/time/rate without resetting animation");
    require(system.advance(.125) && continuation.advance(.125) &&
                resumed.authored_transform(*resumed.find(part->persistent_id)) ==
                    w.authored_transform(*w.find(part->persistent_id)),
            "resumed animation agrees with uninterrupted evaluation");
    // Pure mixer consumes World/cooked assets even with native output disabled.
    require(system.control_audio(beacon, 0, 1, false, false) &&
                system.control_audio(car, 0.5F, 1, true, true),
            "configure deterministic audio fixture");
    require(w.set_spatial_state(*w.find(camera), {{0, 0, 0}, {1}}) &&
                w.set_authored_transform(*w.find(camera), {}) &&
                w.set_spatial_state(vehicle, {{10, 0, 0}, {1}}),
            "listener and source placement");
    auto pcm = system.render_audio(4800);
    double left = 0, right = 0;
    for (std::size_t i = 0; i < pcm.size(); i += 2) {
        left += pcm[i] * pcm[i];
        right += pcm[i + 1] * pcm[i + 1];
    }
    require(right > 0.01 && left < 1e-9, "actual stereo PCM pans a right-side source");
    require(w.set_spatial_state(vehicle, {{-10, 0, 0}, {1}}), "move source left");
    pcm = system.render_audio(4800);
    left = right = 0;
    for (std::size_t i = 0; i < pcm.size(); i += 2) {
        left += pcm[i] * pcm[i];
        right += pcm[i + 1] * pcm[i + 1];
    }
    require(left > 0.01 && right < 1e-9,
            "actual stereo PCM reverses pan with authoritative position");
    const float near_gain = system.audio(car)->left_gain;
    require(w.set_spatial_state(vehicle, {{-20, 0, 0}, {1}}) &&
                system.audio(car)->left_gain < near_gain,
            "distance attenuation uses World coordinates");
    require(w.set_spatial_state(vehicle, {{-100, 0, 0}, {1}}), "out-of-range source");
    pcm = system.render_audio(4800);
    require(std::all_of(pcm.begin(), pcm.end(), [](float x) { return x == 0; }),
            "out-of-range source mixes silence");
    require(w.set_spatial_state(vehicle, {{1e200, 0, 0}, {1}}) &&
                std::isfinite(system.audio(car)->distance),
            "huge global source distance remains finite in observations");
    const double before = w.audio_emitter(vehicle)->time;
    require(system.control_audio(car, .5F, 2, true, true) && system.advance(.125) &&
                std::abs(w.audio_emitter(vehicle)->time - .25) < 1e-9,
            "pitch advances saved playback clock");
    (void)before;
    auto wav = text(f.workspace.asset_root / "audio/engine.wav");
    wav[40] = static_cast<char>(255);
    wav[41] = static_cast<char>(255);
    wav[42] = static_cast<char>(255);
    wav[43] = static_cast<char>(127);
    {
        std::ofstream out(f.path / "bad.wav", std::ios::binary);
        out << wav;
    }
    require(!tools::import_wav(f.path / "bad.wav", {rig}, &d), "malformed WAV length rejected");
    const auto empty = world::World{};
    auto old = world::encode_world_snapshot(empty.snapshot()).bytes;
    old.resize(old.size() - 12);
    old[8] = std::byte{14};
    rehash(old);
    require(world::decode_world_snapshot(old).snapshot.has_value(),
            "v14 remains readable without presentation records");
    auto truncated = save.bytes;
    truncated.pop_back();
    rehash(truncated);
    require(!world::decode_world_snapshot(truncated), "truncated presentation record rejected");
    require(w.set_region_fidelity({1}, world::RegionFidelity::inactive), "dormant region");
    const auto animation_time = w.animation(root)->time,
               audio_time = w.audio_emitter(vehicle)->time;
    require(system.advance(.125) && w.animation(root)->time == animation_time &&
                w.audio_emitter(vehicle)->time == audio_time,
            "inactive presentation freezes clocks");
    const auto step_snapshot = w.snapshot();
    require(w.restore(step_snapshot) && !system.advance(.01),
            "World replacement invalidates cached presentation even at equal counts");
    std::cout << "Imported rig, saved animation and deterministic spatial PCM passed\n";
}
