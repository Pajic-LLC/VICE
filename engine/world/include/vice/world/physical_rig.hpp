#pragma once
#include <array>
#include <cmath>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <vice/world/persistent_id.hpp>
namespace vice::world {
// A body-owned binding. Offset is a rigid pose in COM-local metres; collider scale is ignored.
struct PhysicalRigBinding {
    PersistentId root{};
    std::uint32_t node = 0;
    std::array<float, 3> translation{};
    std::array<float, 4> rotation{0, 0, 0, 1};
    friend bool operator==(const PhysicalRigBinding &, const PhysicalRigBinding &) = default;
};
[[nodiscard]] std::string serialize_physical_rig(const std::optional<PhysicalRigBinding> &);
struct OwnedPhysicalRigBinding {
    PersistentId body{};
    PhysicalRigBinding binding;
};
inline bool valid(const PhysicalRigBinding &b) noexcept {
    if (b.root.is_nil() || b.node >= 128)
        return false;
    for (auto v : b.translation)
        if (!std::isfinite(v) || std::abs(v) > 10000)
            return false;
    double n = 0;
    for (auto v : b.rotation) {
        if (!std::isfinite(v))
            return false;
        n += double(v) * v;
    }
    return std::abs(n - 1) < 1e-4;
}
inline void write_physical_rig(std::ostream &o, const PhysicalRigBinding &b) {
    o << 1 << ' ' << b.root.to_string() << ' ' << b.node;
    for (auto v : b.translation)
        o << ' ' << v;
    for (auto v : b.rotation)
        o << ' ' << v;
}
inline bool read_physical_rig(std::istream &in, PhysicalRigBinding &out) {
    PhysicalRigBinding b;
    unsigned version = 0;
    std::string root;
    if (!(in >> version >> root >> b.node) || version != 1)
        return false;
    auto id = PersistentId::parse(root);
    if (!id)
        return false;
    b.root = *id;
    for (auto &v : b.translation)
        if (!(in >> v))
            return false;
    for (auto &v : b.rotation)
        if (!(in >> v))
            return false;
    if (!valid(b))
        return false;
    out = b;
    return true;
}
} // namespace vice::world
