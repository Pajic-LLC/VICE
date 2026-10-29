#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vice/presentation/assets.hpp>
namespace vice::presentation {
namespace {
bool finite(math::Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool affine(const math::Mat4 &m) {
    return std::all_of(m.values.begin(), m.values.end(),
                       [](float v) { return std::isfinite(v); }) &&
           std::abs(m(3, 0)) < 1e-6F && std::abs(m(3, 1)) < 1e-6F && std::abs(m(3, 2)) < 1e-6F &&
           std::abs(m(3, 3) - 1) < 1e-6F;
}
bool weights_valid(const Skin &skin, const SkinWeights &v) {
    double total = 0;
    for (std::size_t i = 0; i < max_skin_influences; ++i) {
        if (v.joints[i] >= skin.joints.size() || !std::isfinite(v.weights[i]) || v.weights[i] < 0 ||
            v.weights[i] > 1)
            return false;
        total += v.weights[i];
        for (std::size_t j = 0; j < i; ++j)
            if (v.weights[i] > 0 && v.weights[j] > 0 && v.joints[i] == v.joints[j])
                return false;
    }
    return std::abs(total - 1) < 1e-5;
}
bool normal_transform(const math::Mat4 &m, math::Vec3 n, math::Vec3 &out) {
    // Cofactor matrix / determinant is the inverse transpose of the affine linear part.
    const math::Vec3 a{m(0, 0), m(1, 0), m(2, 0)}, b{m(0, 1), m(1, 1), m(2, 1)},
        c{m(0, 2), m(1, 2), m(2, 2)};
    const auto x = math::cross(b, c), y = math::cross(c, a), z = math::cross(a, b);
    const double det = static_cast<double>(math::dot(a, x));
    const double scale =
        std::sqrt(static_cast<double>(math::dot(a, a)) * math::dot(b, b) * math::dot(c, c));
    if (!std::isfinite(det) || !std::isfinite(scale) || scale == 0 || std::abs(det) < 1e-8 * scale)
        return false;
    out = {static_cast<float>((x.x * n.x + y.x * n.y + z.x * n.z) / det),
           static_cast<float>((x.y * n.x + y.y * n.y + z.y * n.z) / det),
           static_cast<float>((x.z * n.x + y.z * n.y + z.z * n.z) / det)};
    return finite(out);
}
} // namespace
const Skin *find_skin(const Rig &r, std::uint32_t node) noexcept {
    const auto i =
        std::find_if(r.skins.begin(), r.skins.end(), [&](const auto &s) { return s.node == node; });
    return i == r.skins.end() ? nullptr : &*i;
}
bool valid_skin(const Rig &r, const Skin &s, std::string *d) {
    const auto fail = [&](const char *message) {
        if (d)
            *d = message;
        return false;
    };
    if (s.node >= r.nodes.size() || !r.nodes[s.node].mesh.valid() || s.joints.empty() ||
        s.joints.size() > 128 || s.vertices.empty() || s.vertices.size() > max_skin_vertices)
        return fail("Invalid skin node, joint or vertex counts.");
    std::unordered_set<std::uint32_t> joints;
    for (const auto &j : s.joints) {
        math::Vec3 n;
        if (j.node >= r.nodes.size() || !joints.insert(j.node).second || !affine(j.inverse_bind) ||
            !normal_transform(j.inverse_bind, {0, 1, 0}, n))
            return fail("Invalid/duplicate skin joint or inverse-bind matrix.");
    }
    for (const auto &v : s.vertices)
        if (!weights_valid(s, v))
            return fail("Skin weights must be finite, normalized, unique and in joint range.");
    return true;
}
bool skin_palette(const Skin &skin, std::span<const Pose> pose, std::vector<math::Mat4> &out) {
    if (skin.joints.empty() || skin.joints.size() > 128)
        return false;
    std::vector<math::Mat4> result;
    result.reserve(skin.joints.size());
    for (const auto &j : skin.joints) {
        if (j.node >= pose.size())
            return false;
        const auto &p = pose[j.node];
        if (!finite(p.translation) ||
            !world::valid(
                world::AuthoredRotation{p.rotation.x, p.rotation.y, p.rotation.z, p.rotation.w}) ||
            !std::isfinite(p.scale) || p.scale <= 0)
            return false;
        auto m = math::rotation(p.rotation);
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned r = 0; r < 3; ++r)
                m(r, c) *= p.scale;
        m(0, 3) = p.translation.x;
        m(1, 3) = p.translation.y;
        m(2, 3) = p.translation.z;
        m = math::multiply(m, j.inverse_bind);
        if (!affine(m))
            return false;
        result.push_back(m);
    }
    out = std::move(result);
    return true;
}
bool deform_skin(const Skin &skin, std::span<const math::Mat4> palette,
                 std::span<const SkinVertex> bind, std::vector<SkinVertex> &out) {
    if (bind.empty() || bind.size() != skin.vertices.size() || bind.size() > max_skin_vertices ||
        palette.size() != skin.joints.size() || palette.empty() || palette.size() > 128)
        return false;
    for (const auto &m : palette)
        if (!affine(m))
            return false;
    std::vector<SkinVertex> result;
    result.reserve(bind.size());
    for (std::size_t i = 0; i < bind.size(); ++i) {
        if (!weights_valid(skin, skin.vertices[i]) || !finite(bind[i].position) ||
            !finite(bind[i].normal))
            return false;
        math::Mat4 blended;
        for (std::size_t k = 0; k < max_skin_influences; ++k) {
            const auto w = skin.vertices[i].weights[k];
            if (w == 0)
                continue;
            const auto &m = palette[skin.vertices[i].joints[k]];
            for (unsigned c = 0; c < 16; ++c)
                blended.values[c] += w * m.values[c];
        }
        const auto p = bind[i].position;
        SkinVertex v;
        v.position = {
            blended(0, 0) * p.x + blended(0, 1) * p.y + blended(0, 2) * p.z + blended(0, 3),
            blended(1, 0) * p.x + blended(1, 1) * p.y + blended(1, 2) * p.z + blended(1, 3),
            blended(2, 0) * p.x + blended(2, 1) * p.y + blended(2, 2) * p.z + blended(2, 3)};
        if (!normal_transform(blended, bind[i].normal, v.normal)) {
            // LBS may collapse at opposing rotations. Use weighted joint normals at that
            // singularity.
            v.normal = {};
            for (std::size_t k = 0; k < max_skin_influences; ++k)
                if (skin.vertices[i].weights[k] > 0) {
                    math::Vec3 n;
                    if (!normal_transform(palette[skin.vertices[i].joints[k]], bind[i].normal, n))
                        return false;
                    const auto w = skin.vertices[i].weights[k];
                    v.normal.x += w * n.x;
                    v.normal.y += w * n.y;
                    v.normal.z += w * n.z;
                }
        }
        if (math::dot(v.normal, v.normal) < 1e-20F) {
            const auto strongest =
                std::max_element(skin.vertices[i].weights.begin(), skin.vertices[i].weights.end());
            const auto slot =
                static_cast<std::size_t>(strongest - skin.vertices[i].weights.begin());
            if (!normal_transform(palette[skin.vertices[i].joints[slot]], bind[i].normal, v.normal))
                return false;
        }
        const auto length = math::dot(v.normal, v.normal);
        if (!finite(v.position) || !std::isfinite(length) || length < 1e-20F)
            return false;
        v.normal = math::normalize(v.normal);
        result.push_back(v);
    }
    out = std::move(result);
    return true;
}
} // namespace vice::presentation
