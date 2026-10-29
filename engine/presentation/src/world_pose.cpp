#include <algorithm>
#include <cmath>
#include <vice/presentation/assets.hpp>
namespace vice::presentation {
namespace {
Pose inverse(Pose p) {
    const auto q = p.rotation;
    const double n =
        std::sqrt(double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w);
    p.rotation = {static_cast<float>(-q.x / n), static_cast<float>(-q.y / n),
                  static_cast<float>(-q.z / n), static_cast<float>(q.w / n)};
    p.scale = 1 / p.scale;
    const auto m = math::rotation(p.rotation);
    const auto t = p.translation;
    p.translation = {-(m(0, 0) * t.x + m(0, 1) * t.y + m(0, 2) * t.z) * p.scale,
                     -(m(1, 0) * t.x + m(1, 1) * t.y + m(1, 2) * t.z) * p.scale,
                     -(m(2, 0) * t.x + m(2, 1) * t.y + m(2, 2) * t.z) * p.scale};
    return p;
}
bool finite(const Pose &p) {
    return std::isfinite(p.translation.x) && std::isfinite(p.translation.y) &&
           std::isfinite(p.translation.z) && std::isfinite(p.rotation.x) &&
           std::isfinite(p.rotation.y) && std::isfinite(p.rotation.z) &&
           std::isfinite(p.rotation.w) && std::isfinite(p.scale) && p.scale > 0 && p.scale <= 1e6F;
}
} // namespace
bool sample_world_pose(const world::World &w, world::EntityId root, const Rig &rig,
                       std::uint32_t clip, double time, std::vector<Pose> &out) {
    const auto player = w.animation(root);
    if (!player || player->rig != rig.id)
        return false;
    std::vector<Pose> sampled;
    if (!sample_pose(rig, clip, time, sampled))
        return false;
    const auto id = w.persistent_id(root);
    if (!id)
        return false;
    const auto bindings = w.physical_rig_bindings(*id);
    if (bindings.empty()) {
        out = std::move(sampled);
        return true;
    }
    const auto root_position = w.spatial_state(root);
    const auto root_transform = w.authored_transform(root).value_or(world::AuthoredTransform{});
    if (!root_position || root_transform.scale.x != 1 || root_transform.scale.y != 1 ||
        root_transform.scale.z != 1)
        return false;
    const auto inverse_root = inverse({{},
                                       {root_transform.rotation.x, root_transform.rotation.y,
                                        root_transform.rotation.z, root_transform.rotation.w},
                                       1});
    std::vector<std::optional<Pose>> overrides(sampled.size());
    for (const auto &r : bindings) {
        const auto &b = r.binding;
        const auto body = w.find(r.body);
        const auto position = body ? w.spatial_state(*body) : std::nullopt;
        if (!body || !w.rigid_box(*body) || !position ||
            position->region != root_position->region || b.node >= sampled.size() ||
            overrides[b.node])
            return false;
        const auto t = w.authored_transform(*body).value_or(world::AuthoredTransform{});
        const double x = position->global_position.x - root_position->global_position.x,
                     y = position->global_position.y - root_position->global_position.y,
                     z = position->global_position.z - root_position->global_position.z;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
            std::max({std::abs(x), std::abs(y), std::abs(z)}) > 1e6)
            return false;
        const Pose relative_body{
            {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
            {t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w},
            1};
        const Pose offset{{b.translation[0], b.translation[1], b.translation[2]},
                          {b.rotation[0], b.rotation[1], b.rotation[2], b.rotation[3]},
                          1};
        auto pose = compose(inverse_root, compose(relative_body, offset));
        pose.scale = sampled[b.node].scale;
        overrides[b.node] = pose;
    }
    std::vector<Pose> result;
    result.reserve(sampled.size());
    for (std::size_t i = 0; i < sampled.size(); ++i) {
        const auto parent = rig.nodes[i].parent;
        const auto local =
            parent < 0 ? sampled[i]
                       : compose(inverse(sampled[static_cast<std::size_t>(parent)]), sampled[i]);
        auto pose = overrides[i].value_or(
            parent < 0 ? local : compose(result[static_cast<std::size_t>(parent)], local));
        if (!finite(pose))
            return false;
        result.push_back(pose);
    }
    out = std::move(result);
    return true;
}
} // namespace vice::presentation
