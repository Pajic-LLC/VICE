#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_set>
#include <vice/presentation/assets.hpp>
namespace vice::presentation {
namespace {
bool fail(std::string *d, const char *s) {
    if (d)
        *d = s;
    return false;
}
bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool unit(math::Quat q) { return world::valid(world::AuthoredRotation{q.x, q.y, q.z, q.w}); }
bool name_ok(const std::string &n) {
    return !n.empty() && n.size() <= 128 &&
           std::none_of(n.begin(), n.end(), [](unsigned char c) { return c < 32; });
}
math::Vec3 rotate(math::Quat q, math::Vec3 v) {
    const auto m = math::rotation(q);
    return {m(0, 0) * v.x + m(0, 1) * v.y + m(0, 2) * v.z,
            m(1, 0) * v.x + m(1, 1) * v.y + m(1, 2) * v.z,
            m(2, 0) * v.x + m(2, 1) * v.y + m(2, 2) * v.z};
}
math::Quat multiply(math::Quat a, math::Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
math::Quat normalize(math::Quat q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / len, q.y / len, q.z / len, q.w / len};
}
math::Quat slerp(std::array<float, 4> a, std::array<float, 4> b, float t) {
    float d = 0;
    for (unsigned i = 0; i < 4; ++i)
        d += a[i] * b[i];
    if (d < 0) {
        for (auto &x : b)
            x = -x;
        d = -d;
    }
    float wa = 1 - t, wb = t;
    if (d < 0.9995F) {
        const float angle = std::acos(std::clamp(d, -1.0F, 1.0F));
        wa = std::sin((1 - t) * angle) / std::sin(angle);
        wb = std::sin(t * angle) / std::sin(angle);
    }
    return normalize({wa * a[0] + wb * b[0], wa * a[1] + wb * b[1], wa * a[2] + wb * b[2],
                      wa * a[3] + wb * b[3]});
}
bool pose_ok(const Pose &p) {
    return finite(p.translation) && unit(p.rotation) && std::isfinite(p.scale) && p.scale > 0 &&
           p.scale <= 1e6F;
}
} // namespace
Pose compose(const Pose &parent, const Pose &child) {
    const auto t = rotate(parent.rotation,
                          {child.translation.x * parent.scale, child.translation.y * parent.scale,
                           child.translation.z * parent.scale});
    return {{parent.translation.x + t.x, parent.translation.y + t.y, parent.translation.z + t.z},
            normalize(multiply(parent.rotation, child.rotation)),
            parent.scale * child.scale};
}
bool valid(const Rig &r, std::string *d) {
    if (!r.id.valid() || r.nodes.empty() || r.nodes.size() > 128 || r.clips.empty() ||
        r.clips.size() > 64)
        return fail(d, "Invalid rig identity/counts.");
    for (std::size_t i = 0; i < r.nodes.size(); ++i) {
        const auto &n = r.nodes[i];
        if (!name_ok(n.name) || n.parent < -1 || n.parent >= static_cast<int>(i) ||
            !pose_ok(n.rest))
            return fail(d, "Invalid rig node, parent order or uniform TRS.");
    }
    std::unordered_set<std::string> names;
    std::size_t total = 0;
    for (const auto &c : r.clips) {
        if (!name_ok(c.name) || !names.insert(c.name).second || !std::isfinite(c.duration) ||
            c.duration <= 0 || c.duration > 3600 || c.tracks.size() > 384)
            return fail(d, "Invalid animation clip.");
        std::unordered_set<unsigned> channels;
        for (const auto &t : c.tracks) {
            if (t.node >= r.nodes.size() ||
                (t.path != Path::translation && t.path != Path::rotation &&
                 t.path != Path::scale) ||
                (t.interpolation != Interpolation::linear &&
                 t.interpolation != Interpolation::step) ||
                t.keys.empty() || t.keys.size() > 4096 ||
                !channels.insert(t.node * 3 + static_cast<unsigned>(t.path)).second)
                return fail(d, "Invalid or duplicate animation track.");
            total += t.keys.size();
            if (total > 262144)
                return fail(d, "Rig key capacity exceeded.");
            double previous = -1;
            for (const auto &k : t.keys) {
                if (!std::isfinite(k.time) || k.time < 0 || k.time <= previous ||
                    k.time > c.duration)
                    return fail(d, "Invalid key time/order.");
                previous = k.time;
                for (float v : k.value)
                    if (!std::isfinite(v))
                        return fail(d, "Nonfinite animation key.");
                if (t.path == Path::rotation &&
                    !unit({k.value[0], k.value[1], k.value[2], k.value[3]}))
                    return fail(d, "Animation rotation must be a unit quaternion.");
                if (t.path == Path::scale && (k.value[0] <= 0 || k.value[0] > 1e3F))
                    return fail(d, "Invalid animation scale.");
            }
        }
    }
    return true;
}
std::optional<Rig> read_rig(const std::filesystem::path &path, std::string *d) {
    std::error_code ec;
    if (std::filesystem::file_size(path, ec) > 16 * 1024 * 1024 || ec) {
        fail(d, "Rig is missing or exceeds 16 MiB.");
        return std::nullopt;
    }
    const auto invalid = [&]() -> std::optional<Rig> {
        fail(d, "Invalid, truncated or excessive rig record.");
        return std::nullopt;
    };
    std::ifstream in(path);
    in.imbue(std::locale::classic());
    std::string word, id;
    unsigned version = 0;
    Rig r;
    if (!(in >> word >> version) || word != "vice-rig" || version != 1 || !(in >> word >> id) ||
        word != "id")
        return invalid();
    auto parsed = world::PersistentId::parse(id);
    if (!parsed)
        return invalid();
    r.id = {*parsed};
    Clip *clip = nullptr;
    std::size_t total_keys = 0;
    while (in >> word) {
        if (word == "node") {
            Node n;
            std::string mesh;
            if (r.nodes.size() >= 128 ||
                !(in >> std::quoted(n.name) >> n.parent >> n.rest.translation.x >>
                  n.rest.translation.y >> n.rest.translation.z >> n.rest.rotation.x >>
                  n.rest.rotation.y >> n.rest.rotation.z >> n.rest.rotation.w >> n.rest.scale >>
                  mesh))
                return invalid();
            if (mesh != "none") {
                parsed = world::PersistentId::parse(mesh);
                if (!parsed)
                    return invalid();
                n.mesh = {*parsed};
            }
            r.nodes.push_back(n);
        } else if (word == "clip") {
            Clip c;
            if (r.clips.size() >= 64 || !(in >> std::quoted(c.name) >> c.duration))
                return invalid();
            r.clips.push_back(std::move(c));
            clip = &r.clips.back();
        } else if (word == "track" && clip) {
            Track t;
            unsigned path_value = 0, interpolation = 0;
            std::size_t count = 0;
            if (!(in >> t.node >> path_value >> interpolation >> count) || count > 4096 ||
                clip->tracks.size() >= 384 || total_keys > 262144 - count)
                return invalid();
            total_keys += count;
            t.path = static_cast<Path>(path_value);
            t.interpolation = static_cast<Interpolation>(interpolation);
            t.keys.resize(count);
            for (auto &k : t.keys)
                if (!(in >> k.time >> k.value[0] >> k.value[1] >> k.value[2] >> k.value[3]))
                    return invalid();
            clip->tracks.push_back(std::move(t));
        } else
            return invalid();
    }
    if (!in.eof() || !valid(r, d))
        return std::nullopt;
    return r;
}
std::string serialize_rig(const Rig &r) {
    if (!valid(r))
        return {};
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::setprecision(17) << "vice-rig 1\nid " << r.id.value.to_string() << '\n';
    for (const auto &n : r.nodes)
        o << "node " << std::quoted(n.name) << ' ' << n.parent << ' ' << n.rest.translation.x << ' '
          << n.rest.translation.y << ' ' << n.rest.translation.z << ' ' << n.rest.rotation.x << ' '
          << n.rest.rotation.y << ' ' << n.rest.rotation.z << ' ' << n.rest.rotation.w << ' '
          << n.rest.scale << ' ' << (n.mesh.valid() ? n.mesh.value.to_string() : "none") << '\n';
    for (const auto &c : r.clips) {
        o << "clip " << std::quoted(c.name) << ' ' << c.duration << '\n';
        for (const auto &t : c.tracks) {
            o << "track " << t.node << ' ' << static_cast<unsigned>(t.path) << ' '
              << static_cast<unsigned>(t.interpolation) << ' ' << t.keys.size();
            for (const auto &k : t.keys)
                o << ' ' << k.time << ' ' << k.value[0] << ' ' << k.value[1] << ' ' << k.value[2]
                  << ' ' << k.value[3];
            o << '\n';
        }
    }
    return o.str();
}
bool sample_pose(const Rig &r, std::uint32_t index, double time, std::vector<Pose> &out) {
    if (index >= r.clips.size() || !std::isfinite(time))
        return false;
    out.clear();
    out.reserve(r.nodes.size());
    for (const auto &n : r.nodes)
        out.push_back(n.rest);
    for (const auto &t : r.clips[index].tracks) {
        if (t.keys.empty() || t.node >= out.size())
            return false;
        auto upper = std::upper_bound(t.keys.begin(), t.keys.end(), time,
                                      [](double value, const Key &k) { return value < k.time; });
        const auto &a = upper == t.keys.begin() ? t.keys.front() : *(upper - 1);
        const auto &b = upper == t.keys.end() ? t.keys.back() : *upper;
        const float f =
            t.interpolation == Interpolation::step || b.time <= a.time
                ? 0.0F
                : static_cast<float>(std::clamp((time - a.time) / (b.time - a.time), 0.0, 1.0));
        auto v = a.value;
        for (unsigned i = 0; i < 4; ++i)
            v[i] = (1 - f) * a.value[i] + f * b.value[i];
        auto &p = out[t.node];
        if (t.path == Path::rotation)
            p.rotation = slerp(a.value, b.value, f);
        else if (t.path == Path::translation)
            p.translation = {v[0], v[1], v[2]};
        else
            p.scale = v[0];
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (r.nodes[i].parent >= static_cast<int>(i) || r.nodes[i].parent < -1)
            return false;
        if (r.nodes[i].parent >= 0)
            out[i] = compose(out[static_cast<std::size_t>(r.nodes[i].parent)], out[i]);
        if (!pose_ok(out[i]))
            return false;
    }
    return true;
}
std::optional<Sound> read_pcm(const std::filesystem::path &path, std::string *d) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size < 32 || size > 16 * 1024 * 1024) {
        fail(d, "PCM asset missing or exceeds bounds.");
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    const unsigned char magic[8] = {'V', 'I', 'C', 'E', 'P', 'C', 'M', '1'};
    if (!in || !std::equal(bytes.begin(), bytes.begin() + 8, magic)) {
        fail(d, "Invalid PCM asset header.");
        return std::nullopt;
    }
    Sound s;
    world::PersistentId::Bytes id{};
    std::copy(bytes.begin() + 8, bytes.begin() + 24, id.begin());
    s.id = {world::PersistentId{id}};
    const auto u32 = [&](std::size_t o) {
        std::uint32_t v = 0;
        for (unsigned j = 0; j < 4; ++j)
            v |= static_cast<std::uint32_t>(bytes[o + j]) << (j * 8);
        return v;
    };
    s.sample_rate = u32(24);
    const auto count = u32(28);
    if (!s.id.valid() || s.sample_rate != 48000 || count == 0 || count > 4000000 ||
        bytes.size() != 32ULL + 4ULL * count) {
        fail(d, "Invalid PCM counts/rate/identity.");
        return std::nullopt;
    }
    s.samples.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        s.samples[i] = std::bit_cast<float>(u32(32 + 4 * i));
        if (!std::isfinite(s.samples[i]) || std::abs(s.samples[i]) > 1) {
            fail(d, "Invalid PCM sample.");
            return std::nullopt;
        }
    }
    return s;
}
bool write_pcm(const std::filesystem::path &path, const Sound &s, std::string *d) {
    if (!s.id.valid() || s.sample_rate != 48000 || s.samples.empty() || s.samples.size() > 4000000)
        return fail(d, "Invalid sound asset.");
    for (float x : s.samples)
        if (!std::isfinite(x) || std::abs(x) > 1)
            return fail(d, "Invalid sound sample.");
    std::ofstream out(path, std::ios::binary);
    out.write("VICEPCM1", 8);
    for (auto x : s.id.value.bytes())
        out.put(static_cast<char>(x));
    const auto u32 = [&](std::uint32_t v) {
        for (unsigned i = 0; i < 4; ++i)
            out.put(static_cast<char>((v >> (i * 8)) & 255));
    };
    u32(s.sample_rate);
    u32(static_cast<std::uint32_t>(s.samples.size()));
    for (float x : s.samples) {
        if (!std::isfinite(x) || std::abs(x) > 1)
            return fail(d, "Invalid sound sample.");
        u32(std::bit_cast<std::uint32_t>(x));
    }
    return static_cast<bool>(out) || fail(d, "Could not write PCM asset.");
}
} // namespace vice::presentation
