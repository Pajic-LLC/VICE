#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <map>
#include <vice/presentation/assets.hpp>
#include <vice/runtime/skinning.hpp>
namespace vice::runtime {
namespace {
constexpr std::size_t instance_limit = 256, vertex_limit = 500000, frame_vertex_limit = 200000;
bool fail(std::string *d, const char *text) {
    if (d)
        *d = text;
    return false;
}
world::ResourceId generated(unsigned slot) {
    world::PersistentId::Bytes b{0xfa, 0xdf, 0x53, 0x4b, 0x49, 0x4e, 0x40, 0,
                                 0x80, 0,    0,    0,    0,    0,    0,    0};
    b[14] = static_cast<std::uint8_t>(slot >> 8);
    b[15] = static_cast<std::uint8_t>(slot);
    return {world::PersistentId{b}};
}
std::uint64_t fingerprint(const render::MeshAsset &m) {
    std::uint64_t h = 14695981039346656037ULL;
    for (const auto byte : std::as_bytes(std::span{m.vertices})) {
        h ^= std::to_integer<unsigned char>(byte);
        h *= 1099511628211ULL;
    }
    for (const auto byte : std::as_bytes(std::span{m.indices})) {
        h ^= std::to_integer<unsigned char>(byte);
        h *= 1099511628211ULL;
    }
    return h;
}
} // namespace
struct RuntimeSkinningCache::Impl {
    std::map<std::array<std::uint8_t, 16>, presentation::Rig> rigs;
    struct Entry {
        world::PersistentId entity{}, root{};
        world::ResourceId rig{}, source{}, resource{};
        std::uint32_t node = 0, clip = 0;
        double time = -1, blend_time = -1;
        std::uint32_t blend_clip = 0;
        float blend_weight = -1;
        std::uint64_t source_fingerprint = 0, presentation_revision = 0;
        render::MeshAsset asset;
        std::vector<render::MeshVertex> previous;
        std::vector<math::Mat4> palette;
        bool seen = false, changed = false, settled = false, reduced = false;
        std::uint64_t previous_fingerprint = 0;
        math::Vec3 minimum{}, maximum{};
        unsigned slot = 0;
    };
    std::map<std::array<std::uint8_t, 16>, Entry> entries;
    std::array<bool, instance_limit> slots{};
    std::size_t vertices = 0, evaluated_vertices = 0;
};
RuntimeSkinningCache::RuntimeSkinningCache() : impl_(std::make_unique<Impl>()) {}
RuntimeSkinningCache::~RuntimeSkinningCache() = default;
bool RuntimeSkinningCache::reset(const std::filesystem::path &root, std::string *d) {
    auto next = std::make_unique<Impl>();
    std::error_code ec;
    if (root.empty()) {
        impl_ = std::move(next);
        return true;
    }
    if (!std::filesystem::is_directory(root, ec))
        return fail(d, "Skinning content directory is unavailable.");
    std::uintmax_t bytes = 0;
    std::size_t files = 0;
    for (std::filesystem::recursive_directory_iterator
             i(root, std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         !ec && i != end; i.increment(ec)) {
        if (i->is_symlink() || !i->is_regular_file() || i->path().extension() != ".vrigc")
            continue;
        const auto size = i->file_size(ec);
        if (ec || size > 64 * 1024 * 1024 - bytes || ++files > 512)
            return fail(d, "Skinning rig asset budget exceeded.");
        bytes += size;
        auto r = presentation::read_rig(i->path(), d);
        if (!r || !next->rigs.emplace(r->id.value.bytes(), std::move(*r)).second)
            return fail(d, "Invalid or duplicate skinning rig.");
    }
    if (ec)
        return fail(d, "Could not enumerate skinning assets.");
    impl_ = std::move(next);
    if (d)
        d->clear();
    return true;
}
void RuntimeSkinningCache::begin_frame() {
    impl_->evaluated_vertices = 0;
    for (auto &[id, e] : impl_->entries) {
        (void)id;
        e.seen = false;
        e.changed = false;
    }
}
void RuntimeSkinningCache::end_frame() {
    std::erase_if(impl_->entries, [&](const auto &item) {
        const auto &e = item.second;
        if (e.seen)
            return false;
        impl_->vertices -= e.asset.vertices.size();
        impl_->slots[e.slot] = false;
        return true;
    });
}
SkinningInfo RuntimeSkinningCache::info() const noexcept {
    return {impl_->entries.size(), impl_->vertices, impl_->evaluated_vertices};
}
bool RuntimeSkinningCache::evaluate(const world::World &w, world::PersistentId id,
                                    const render::MeshAsset &bind, SkinningMeshView &out,
                                    std::string *d) {
    out = {};
    const auto entity = w.find(id);
    const auto part = entity ? w.rig_part(*entity) : std::nullopt;
    if (!part)
        return true;
    const auto owner = w.find(part->root);
    const auto p = owner ? w.animation(*owner) : std::nullopt;
    if (!p)
        return fail(d, "Skinned root playback state is missing.");
    const auto r = impl_->rigs.find(p->rig.value.bytes());
    if (r == impl_->rigs.end())
        return fail(d, "Skinned/rigid part references an unavailable rig.");
    const auto &rig = r->second;
    const auto *skin = presentation::find_skin(rig, part->node);
    if (!skin)
        return true;
    const auto source = w.resource_reference(*entity, world::ResourceRole::render_mesh);
    if (!source || part->node >= rig.nodes.size() || rig.nodes[part->node].mesh != source ||
        bind.stable_id != source->value.bytes() || bind.vertices.size() != skin->vertices.size() ||
        bind.indices.empty())
        return fail(d, "Skin binding/source vertex topology is inconsistent.");
    const auto spatial = w.spatial_state(*owner);
    const auto region = spatial ? w.region(spatial->region) : std::nullopt;
    double time = p->time;
    if (region && region->fidelity == world::RegionFidelity::reduced)
        time = std::floor(time * 15.0) / 15.0;
    auto found = impl_->entries.find(id.bytes());
    bool fresh = found == impl_->entries.end();
    if (!fresh && (found->second.source != source || found->second.rig != p->rig ||
                   found->second.root != part->root || found->second.node != part->node ||
                   found->second.source_fingerprint != bind.source_fingerprint)) {
        impl_->vertices -= found->second.asset.vertices.size();
        impl_->slots[found->second.slot] = false;
        impl_->entries.erase(found);
        fresh = true;
    }
    if (fresh) {
        if (impl_->entries.size() >= instance_limit ||
            bind.vertices.size() > vertex_limit - impl_->vertices)
            return fail(d, "Skinned instance/vertex cache capacity exceeded.");
        const auto slot = std::find(impl_->slots.begin(), impl_->slots.end(), false);
        if (slot == impl_->slots.end())
            return fail(d, "Skinned geometry slots exhausted.");
        Impl::Entry e;
        e.entity = id;
        e.root = part->root;
        e.rig = p->rig;
        e.source = *source;
        e.node = part->node;
        e.source_fingerprint = bind.source_fingerprint;
        e.slot = static_cast<unsigned>(slot - impl_->slots.begin());
        e.resource = generated(e.slot + 1);
        e.asset.vertices = bind.vertices;
        e.asset.indices = bind.indices;
        e.asset.version = render::mesh_asset_format_version_v2;
        e.asset.stable_id = e.resource.value.bytes();
        e.asset.meshlet_geometry = {};
        e.asset.virtual_geometry_pages = {};
        e.asset.virtual_geometry_proxies = {};
        impl_->vertices += e.asset.vertices.size();
        *slot = true;
        found = impl_->entries.emplace(id.bytes(), std::move(e)).first;
    }
    auto &entry = found->second;
    entry.seen = true;
    entry.reduced = region && region->fidelity == world::RegionFidelity::reduced;
    const bool restored = entry.presentation_revision != w.presentation_revision();
    if (entry.time != time || entry.clip != p->clip || entry.blend_time != p->blend_time ||
        entry.blend_clip != p->blend_clip || entry.blend_weight != p->blend_weight || restored ||
        w.has_physical_rig_bindings(part->root)) {
        std::vector<presentation::Pose> pose;
        std::vector<math::Mat4> palette;
        std::vector<presentation::SkinVertex> input, result;
        if (!presentation::sample_world_pose(w, *owner, rig, p->clip, time, pose) ||
            !presentation::skin_palette(*skin, pose, palette))
            return fail(d, "Could not evaluate skeletal pose/palette.");
        const bool unchanged =
            palette.size() == entry.palette.size() &&
            std::equal(palette.begin(), palette.end(), entry.palette.begin(),
                       [](const auto &a, const auto &b) { return a.values == b.values; });
        if (!fresh && !restored && unchanged) {
            entry.time = time;
            entry.clip = p->clip;
            entry.blend_time = p->blend_time;
            entry.blend_clip = p->blend_clip;
            entry.blend_weight = p->blend_weight;
            if (!entry.settled) {
                entry.previous = entry.asset.vertices;
                entry.previous_fingerprint = entry.asset.source_fingerprint;
                entry.settled = true;
            }
            out = {entry.resource, &entry.asset,  entry.previous,
                   false,          entry.minimum, entry.maximum};
            if (d)
                d->clear();
            return true;
        }
        if (bind.vertices.size() > frame_vertex_limit - impl_->evaluated_vertices)
            return fail(d, "Per-frame CPU skinning budget exceeded; reduce active fidelity/count.");
        input.reserve(bind.vertices.size());
        for (const auto &v : bind.vertices)
            input.push_back({{v.position[0], v.position[1], v.position[2]},
                             {v.normal[0], v.normal[1], v.normal[2]}});
        if (!presentation::deform_skin(*skin, palette, input, result))
            return fail(d, "Skin deformation produced invalid positions/normals.");
        entry.palette = std::move(palette);
        entry.previous = entry.asset.vertices;
        entry.previous_fingerprint = entry.asset.source_fingerprint;
        const auto infinity = std::numeric_limits<float>::infinity();
        entry.minimum = {infinity, infinity, infinity};
        entry.maximum = {-infinity, -infinity, -infinity};
        for (const auto &v : result) {
            entry.minimum = {std::min(entry.minimum.x, v.position.x),
                             std::min(entry.minimum.y, v.position.y),
                             std::min(entry.minimum.z, v.position.z)};
            entry.maximum = {std::max(entry.maximum.x, v.position.x),
                             std::max(entry.maximum.y, v.position.y),
                             std::max(entry.maximum.z, v.position.z)};
        }
        for (std::size_t i = 0; i < result.size(); ++i) {
            auto &v = entry.asset.vertices[i];
            const auto &s = result[i];
            v.position[0] = s.position.x;
            v.position[1] = s.position.y;
            v.position[2] = s.position.z;
            v.normal[0] = s.normal.x;
            v.normal[1] = s.normal.y;
            v.normal[2] = s.normal.z;
        }
        if (fresh || restored)
            entry.previous = entry.asset.vertices;
        entry.presentation_revision = w.presentation_revision();
        entry.asset.source_fingerprint = fingerprint(entry.asset);
        if (fresh || restored)
            entry.previous_fingerprint = entry.asset.source_fingerprint;
        entry.reduced = region && region->fidelity == world::RegionFidelity::reduced;
        entry.time = time;
        entry.clip = p->clip;
            entry.blend_time = p->blend_time;
            entry.blend_clip = p->blend_clip;
            entry.blend_weight = p->blend_weight;
        entry.changed = true;
        entry.settled = false;
        impl_->evaluated_vertices += bind.vertices.size();
    } else if (!entry.settled) {
        entry.previous = entry.asset.vertices;
        entry.previous_fingerprint = entry.asset.source_fingerprint;
        entry.settled = true;
    }
    out = {entry.resource, &entry.asset,  entry.previous,
           entry.changed,  entry.minimum, entry.maximum};
    if (d)
        d->clear();
    return true;
}
std::optional<SkinningMeshView> RuntimeSkinningCache::find(world::ResourceId resource) const {
    for (const auto &[id, e] : impl_->entries) {
        (void)id;
        if (e.resource == resource)
            return SkinningMeshView{e.resource, &e.asset,  e.previous,
                                    e.changed,  e.minimum, e.maximum};
    }
    return {};
}
std::vector<SkinningObservation>
RuntimeSkinningCache::observations(world::PersistentId endpoint) const {
    std::vector<SkinningObservation> result;
    for (const auto &[id, e] : impl_->entries) {
        (void)id;
        if (e.entity != endpoint && e.root != endpoint)
            continue;
        if (result.size() == 8)
            break;
        result.push_back({e.entity, e.root, e.source, e.resource, e.asset.vertices.size(), e.clip,
                          e.time, e.asset.source_fingerprint, e.previous_fingerprint, e.minimum,
                          e.maximum, e.reduced, false});
    }
    return result;
}
bool RuntimeSkinningCache::apply(const world::World &w, CookedMeshCatalog &catalog,
                                 std::vector<RenderMeshInstance> &instances, std::string *d) {
    std::vector<world::ResourceId> old;
    for (const auto &[id, e] : impl_->entries) {
        (void)id;
        old.push_back(e.resource);
    }
    begin_frame();
    for (auto &[id, e] : impl_->entries) {
        (void)id;
        e.seen = std::any_of(instances.begin(), instances.end(),
                             [&](const auto &i) { return i.entity == e.entity; });
    }
    end_frame();
    begin_frame();
    for (auto &i : instances) {
        const auto entity = w.find(i.entity);
        if (!entity || !w.rig_part(*entity))
            continue;
        const auto *source = catalog.find(i.mesh);
        if (!source)
            return fail(d, "Missing skin bind mesh.");
        SkinningMeshView view;
        if (!evaluate(w, i.entity, source->asset, view, d))
            return false;
        if (!view.asset)
            continue;
        if (view.changed &&
            !catalog.upsert_runtime_record(
                {view.resource, {}, *view.asset, true, view.previous.size_bytes()}, d))
            return false;
        i.mesh = view.resource;
        const auto part = w.rig_part(*entity);
        const auto root = part ? w.find(part->root) : std::nullopt;
        if (!root || !w.spatial_state(*root))
            return fail(d, "Skin root must be spatial.");
        i.spatial = *w.spatial_state(*root);
        i.authored = w.authored_transform(*root).value_or(world::AuthoredTransform{});
    }
    end_frame();
    for (auto id : old)
        if (!find(id))
            catalog.erase_runtime_record(id);
    return true;
}
} // namespace vice::runtime
