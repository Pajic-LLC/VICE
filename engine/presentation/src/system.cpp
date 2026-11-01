#include "audio_output.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vice/presentation/system.hpp>
namespace vice::presentation {
namespace {
bool fail(std::string *d, const char *s) {
    if (d)
        *d = s;
    return false;
}
bool active(const world::World &w, world::EntityId e) {
    const auto p = w.spatial_state(e);
    const auto region = p ? w.region(p->region) : std::nullopt;
    return region && region->fidelity != world::RegionFidelity::inactive;
}
Pose root_pose(const world::World &w, world::EntityId e) {
    const auto t = w.authored_transform(e).value_or(world::AuthoredTransform{});
    return {{}, {t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w}, t.scale.x};
}
} // namespace
struct System::Impl {
    world::World *world = nullptr;
    std::uint64_t revision = 0;
    bool has_components = false;
    std::unordered_map<world::ResourceId, Rig, world::ResourceIdHash> rigs;
    std::unordered_map<world::ResourceId, Sound, world::ResourceIdHash> sounds;
    struct Root {
        world::PersistentId id;
        world::EntityId entity;
    };
    struct Part {
        world::PersistentId id;
        world::EntityId entity;
        world::RigPart binding;
    };
    std::vector<Root> roots, emitters;
    std::vector<Part> parts;
    AudioOutput output;
    double remainder = 0;
    bool current() const { return world && world->presentation_revision() == revision; }
    AudioObservation observed_audio(const world::AudioEmitter &p, world::EntityId e) const {
        AudioObservation o;
        o.output_active = output.active();
        o.delivered_frames = output.delivered();
        o.underrun_frames = output.underruns();
        const auto sound = sounds.find(p.sound);
        if (sound == sounds.end())
            return o;
        o.duration = static_cast<double>(sound->second.samples.size()) / 48000;
        if (!p.playing || !active(*world, e))
            return o;
        const auto source = world->spatial_state(e);
        const auto listener = world->primary_camera();
        const auto camera = listener ? world->spatial_state(*listener) : std::nullopt;
        if (!source || !camera)
            return o;
        const double x = source->global_position.x - camera->global_position.x,
                     y = source->global_position.y - camera->global_position.y,
                     z = source->global_position.z - camera->global_position.z;
        const double distance = std::hypot(x, y, z);
        o.distance = static_cast<float>(
            std::min(distance, static_cast<double>(std::numeric_limits<float>::max())));
        if (!std::isfinite(distance) || distance >= p.max_distance)
            return o;
        const auto q =
            world->authored_transform(*listener).value_or(world::AuthoredTransform{}).rotation;
        const auto m = math::rotation({q.x, q.y, q.z, q.w});
        const double pan =
            distance > 1e-6
                ? std::clamp((x * m(0, 0) + y * m(1, 0) + z * m(2, 0)) / distance, -1.0, 1.0)
                : 0;
        const double gain =
            p.gain *
            std::min(1.0, static_cast<double>(p.reference_distance) /
                              std::max(distance, static_cast<double>(p.reference_distance))) *
            (1 - distance / p.max_distance);
        o.left_gain = static_cast<float>(gain * std::sqrt(0.5 * (1 - pan)));
        o.right_gain = static_cast<float>(gain * std::sqrt(0.5 * (1 + pan)));
        return o;
    }
};
System::System() : impl_(std::make_unique<Impl>()) {}
System::~System() = default;
bool System::enabled() const noexcept { return impl_->has_components; }
bool System::reset(world::World &w, const std::filesystem::path &root, std::string *d) {
    auto next = std::make_unique<Impl>();
    next->world = &w;
    next->revision = w.presentation_revision();
    const auto snapshot = w.snapshot();
    for (const auto &e : snapshot.entities) {
        const auto owner = *w.find(e.persistent_id);
        if (e.animation)
            next->roots.push_back({e.persistent_id, owner});
        if (e.audio_emitter)
            next->emitters.push_back({e.persistent_id, owner});
        if (e.rig_part)
            next->parts.push_back({e.persistent_id, owner, *e.rig_part});
    }
    if (next->roots.size() > 256 || next->parts.size() > 4096 || next->emitters.size() > 128)
        return fail(d,
                    "Presentation scene capacity exceeded (256 roots, 4096 parts, 128 emitters).");
    next->has_components = !next->roots.empty() || !next->emitters.empty() || !next->parts.empty();
    if (next->has_components) {
        std::error_code error;
        if (root.empty() || !std::filesystem::is_directory(root, error))
            return fail(d, "Presentation assets require a cooked content directory.");
        std::filesystem::recursive_directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, error),
            end;
        std::uintmax_t asset_bytes = 0;
        std::size_t asset_count = 0;
        for (; !error && it != end; it.increment(error)) {
            if (!it->is_regular_file() || it->is_symlink())
                continue;
            const auto ext = it->path().extension();
            if (ext == ".vrigc" || ext == ".vpcmc") {
                const auto bytes = it->file_size(error);
                if (error || bytes > 64 * 1024 * 1024 - asset_bytes || ++asset_count > 512)
                    return fail(d,
                                "Cooked presentation asset budget exceeded (64 MiB, 512 files).");
                asset_bytes += bytes;
            }
            if (ext == ".vrigc") {
                auto rig = read_rig(it->path(), d);
                if (!rig || !next->rigs.emplace(rig->id, std::move(*rig)).second)
                    return fail(d, "Invalid/duplicate cooked rig.");
            } else if (ext == ".vpcmc") {
                auto sound = read_pcm(it->path(), d);
                if (!sound || !next->sounds.emplace(sound->id, std::move(*sound)).second)
                    return fail(d, "Invalid/duplicate cooked sound.");
            }
        }
        if (error)
            return fail(d, "Could not enumerate presentation assets.");
        for (const auto &r : next->roots) {
            const auto p = *w.animation(r.entity);
            const auto rig = next->rigs.find(p.rig);
            if (rig == next->rigs.end() || (p.clip >= rig->second.clips.size() || (p.blend_weight > 0 && p.blend_clip >= rig->second.clips.size())))
                return fail(d, "Animation references a missing rig or clip.");
            std::vector<Pose> checked;
            if (!sample_world_pose(w, r.entity, rig->second, p.clip, p.time, checked))
                return fail(d, "Invalid physical/animated rig pose.");
        }
        for (const auto &record : snapshot.physical_rig_bindings) {
            const auto root = w.find(record.binding.root);
            const auto animation = root ? w.animation(*root) : std::nullopt;
            const auto rig = animation ? next->rigs.find(animation->rig) : next->rigs.end();
            if (rig == next->rigs.end() || record.binding.node >= rig->second.nodes.size())
                return fail(d, "Physical rig node is out of range.");
        }
        for (const auto &e : next->emitters)
            if (!next->sounds.contains(w.audio_emitter(e.entity)->sound))
                return fail(d, "Emitter references a missing sound.");
        for (const auto &p : next->parts) {
            const auto r = w.find(p.binding.root);
            const auto a = r ? w.animation(*r) : std::nullopt;
            const auto rig = a ? next->rigs.find(a->rig) : next->rigs.end();
            if (rig == next->rigs.end() || p.binding.node >= rig->second.nodes.size())
                return fail(d, "Rig part node is out of range.");
            const auto mesh = rig->second.nodes[p.binding.node].mesh;
            const auto bound = w.resource_reference(p.entity, world::ResourceRole::render_mesh);
            if (mesh.valid() ? bound != mesh : bound.has_value())
                return fail(d, "Rig part mesh binding disagrees with imported rig.");
        }
    }
    impl_ = std::move(next);
    return advance(0, d);
}
std::vector<float> System::render_audio(std::size_t frames) const {
    if (frames > 12000 || !impl_->current())
        return {};
    std::vector<float> stereo(frames * 2, 0);
    for (const auto &e : impl_->emitters) {
        const auto p = impl_->world->audio_emitter(e.entity);
        if (!p)
            continue;
        const auto sound = impl_->sounds.find(p->sound);
        if (sound == impl_->sounds.end())
            continue;
        const auto o = impl_->observed_audio(*p, e.entity);
        if (o.left_gain == 0 && o.right_gain == 0)
            continue;
        const auto &samples = sound->second.samples;
        for (std::size_t i = 0; i < frames; ++i) {
            double position = p->time * 48000 + static_cast<double>(i) * p->pitch;
            if (p->looping)
                position = std::fmod(position, static_cast<double>(samples.size()));
            if (position >= static_cast<double>(samples.size()))
                break;
            const auto first = static_cast<std::size_t>(position);
            const auto second = first + 1 < samples.size() ? first + 1 : (p->looping ? 0 : first);
            const auto fraction = static_cast<float>(position - static_cast<double>(first));
            const float value = samples[first] * (1 - fraction) + samples[second] * fraction;
            stereo[i * 2] += value * o.left_gain;
            stereo[i * 2 + 1] += value * o.right_gain;
        }
    }
    for (auto &v : stereo)
        v = std::clamp(v, -1.0F, 1.0F);
    return stereo;
}
bool System::advance(double seconds, std::string *d) {
    if (!impl_->current()) {
        stop_audio();
        return fail(d, "Presentation structure changed; reset the presentation service.");
    }
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 0.25)
        return fail(d, "Presentation delta must be finite in [0,0.25].");
    if (!enabled())
        return true;
    struct Change {
        world::EntityId entity;
        world::SpatialState spatial;
        world::AuthoredTransform transform;
    };
    std::vector<Change> changes;
    std::vector<std::pair<world::EntityId, world::AnimationPlayer>> players;
    std::vector<std::pair<world::EntityId, world::AudioEmitter>> emitters;
    for (const auto &root : impl_->roots) {
        auto p = impl_->world->animation(root.entity);
        if (!p)
            return fail(d, "Animation root is missing.");
        const auto rig = impl_->rigs.find(p->rig);
        if (rig == impl_->rigs.end() || p->clip >= rig->second.clips.size())
            return fail(d, "Animation clip is invalid.");
        if (!active(*impl_->world, root.entity))
            continue;
        const auto &clip = rig->second.clips[p->clip];
        if (p->playing) {
            p->time += seconds * p->rate;
            if (p->looping)
                p->time = std::fmod(p->time, clip.duration);
            else if (p->time >= clip.duration) {
                p->time = clip.duration;
                p->playing = false;
            }
        }
        if (p->blend_weight > 0) {
            if (p->blend_clip >= rig->second.clips.size()) return fail(d, "Invalid blend clip.");
            const auto duration = rig->second.clips[p->blend_clip].duration;
            if (p->playing) p->blend_time += seconds * p->rate;
            p->blend_time = p->looping ? std::fmod(p->blend_time, duration)
                                     : std::min(p->blend_time, duration);
        }
        std::vector<Pose> pose;
        if (!sample_world_pose(*impl_->world, root.entity, rig->second, *p, pose))
            return fail(d, "Invalid evaluated rig pose.");
        const auto spatial = impl_->world->spatial_state(root.entity);
        const auto transform = root_pose(*impl_->world, root.entity);
        if (!spatial)
            return fail(d, "Animation root is not spatial.");
        for (const auto &part : impl_->parts)
            if (part.binding.root == root.id) {
                if (impl_->world->find(part.id) != part.entity ||
                    impl_->world->rig_part(part.entity) != part.binding)
                    return fail(d, "Rig-part identity changed.");
                const auto mesh = rig->second.nodes[part.binding.node].mesh;
                const auto bound =
                    impl_->world->resource_reference(part.entity, world::ResourceRole::render_mesh);
                if (mesh.valid() ? bound != mesh : bound.has_value())
                    return fail(d, "Rig-part mesh changed.");
                const auto global = find_skin(rig->second, part.binding.node)
                                        ? transform
                                        : compose(transform, pose[part.binding.node]);
                world::SpatialState location{{spatial->global_position.x + global.translation.x,
                                              spatial->global_position.y + global.translation.y,
                                              spatial->global_position.z + global.translation.z},
                                             spatial->region};
                world::AuthoredTransform authored{
                    {global.rotation.x, global.rotation.y, global.rotation.z, global.rotation.w},
                    {global.scale, global.scale, global.scale}};
                if (!world::finite(location.global_position) || !world::valid(authored))
                    return fail(d, "Evaluated rig pose exceeds World bounds.");
                changes.push_back({part.entity, location, authored});
            }
        players.emplace_back(root.entity, *p);
    }
    for (const auto &e : impl_->emitters) {
        auto p = impl_->world->audio_emitter(e.entity);
        if (!p)
            return fail(d, "Audio emitter is missing.");
        const auto sound = impl_->sounds.find(p->sound);
        if (sound == impl_->sounds.end())
            return fail(d, "Sound asset is missing.");
        if (p->playing && active(*impl_->world, e.entity)) {
            const double duration = static_cast<double>(sound->second.samples.size()) / 48000;
            p->time += seconds * p->pitch;
            if (p->looping)
                p->time = std::fmod(p->time, duration);
            else if (p->time >= duration) {
                p->time = duration;
                p->playing = false;
            }
        }
        emitters.emplace_back(e.entity, *p);
    }
    const double wanted = seconds * 48000 + impl_->remainder;
    const auto frames = static_cast<std::size_t>(wanted);
    impl_->remainder = wanted - static_cast<double>(frames);
    const auto mixed = render_audio(frames);
    for (const auto &c : changes)
        if (!impl_->world->set_spatial_state(c.entity, c.spatial) ||
            !impl_->world->set_authored_transform(c.entity, c.transform))
            return fail(d, "Could not publish rig part pose.");
    for (const auto &[e, p] : players)
        if (!impl_->world->set_animation(e, p))
            return fail(d, "Could not publish animation clock.");
    for (const auto &[e, p] : emitters)
        if (!impl_->world->set_audio_emitter(e, p))
            return fail(d, "Could not publish sound clock.");
    impl_->output.push(mixed);
    return true;
}
bool System::control_animation(world::PersistentId id, std::uint32_t clip, float rate, bool playing,
                               bool looping, bool restart) {
    if (!impl_->current())
        return false;
    const auto entity = impl_->world->find(id);
    auto p = entity ? impl_->world->animation(*entity) : std::nullopt;
    if (!p)
        return false;
    const auto rig = impl_->rigs.find(p->rig);
    if (rig == impl_->rigs.end() || clip >= rig->second.clips.size())
        return false;
    if (p->clip != clip || restart)
        p->time = 0;
    p->clip = clip;
    p->blend_weight = 0;
    p->rate = rate;
    p->playing = playing;
    p->looping = looping;
    return impl_->world->set_animation(*entity, *p);
}
std::optional<Pose> System::node_pose(world::PersistentId id, std::uint32_t node, bool physical) const {
    if (!impl_->current()) return std::nullopt;
    const auto entity = impl_->world->find(id);
    const auto p = entity ? impl_->world->animation(*entity) : std::nullopt;
    if (!p) return std::nullopt;
    const auto rig = impl_->rigs.find(p->rig);
    if (rig == impl_->rigs.end() || node >= rig->second.nodes.size()) return std::nullopt;
    std::vector<Pose> pose;
    const bool ok = physical
        ? sample_world_pose(*impl_->world, *entity, rig->second, p->clip, p->time, pose)
        : p->blend_weight > 0
            ? sample_blended_pose(rig->second,p->clip,p->time,p->blend_clip,p->blend_time,p->blend_weight,pose)
            : sample_pose(rig->second,p->clip,p->time,pose);
    return ok ? std::optional<Pose>{pose[node]} : std::nullopt;
}
bool System::control_animation_mix(world::PersistentId id, std::uint32_t clip,
                                   std::uint32_t other, float weight, float rate) {
    if (!impl_->current() || !std::isfinite(weight) || weight < 0 || weight > 1 ||
        !std::isfinite(rate) || rate < 0 || rate > 8) return false;
    const auto entity = impl_->world->find(id);
    auto p = entity ? impl_->world->animation(*entity) : std::nullopt;
    if (!p) return false;
    const auto rig = impl_->rigs.find(p->rig);
    if (rig == impl_->rigs.end() || clip >= rig->second.clips.size() ||
        other >= rig->second.clips.size()) return false;
    if (p->clip != clip) p->time = 0;
    if (p->blend_clip != other) p->blend_time = 0;
    p->clip = clip; p->blend_clip = other; p->blend_weight = weight;
    p->rate = rate; p->playing = true; p->looping = true;
    return impl_->world->set_animation(*entity, *p);
}
bool System::control_audio(world::PersistentId id, float gain, float pitch, bool playing,
                           bool restart) {
    if (!impl_->current())
        return false;
    const auto entity = impl_->world->find(id);
    auto p = entity ? impl_->world->audio_emitter(*entity) : std::nullopt;
    if (!p)
        return false;
    p->gain = gain;
    p->pitch = pitch;
    p->playing = playing;
    if (restart)
        p->time = 0;
    return impl_->world->set_audio_emitter(*entity, *p);
}
std::optional<AnimationObservation> System::animation(world::PersistentId id) const {
    if (!impl_->current())
        return std::nullopt;
    const auto e = impl_->world->find(id);
    const auto p = e ? impl_->world->animation(*e) : std::nullopt;
    if (!p)
        return std::nullopt;
    const auto r = impl_->rigs.find(p->rig);
    if (r == impl_->rigs.end() || p->clip >= r->second.clips.size())
        return std::nullopt;
    const auto count = std::count_if(impl_->parts.begin(), impl_->parts.end(),
                                     [&](const auto &part) { return part.binding.root == id; });
    AnimationObservation observation{r->second.clips[p->clip].name,
                                     r->second.clips[p->clip].duration, r->second.nodes.size(),
                                     static_cast<std::size_t>(count)};
    observation.skin_count = r->second.skins.size();
    for (const auto &skin : r->second.skins) {
        observation.joint_count += skin.joints.size();
        observation.weighted_vertices += skin.vertices.size();
    }
    return observation;
}
std::optional<AudioObservation> System::audio(world::PersistentId id) const {
    if (!impl_->current())
        return std::nullopt;
    const auto e = impl_->world->find(id);
    const auto p = e ? impl_->world->audio_emitter(*e) : std::nullopt;
    if (!p)
        return std::nullopt;
    return impl_->observed_audio(*p, *e);
}
bool System::start_audio() { return !impl_->emitters.empty() && impl_->output.start(); }
void System::stop_audio() { impl_->output.stop(); }
} // namespace vice::presentation
