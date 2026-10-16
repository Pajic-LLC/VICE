#include <vice/rigid_body/collision_asset.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vice::rigid_body {
namespace {

constexpr std::array<char, collision_asset_magic_size> asset_magic{
    'V', 'I', 'C', 'E', 'C', 'O', 'L', 'L'
};
constexpr std::size_t header_size = 24;
constexpr std::uint32_t max_shapes = 65'536;
constexpr std::uint32_t max_points = 1'000'000;
constexpr std::uint32_t max_vertices = 1'000'000;
constexpr std::uint32_t max_indices = 3'000'000;
constexpr std::uint32_t max_compound_children = 65'536;
constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

enum class ShapeTag : std::uint32_t {
    box = 1,
    sphere = 2,
    capsule = 3,
    convex_hull = 4,
    triangle_mesh = 5,
    compound = 6,
};

[[nodiscard]] std::uint64_t fingerprint(std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const std::byte value : bytes) {
        hash ^= static_cast<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
}

class Reader final {
public:
    explicit Reader(const std::span<const std::byte> bytes) noexcept : bytes_{bytes} {}

    [[nodiscard]] bool read_u32(std::uint32_t& value) noexcept {
        if (!available(4)) {
            return false;
        }
        value = static_cast<std::uint32_t>(bytes_[position_]) |
            (static_cast<std::uint32_t>(bytes_[position_ + 1]) << 8U) |
            (static_cast<std::uint32_t>(bytes_[position_ + 2]) << 16U) |
            (static_cast<std::uint32_t>(bytes_[position_ + 3]) << 24U);
        position_ += 4;
        return true;
    }

    [[nodiscard]] bool read_u64(std::uint64_t& value) noexcept {
        if (!available(8)) {
            return false;
        }
        value = 0;
        for (std::uint32_t shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(
                static_cast<std::uint8_t>(bytes_[position_++])
            ) << shift;
        }
        return true;
    }

    [[nodiscard]] bool read_f32(float& value) noexcept {
        std::uint32_t bits = 0;
        if (!read_u32(bits)) {
            return false;
        }
        value = std::bit_cast<float>(bits);
        return true;
    }

    [[nodiscard]] bool read_vec3(math::Vec3& value) noexcept {
        return read_f32(value.x) && read_f32(value.y) && read_f32(value.z);
    }

    [[nodiscard]] bool read_quat(math::Quat& value) noexcept {
        return read_f32(value.x) && read_f32(value.y) &&
            read_f32(value.z) && read_f32(value.w);
    }

    [[nodiscard]] bool finished() const noexcept {
        return position_ == bytes_.size();
    }

private:
    [[nodiscard]] bool available(const std::size_t count) const noexcept {
        return count <= bytes_.size() - position_;
    }

    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

[[nodiscard]] bool finite(const math::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finite(const math::Quat value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] bool valid_pose(
    const math::Vec3 position,
    const math::Quat orientation
) noexcept {
    const float length_squared = orientation.x * orientation.x +
        orientation.y * orientation.y + orientation.z * orientation.z +
        orientation.w * orientation.w;
    return finite(position) && finite(orientation) && length_squared > 0.000001F;
}

[[nodiscard]] bool read_shape(
    Reader& reader,
    const ShapeTag tag,
    ShapeDesc& output,
    std::string& diagnostics
) {
    switch (tag) {
    case ShapeTag::box: {
        BoxShapeDesc desc;
        if (!reader.read_vec3(desc.half_extents)) {
            diagnostics = "Collision asset ended inside a box shape.";
            return false;
        }
        output = desc;
        return true;
    }
    case ShapeTag::sphere: {
        SphereShapeDesc desc;
        if (!reader.read_f32(desc.radius)) {
            diagnostics = "Collision asset ended inside a sphere shape.";
            return false;
        }
        output = desc;
        return true;
    }
    case ShapeTag::capsule: {
        CapsuleShapeDesc desc;
        if (!reader.read_f32(desc.half_height) || !reader.read_f32(desc.radius)) {
            diagnostics = "Collision asset ended inside a capsule shape.";
            return false;
        }
        output = desc;
        return true;
    }
    case ShapeTag::convex_hull: {
        std::uint32_t count = 0;
        if (!reader.read_u32(count) || count > max_points) {
            diagnostics = "Collision asset has an invalid convex hull point count.";
            return false;
        }
        ConvexHullShapeDesc desc;
        desc.points.resize(count);
        for (auto& point : desc.points) {
            if (!reader.read_vec3(point)) {
                diagnostics = "Collision asset ended inside convex hull points.";
                return false;
            }
        }
        output = std::move(desc);
        return true;
    }
    case ShapeTag::triangle_mesh: {
        std::uint32_t vertex_count = 0;
        std::uint32_t index_count = 0;
        if (!reader.read_u32(vertex_count) || !reader.read_u32(index_count) ||
            vertex_count > max_vertices || index_count > max_indices) {
            diagnostics = "Collision asset has invalid triangle mesh counts.";
            return false;
        }
        TriangleMeshShapeDesc desc;
        desc.vertices.resize(vertex_count);
        desc.indices.resize(index_count);
        for (auto& vertex : desc.vertices) {
            if (!reader.read_vec3(vertex)) {
                diagnostics = "Collision asset ended inside mesh vertices.";
                return false;
            }
        }
        for (auto& index : desc.indices) {
            if (!reader.read_u32(index)) {
                diagnostics = "Collision asset ended inside mesh indices.";
                return false;
            }
        }
        output = std::move(desc);
        return true;
    }
    case ShapeTag::compound: {
        std::uint32_t child_count = 0;
        if (!reader.read_u32(child_count) || child_count > max_compound_children) {
            diagnostics = "Collision asset has an invalid compound child count.";
            return false;
        }
        CompoundShapeDesc desc;
        desc.children.resize(child_count);
        for (auto& child : desc.children) {
            if (!reader.read_u64(child.shape.value) ||
                !reader.read_vec3(child.position) ||
                !reader.read_quat(child.orientation)) {
                diagnostics = "Collision asset ended inside compound children.";
                return false;
            }
            if (!child.shape || !valid_pose(child.position, child.orientation)) {
                diagnostics = "Collision asset contains an invalid compound child.";
                return false;
            }
        }
        output = std::move(desc);
        return true;
    }
    }

    diagnostics = "Collision asset contains an unknown shape tag.";
    return false;
}

} // namespace

CollisionAssetDecodeResult decode_collision_asset(
    const std::span<const std::byte> bytes
) {
    CollisionAssetDecodeResult result;
    if (bytes.size() < header_size) {
        result.diagnostics = "Collision asset is smaller than the version 1 header.";
        return result;
    }

    for (std::size_t index = 0; index < asset_magic.size(); ++index) {
        if (bytes[index] != static_cast<std::byte>(asset_magic[index])) {
            result.diagnostics = "Collision asset magic does not match VICECOLL.";
            return result;
        }
    }

    Reader reader{bytes.subspan(asset_magic.size())};
    std::uint32_t version = 0;
    std::uint32_t shape_count = 0;
    std::uint64_t stored_fingerprint = 0;
    if (!reader.read_u32(version) || !reader.read_u32(shape_count) ||
        !reader.read_u64(stored_fingerprint)) {
        result.diagnostics = "Collision asset header is incomplete.";
        return result;
    }
    if (version != collision_asset_format_version) {
        result.diagnostics = "Collision asset format version is not supported.";
        return result;
    }
    if (shape_count > max_shapes) {
        result.diagnostics = "Collision asset shape count exceeds the runtime limit.";
        return result;
    }

    const auto payload = bytes.subspan(header_size);
    if (fingerprint(payload) != stored_fingerprint) {
        result.diagnostics = "Collision asset fingerprint does not match its payload.";
        return result;
    }

    CollisionAsset asset;
    asset.version = version;
    asset.source_fingerprint = stored_fingerprint;
    asset.shapes.reserve(shape_count);

    std::unordered_set<std::uint64_t> registered_ids;
    registered_ids.reserve(shape_count);
    for (std::uint32_t shape_index = 0; shape_index < shape_count; ++shape_index) {
        CollisionAssetShape shape;
        std::uint32_t raw_tag = 0;
        if (!reader.read_u64(shape.id.value) || !reader.read_u32(raw_tag)) {
            result.diagnostics = "Collision asset ended before a complete shape record.";
            return result;
        }
        if (!shape.id || !registered_ids.insert(shape.id.value).second) {
            result.diagnostics = "Collision asset contains a zero or duplicate shape ID.";
            return result;
        }
        if (raw_tag < static_cast<std::uint32_t>(ShapeTag::box) ||
            raw_tag > static_cast<std::uint32_t>(ShapeTag::compound)) {
            result.diagnostics = "Collision asset contains an unknown shape tag.";
            return result;
        }
        if (!read_shape(reader, static_cast<ShapeTag>(raw_tag), shape.desc, result.diagnostics)) {
            return result;
        }
        if (const auto* compound = std::get_if<CompoundShapeDesc>(&shape.desc)) {
            for (const auto& child : compound->children) {
                if (!registered_ids.contains(child.shape.value)) {
                    result.diagnostics =
                        "Collision asset compound references a shape that does not precede it.";
                    return result;
                }
            }
        }
        asset.shapes.push_back(std::move(shape));
    }

    if (!reader.finished()) {
        result.diagnostics = "Collision asset contains trailing bytes after its shape records.";
        return result;
    }

    result.asset = std::move(asset);
    return result;
}

bool register_collision_asset(
    Domain& domain,
    const CollisionAsset& asset,
    std::string* const diagnostics
) {
    const auto fail = [&](const std::string& message) {
        if (diagnostics != nullptr) {
            *diagnostics = message;
        }
        return false;
    };

    if (asset.version != collision_asset_format_version) {
        return fail("Collision asset version is not supported by this runtime.");
    }

    std::vector<ShapeId> registered;
    registered.reserve(asset.shapes.size());
    for (const auto& shape : asset.shapes) {
        if (!domain.register_shape(shape.id, shape.desc)) {
            for (auto it = registered.rbegin(); it != registered.rend(); ++it) {
                (void)domain.unregister_shape(*it);
            }
            return fail(
                "Collision asset could not register shape " +
                std::to_string(shape.id.value) + "."
            );
        }
        registered.push_back(shape.id);
    }

    if (diagnostics != nullptr) {
        diagnostics->clear();
    }
    return true;
}

} // namespace vice::rigid_body
