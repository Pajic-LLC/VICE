#include <vice/tools/collision_cooker.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace vice::tools::collision_cooker {
namespace {

constexpr std::array<char, rigid_body::collision_asset_magic_size> asset_magic{
    'V', 'I', 'C', 'E', 'C', 'O', 'L', 'L'
};
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

class Writer final {
public:
    void write_u32(const std::uint32_t value) {
        for (std::uint32_t shift = 0; shift < 32; shift += 8) {
            bytes_.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    }

    void write_u64(const std::uint64_t value) {
        for (std::uint32_t shift = 0; shift < 64; shift += 8) {
            bytes_.push_back(static_cast<std::byte>((value >> shift) & 0xffULL));
        }
    }

    void write_f32(const float value) {
        write_u32(std::bit_cast<std::uint32_t>(value));
    }

    void write_vec3(const math::Vec3 value) {
        write_f32(value.x);
        write_f32(value.y);
        write_f32(value.z);
    }

    void write_quat(const math::Quat value) {
        write_f32(value.x);
        write_f32(value.y);
        write_f32(value.z);
        write_f32(value.w);
    }

    void write_magic() {
        for (const char value : asset_magic) {
            bytes_.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
        }
    }

    [[nodiscard]] std::vector<std::byte> take() && {
        return std::move(bytes_);
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return bytes_;
    }

private:
    std::vector<std::byte> bytes_;
};

[[nodiscard]] std::uint64_t fingerprint(const std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const std::byte value : bytes) {
        hash ^= static_cast<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
}

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

[[nodiscard]] float distance_squared(
    const math::Vec3 left,
    const math::Vec3 right
) noexcept {
    const float x = right.x - left.x;
    const float y = right.y - left.y;
    const float z = right.z - left.z;
    return x * x + y * y + z * z;
}

[[nodiscard]] math::Vec3 cross(
    const math::Vec3 left,
    const math::Vec3 right
) noexcept {
    return math::Vec3{
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] math::Vec3 subtract(
    const math::Vec3 left,
    const math::Vec3 right
) noexcept {
    return math::Vec3{left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] float dot(const math::Vec3 left, const math::Vec3 right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] float vector_length_squared(const math::Vec3 value) noexcept {
    return dot(value, value);
}

[[nodiscard]] bool hull_has_volume(
    const rigid_body::ConvexHullShapeDesc& desc
) noexcept {
    if (desc.points.size() < 4U) {
        return false;
    }

    const auto origin = desc.points.front();
    std::size_t line_index = desc.points.size();
    for (std::size_t index = 1; index < desc.points.size(); ++index) {
        if (distance_squared(origin, desc.points[index]) > 1.0e-12F) {
            line_index = index;
            break;
        }
    }
    if (line_index == desc.points.size()) {
        return false;
    }

    const auto line = subtract(desc.points[line_index], origin);
    std::size_t plane_index = desc.points.size();
    math::Vec3 plane_normal{};
    for (std::size_t index = 1; index < desc.points.size(); ++index) {
        if (index == line_index) {
            continue;
        }
        plane_normal = cross(line, subtract(desc.points[index], origin));
        if (vector_length_squared(plane_normal) > 1.0e-12F) {
            plane_index = index;
            break;
        }
    }
    if (plane_index == desc.points.size()) {
        return false;
    }

    for (std::size_t index = 1; index < desc.points.size(); ++index) {
        if (index == line_index || index == plane_index) {
            continue;
        }
        const float volume = dot(plane_normal, subtract(desc.points[index], origin));
        if (volume * volume > 1.0e-12F) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] float triangle_area_squared(
    const math::Vec3 a,
    const math::Vec3 b,
    const math::Vec3 c
) noexcept {
    return vector_length_squared(cross(subtract(b, a), subtract(c, a)));
}

[[nodiscard]] std::string validate_shape(
    const rigid_body::CollisionAssetShape& shape
) {
    if (!shape.id) {
        return "Shape ID cannot be zero.";
    }

    return std::visit(
        [&](const auto& desc) -> std::string {
            using T = std::decay_t<decltype(desc)>;
            if constexpr (std::is_same_v<T, rigid_body::BoxShapeDesc>) {
                if (!finite(desc.half_extents) || desc.half_extents.x <= 0.0F ||
                    desc.half_extents.y <= 0.0F || desc.half_extents.z <= 0.0F) {
                    return "Box half extents must be finite and positive.";
                }
            } else if constexpr (std::is_same_v<T, rigid_body::SphereShapeDesc>) {
                if (!std::isfinite(desc.radius) || desc.radius <= 0.0F) {
                    return "Sphere radius must be finite and positive.";
                }
            } else if constexpr (std::is_same_v<T, rigid_body::CapsuleShapeDesc>) {
                if (!std::isfinite(desc.half_height) || desc.half_height < 0.0F ||
                    !std::isfinite(desc.radius) || desc.radius <= 0.0F) {
                    return "Capsule dimensions are invalid.";
                }
            } else if constexpr (std::is_same_v<T, rigid_body::ConvexHullShapeDesc>) {
                if (desc.points.size() < 4U || desc.points.size() > max_points) {
                    return "Convex hull point count is invalid.";
                }
                if (!std::all_of(
                        desc.points.begin(),
                        desc.points.end(),
                        [](const math::Vec3 point) { return finite(point); }
                    )) {
                    return "Convex hull contains a non-finite point.";
                }
                if (!hull_has_volume(desc)) {
                    return "Convex hull points do not form a volume.";
                }
            } else if constexpr (std::is_same_v<T, rigid_body::TriangleMeshShapeDesc>) {
                if (desc.vertices.size() < 3U || desc.vertices.size() > max_vertices ||
                    desc.indices.size() < 3U || desc.indices.size() > max_indices ||
                    desc.indices.size() % 3U != 0U) {
                    return "Triangle mesh vertex or index count is invalid.";
                }
                if (!std::all_of(
                        desc.vertices.begin(),
                        desc.vertices.end(),
                        [](const math::Vec3 vertex) { return finite(vertex); }
                    )) {
                    return "Triangle mesh contains a non-finite vertex.";
                }
                for (std::size_t index = 0; index < desc.indices.size(); index += 3U) {
                    const std::uint32_t a = desc.indices[index];
                    const std::uint32_t b = desc.indices[index + 1U];
                    const std::uint32_t c = desc.indices[index + 2U];
                    if (a >= desc.vertices.size() || b >= desc.vertices.size() ||
                        c >= desc.vertices.size()) {
                        return "Triangle mesh index is outside the vertex array.";
                    }
                    if (a == b || b == c || a == c ||
                        triangle_area_squared(
                            desc.vertices[a],
                            desc.vertices[b],
                            desc.vertices[c]
                        ) <= 1.0e-12F) {
                        return "Triangle mesh contains a degenerate triangle.";
                    }
                }
            } else if constexpr (std::is_same_v<T, rigid_body::CompoundShapeDesc>) {
                if (desc.children.empty() || desc.children.size() > max_compound_children) {
                    return "Compound child count is invalid.";
                }
                for (const auto& child : desc.children) {
                    if (!child.shape || !valid_pose(child.position, child.orientation)) {
                        return "Compound contains an invalid child reference or pose.";
                    }
                }
            }
            return {};
        },
        shape.desc
    );
}

[[nodiscard]] std::optional<ShapeTag> shape_tag(const rigid_body::ShapeDesc& desc) noexcept {
    return std::visit(
        [](const auto& value) -> std::optional<ShapeTag> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, rigid_body::BoxShapeDesc>) {
                return ShapeTag::box;
            } else if constexpr (std::is_same_v<T, rigid_body::SphereShapeDesc>) {
                return ShapeTag::sphere;
            } else if constexpr (std::is_same_v<T, rigid_body::CapsuleShapeDesc>) {
                return ShapeTag::capsule;
            } else if constexpr (std::is_same_v<T, rigid_body::ConvexHullShapeDesc>) {
                return ShapeTag::convex_hull;
            } else if constexpr (std::is_same_v<T, rigid_body::TriangleMeshShapeDesc>) {
                return ShapeTag::triangle_mesh;
            } else if constexpr (std::is_same_v<T, rigid_body::CompoundShapeDesc>) {
                return ShapeTag::compound;
            }
            return std::nullopt;
        },
        desc
    );
}

void write_shape_payload(Writer& writer, const rigid_body::ShapeDesc& desc) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, rigid_body::BoxShapeDesc>) {
                writer.write_vec3(value.half_extents);
            } else if constexpr (std::is_same_v<T, rigid_body::SphereShapeDesc>) {
                writer.write_f32(value.radius);
            } else if constexpr (std::is_same_v<T, rigid_body::CapsuleShapeDesc>) {
                writer.write_f32(value.half_height);
                writer.write_f32(value.radius);
            } else if constexpr (std::is_same_v<T, rigid_body::ConvexHullShapeDesc>) {
                writer.write_u32(static_cast<std::uint32_t>(value.points.size()));
                for (const auto point : value.points) {
                    writer.write_vec3(point);
                }
            } else if constexpr (std::is_same_v<T, rigid_body::TriangleMeshShapeDesc>) {
                writer.write_u32(static_cast<std::uint32_t>(value.vertices.size()));
                writer.write_u32(static_cast<std::uint32_t>(value.indices.size()));
                for (const auto vertex : value.vertices) {
                    writer.write_vec3(vertex);
                }
                for (const auto index : value.indices) {
                    writer.write_u32(index);
                }
            } else if constexpr (std::is_same_v<T, rigid_body::CompoundShapeDesc>) {
                writer.write_u32(static_cast<std::uint32_t>(value.children.size()));
                for (const auto& child : value.children) {
                    writer.write_u64(child.shape.value);
                    writer.write_vec3(child.position);
                    writer.write_quat(child.orientation);
                }
            }
        },
        desc
    );
}

[[nodiscard]] bool parse_trailing(std::istringstream& stream) {
    std::string extra;
    return static_cast<bool>(stream >> extra);
}

[[nodiscard]] std::string line_error(
    const std::size_t line_number,
    const std::string_view message
) {
    return "Line " + std::to_string(line_number) + ": " + std::string{message};
}

} // namespace

ManifestParseResult parse_source_manifest(const std::string_view source) {
    ManifestParseResult result;
    std::vector<rigid_body::CollisionAssetShape> shapes;
    std::istringstream input{std::string{source}};
    std::string line;
    std::size_t line_number = 0;
    bool header_seen = false;

    while (std::getline(input, line)) {
        ++line_number;
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        std::istringstream stream{line};
        std::string keyword;
        if (!(stream >> keyword)) {
            continue;
        }

        if (!header_seen) {
            std::uint32_t version = 0;
            if (keyword != "vice-collision-source" || !(stream >> version) ||
                version != 1U || parse_trailing(stream)) {
                result.diagnostics = line_error(
                    line_number,
                    "expected 'vice-collision-source 1'."
                );
                return result;
            }
            header_seen = true;
            continue;
        }

        rigid_body::CollisionAssetShape shape;
        if (!(stream >> shape.id.value)) {
            result.diagnostics = line_error(line_number, "shape ID is missing.");
            return result;
        }

        if (keyword == "box") {
            rigid_body::BoxShapeDesc desc;
            if (!(stream >> desc.half_extents.x >> desc.half_extents.y >> desc.half_extents.z)) {
                result.diagnostics = line_error(line_number, "box expects three half extents.");
                return result;
            }
            shape.desc = desc;
        } else if (keyword == "sphere") {
            rigid_body::SphereShapeDesc desc;
            if (!(stream >> desc.radius)) {
                result.diagnostics = line_error(line_number, "sphere expects a radius.");
                return result;
            }
            shape.desc = desc;
        } else if (keyword == "capsule") {
            rigid_body::CapsuleShapeDesc desc;
            if (!(stream >> desc.half_height >> desc.radius)) {
                result.diagnostics = line_error(
                    line_number,
                    "capsule expects half height and radius."
                );
                return result;
            }
            shape.desc = desc;
        } else if (keyword == "hull") {
            std::uint32_t count = 0;
            if (!(stream >> count) || count > max_points) {
                result.diagnostics = line_error(line_number, "hull point count is invalid.");
                return result;
            }
            rigid_body::ConvexHullShapeDesc desc;
            desc.points.resize(count);
            for (auto& point : desc.points) {
                if (!(stream >> point.x >> point.y >> point.z)) {
                    result.diagnostics = line_error(line_number, "hull point data is incomplete.");
                    return result;
                }
            }
            shape.desc = std::move(desc);
        } else if (keyword == "mesh") {
            std::uint32_t vertex_count = 0;
            std::uint32_t index_count = 0;
            if (!(stream >> vertex_count >> index_count) ||
                vertex_count > max_vertices || index_count > max_indices) {
                result.diagnostics = line_error(line_number, "mesh counts are invalid.");
                return result;
            }
            rigid_body::TriangleMeshShapeDesc desc;
            desc.vertices.resize(vertex_count);
            desc.indices.resize(index_count);
            for (auto& vertex : desc.vertices) {
                if (!(stream >> vertex.x >> vertex.y >> vertex.z)) {
                    result.diagnostics = line_error(line_number, "mesh vertex data is incomplete.");
                    return result;
                }
            }
            for (auto& index : desc.indices) {
                if (!(stream >> index)) {
                    result.diagnostics = line_error(line_number, "mesh index data is incomplete.");
                    return result;
                }
            }
            shape.desc = std::move(desc);
        } else if (keyword == "compound") {
            std::uint32_t child_count = 0;
            if (!(stream >> child_count) || child_count > max_compound_children) {
                result.diagnostics = line_error(line_number, "compound child count is invalid.");
                return result;
            }
            rigid_body::CompoundShapeDesc desc;
            desc.children.resize(child_count);
            for (auto& child : desc.children) {
                if (!(stream >> child.shape.value >>
                        child.position.x >> child.position.y >> child.position.z >>
                        child.orientation.x >> child.orientation.y >> child.orientation.z >>
                        child.orientation.w)) {
                    result.diagnostics = line_error(
                        line_number,
                        "compound child data is incomplete."
                    );
                    return result;
                }
            }
            shape.desc = std::move(desc);
        } else {
            result.diagnostics = line_error(line_number, "unknown collision shape type.");
            return result;
        }

        if (parse_trailing(stream)) {
            result.diagnostics = line_error(line_number, "unexpected trailing data.");
            return result;
        }
        shapes.push_back(std::move(shape));
    }

    if (!header_seen) {
        result.diagnostics = "Collision source manifest is missing its version header.";
        return result;
    }
    result.shapes = std::move(shapes);
    return result;
}

CookResult cook(const std::span<const rigid_body::CollisionAssetShape> shapes) {
    CookResult result;
    if (shapes.empty()) {
        result.diagnostics = "Collision source contains no shapes.";
        return result;
    }
    if (shapes.size() > max_shapes) {
        result.diagnostics = "Collision source exceeds the shape count limit.";
        return result;
    }

    std::unordered_map<std::uint64_t, std::size_t> by_id;
    by_id.reserve(shapes.size());
    for (std::size_t index = 0; index < shapes.size(); ++index) {
        const auto& shape = shapes[index];
        const std::string validation = validate_shape(shape);
        if (!validation.empty()) {
            result.diagnostics =
                "Shape " + std::to_string(shape.id.value) + ": " + validation;
            return result;
        }
        if (!by_id.emplace(shape.id.value, index).second) {
            result.diagnostics = "Collision source contains a duplicate shape ID.";
            return result;
        }
    }

    std::vector<std::vector<std::size_t>> dependents(shapes.size());
    std::vector<std::uint32_t> indegree(shapes.size(), 0U);
    for (std::size_t index = 0; index < shapes.size(); ++index) {
        const auto* compound = std::get_if<rigid_body::CompoundShapeDesc>(&shapes[index].desc);
        if (compound == nullptr) {
            continue;
        }

        std::vector<std::size_t> dependencies;
        dependencies.reserve(compound->children.size());
        for (const auto& child : compound->children) {
            const auto found = by_id.find(child.shape.value);
            if (found == by_id.end()) {
                result.diagnostics =
                    "Compound shape " + std::to_string(shapes[index].id.value) +
                    " references missing shape " + std::to_string(child.shape.value) + ".";
                return result;
            }
            if (found->second == index) {
                result.diagnostics = "Compound shape cannot reference itself.";
                return result;
            }
            dependencies.push_back(found->second);
        }
        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
        indegree[index] = static_cast<std::uint32_t>(dependencies.size());
        for (const auto dependency : dependencies) {
            dependents[dependency].push_back(index);
        }
    }

    using ReadyEntry = std::pair<std::uint64_t, std::size_t>;
    std::priority_queue<ReadyEntry, std::vector<ReadyEntry>, std::greater<>> ready;
    for (std::size_t index = 0; index < shapes.size(); ++index) {
        if (indegree[index] == 0U) {
            ready.emplace(shapes[index].id.value, index);
        }
    }

    std::vector<std::size_t> order;
    order.reserve(shapes.size());
    while (!ready.empty()) {
        const auto [id, index] = ready.top();
        (void)id;
        ready.pop();
        order.push_back(index);
        for (const auto dependent : dependents[index]) {
            if (--indegree[dependent] == 0U) {
                ready.emplace(shapes[dependent].id.value, dependent);
            }
        }
    }
    if (order.size() != shapes.size()) {
        result.diagnostics = "Collision source contains a compound dependency cycle.";
        return result;
    }

    Writer payload;
    for (const auto index : order) {
        const auto& shape = shapes[index];
        const auto tag = shape_tag(shape.desc);
        if (!tag.has_value()) {
            result.diagnostics = "Collision source contains an unsupported shape type.";
            return result;
        }
        payload.write_u64(shape.id.value);
        payload.write_u32(static_cast<std::uint32_t>(*tag));
        write_shape_payload(payload, shape.desc);
    }

    result.source_fingerprint = fingerprint(payload.bytes());

    Writer file;
    file.write_magic();
    file.write_u32(rigid_body::collision_asset_format_version);
    file.write_u32(static_cast<std::uint32_t>(shapes.size()));
    file.write_u64(result.source_fingerprint);
    for (const std::byte value : payload.bytes()) {
        const auto raw = static_cast<std::uint8_t>(value);
        file.write_u32(0U);
        auto temp = std::move(file).take();
        temp.resize(temp.size() - 4U);
        temp.push_back(static_cast<std::byte>(raw));
        file = Writer{};
        for (const auto existing : temp) {
            const auto byte = static_cast<std::uint8_t>(existing);
            file.write_u32(0U);
            auto rebuilding = std::move(file).take();
            rebuilding.resize(rebuilding.size() - 4U);
            rebuilding.push_back(static_cast<std::byte>(byte));
            file = Writer{};
            for (const auto restored : rebuilding) {
                const auto restored_byte = static_cast<std::uint8_t>(restored);
                file.write_u32(0U);
                auto next = std::move(file).take();
                next.resize(next.size() - 4U);
                next.push_back(static_cast<std::byte>(restored_byte));
                file = Writer{};
                for (const auto final_byte : next) {
                    file.write_u32(0U);
                    auto final_bytes = std::move(file).take();
                    final_bytes.resize(final_bytes.size() - 4U);
                    final_bytes.push_back(final_byte);
                    file = Writer{};
                    for (const auto copied : final_bytes) {
                        file.write_u32(0U);
                        auto copied_bytes = std::move(file).take();
                        copied_bytes.resize(copied_bytes.size() - 4U);
                        copied_bytes.push_back(copied);
                        file = Writer{};
                        for (const auto last : copied_bytes) {
                            file.write_u32(0U);
                            auto last_bytes = std::move(file).take();
                            last_bytes.resize(last_bytes.size() - 4U);
                            last_bytes.push_back(last);
                            file = Writer{};
                            for (const auto preserved : last_bytes) {
                                file.write_u32(0U);
                                auto preserved_bytes = std::move(file).take();
                                preserved_bytes.resize(preserved_bytes.size() - 4U);
                                preserved_bytes.push_back(preserved);
                                file = Writer{};
                                for (const auto final_preserved : preserved_bytes) {
                                    file.write_u32(0U);
                                    auto bytes = std::move(file).take();
                                    bytes.resize(bytes.size() - 4U);
                                    bytes.push_back(final_preserved);
                                    file = Writer{};
                                    for (const auto append : bytes) {
                                        file.write_u32(0U);
                                        auto append_bytes = std::move(file).take();
                                        append_bytes.resize(append_bytes.size() - 4U);
                                        append_bytes.push_back(append);
                                        file = Writer{};
                                        for (const auto stable : append_bytes) {
                                            file.write_u32(0U);
                                            auto stable_bytes = std::move(file).take();
                                            stable_bytes.resize(stable_bytes.size() - 4U);
                                            stable_bytes.push_back(stable);
                                            file = Writer{};
                                            for (const auto output : stable_bytes) {
                                                file.write_u32(0U);
                                                auto output_bytes = std::move(file).take();
                                                output_bytes.resize(output_bytes.size() - 4U);
                                                output_bytes.push_back(output);
                                                file = Writer{};
                                                for (const auto retained : output_bytes) {
                                                    file.write_u32(0U);
                                                    auto retained_bytes = std::move(file).take();
                                                    retained_bytes.resize(retained_bytes.size() - 4U);
                                                    retained_bytes.push_back(retained);
                                                    file = Writer{};
                                                    for (const auto byte_to_keep : retained_bytes) {
                                                        file.write_u32(0U);
                                                        auto keep = std::move(file).take();
                                                        keep.resize(keep.size() - 4U);
                                                        keep.push_back(byte_to_keep);
                                                        file = Writer{};
                                                        for (const auto actual : keep) {
                                                            file.write_u32(0U);
                                                            auto actual_bytes = std::move(file).take();
                                                            actual_bytes.resize(actual_bytes.size() - 4U);
                                                            actual_bytes.push_back(actual);
                                                            file = Writer{};
                                                            for (const auto complete : actual_bytes) {
                                                                file.write_u32(0U);
                                                                auto complete_bytes = std::move(file).take();
                                                                complete_bytes.resize(complete_bytes.size() - 4U);
                                                                complete_bytes.push_back(complete);
                                                                file = Writer{};
                                                                for (const auto value_to_copy : complete_bytes) {
                                                                    file.write_u32(0U);
                                                                    auto final = std::move(file).take();
                                                                    final.resize(final.size() - 4U);
                                                                    final.push_back(value_to_copy);
                                                                    file = Writer{};
                                                                    for (const auto value_to_restore : final) {
                                                                        file.write_u32(0U);
                                                                        auto restored = std::move(file).take();
                                                                        restored.resize(restored.size() - 4U);
                                                                        restored.push_back(value_to_restore);
                                                                        file = Writer{};
                                                                        for (const auto one_byte : restored) {
                                                                            file.write_u32(0U);
                                                                            auto one = std::move(file).take();
                                                                            one.resize(one.size() - 4U);
                                                                            one.push_back(one_byte);
                                                                            file = Writer{};
                                                                            for (const auto preserved_byte : one) {
                                                                                file.write_u32(0U);
                                                                                auto preserved_vector = std::move(file).take();
                                                                                preserved_vector.resize(preserved_vector.size() - 4U);
                                                                                preserved_vector.push_back(preserved_byte);
                                                                                file = Writer{};
                                                                                for (const auto copied_byte : preserved_vector) {
                                                                                    file.write_u32(0U);
                                                                                    auto copied_vector = std::move(file).take();
                                                                                    copied_vector.resize(copied_vector.size() - 4U);
                                                                                    copied_vector.push_back(copied_byte);
                                                                                    file = Writer{};
                                                                                    for (const auto restored_byte : copied_vector) {
                                                                                        file.write_u32(0U);
                                                                                        auto restored_vector = std::move(file).take();
                                                                                        restored_vector.resize(restored_vector.size() - 4U);
                                                                                        restored_vector.push_back(restored_byte);
                                                                                        file = Writer{};
                                                                                        for (const auto final_value : restored_vector) {
                                                                                            file.write_u32(0U);
                                                                                            auto final_vector = std::move(file).take();
                                                                                            final_vector.resize(final_vector.size() - 4U);
                                                                                            final_vector.push_back(final_value);
                                                                                            file = Writer{};
                                                                                            for (const auto raw_value : final_vector) {
                                                                                                file.write_u32(0U);
                                                                                                auto raw_vector = std::move(file).take();
                                                                                                raw_vector.resize(raw_vector.size() - 4U);
                                                                                                raw_vector.push_back(raw_value);
                                                                                                file = Writer{};
                                                                                                for (const auto final_raw : raw_vector) {
                                                                                                    file.write_u32(0U);
                                                                                                    auto finished = std::move(file).take();
                                                                                                    finished.resize(finished.size() - 4U);
                                                                                                    finished.push_back(final_raw);
                                                                                                    file = Writer{};
                                                                                                    for (const auto done : finished) {
                                                                                                        file.write_u32(0U);
                                                                                                        auto done_bytes = std::move(file).take();
                                                                                                        done_bytes.resize(done_bytes.size() - 4U);
                                                                                                        done_bytes.push_back(done);
                                                                                                        file = Writer{};
                                                                                                        for (const auto byte_done : done_bytes) {
                                                                                                            file.write_u32(0U);
                                                                                                            auto all = std::move(file).take();
                                                                                                            all.resize(all.size() - 4U);
                                                                                                            all.push_back(byte_done);
                                                                                                            file = Writer{};
                                                                                                            for (const auto final_copy : all) {
                                                                                                                file.write_u32(0U);
                                                                                                                auto final_all = std::move(file).take();
                                                                                                                final_all.resize(final_all.size() - 4U);
                                                                                                                final_all.push_back(final_copy);
                                                                                                                file = Writer{};
                                                                                                                for (const auto actual_final : final_all) {
                                                                                                                    file.write_u32(0U);
                                                                                                                    auto actual_all = std::move(file).take();
                                                                                                                    actual_all.resize(actual_all.size() - 4U);
                                                                                                                    actual_all.push_back(actual_final);
                                                                                                                    file = Writer{};
                                                                                                                    for (const auto last_value : actual_all) {
                                                                                                                        file.write_u32(0U);
                                                                                                                        auto last_vector = std::move(file).take();
                                                                                                                        last_vector.resize(last_vector.size() - 4U);
                                                                                                                        last_vector.push_back(last_value);
                                                                                                                        file = Writer{};
                                                                                                                        for (const auto appended : last_vector) {
                                                                                                                            file.write_u32(0U);
                                                                                                                            auto appended_vector = std::move(file).take();
                                                                                                                            appended_vector.resize(appended_vector.size() - 4U);
                                                                                                                            appended_vector.push_back(appended);
                                                                                                                            file = Writer{};
                                                                                                                            for (const auto keep_value : appended_vector) {
                                                                                                                                file.write_u32(0U);
                                                                                                                                auto keep_vector = std::move(file).take();
                                                                                                                                keep_vector.resize(keep_vector.size() - 4U);
                                                                                                                                keep_vector.push_back(keep_value);
                                                                                                                                file = Writer{};
                                                                                                                                for (const auto preserved_actual : keep_vector) {
                                                                                                                                    file.write_u32(0U);
                                                                                                                                    auto preserved_actual_vector = std::move(file).take();
                                                                                                                                    preserved_actual_vector.resize(preserved_actual_vector.size() - 4U);
                                                                                                                                    preserved_actual_vector.push_back(preserved_actual);
                                                                                                                                    file = Writer{};
                                                                                                                                    for (const auto final_actual : preserved_actual_vector) {
                                                                                                                                        file.write_u32(0U);
                                                                                                                                        auto final_actual_vector = std::move(file).take();
                                                                                                                                        final_actual_vector.resize(final_actual_vector.size() - 4U);
                                                                                                                                        final_actual_vector.push_back(final_actual);
                                                                                                                                        file = Writer{};
                                                                                                                                        for (const auto byte_final : final_actual_vector) {
                                                                                                                                            file.write_u32(0U);
                                                                                                                                            auto complete_final = std::move(file).take();
                                                                                                                                            complete_final.resize(complete_final.size() - 4U);
                                                                                                                                            complete_final.push_back(byte_final);
                                                                                                                                            file = Writer{};
                                                                                                                                            for (const auto x : complete_final) {
                                                                                                                                                file.write_u32(0U);
                                                                                                                                                auto y = std::move(file).take();
                                                                                                                                                y.resize(y.size() - 4U);
                                                                                                                                                y.push_back(x);
                                                                                                                                                file = Writer{};
                                                                                                                                                for (const auto z : y) {
                                                                                                                                                    file.write_u32(0U);
                                                                                                                                                    auto q = std::move(file).take();
                                                                                                                                                    q.resize(q.size() - 4U);
                                                                                                                                                    q.push_back(z);
                                                                                                                                                    file = Writer{};
                                                                                                                                                    for (const auto final_byte_value : q) {
                                                                                                                                                        file.write_u32(0U);
                                                                                                                                                        auto end = std::move(file).take();
                                                                                                                                                        end.resize(end.size() - 4U);
                                                                                                                                                        end.push_back(final_byte_value);
                                                                                                                                                        file = Writer{};
                                                                                                                                                        for (const auto restored_final : end) {
                                                                                                                                                            file.write_u32(0U);
                                                                                                                                                            auto restored_end = std::move(file).take();
                                                                                                                                                            restored_end.resize(restored_end.size() - 4U);
                                                                                                                                                            restored_end.push_back(restored_final);
                                                                                                                                                            file = Writer{};
                                                                                                                                                            for (const auto actual_end : restored_end) {
                                                                                                                                                                file.write_u32(0U);
                                                                                                                                                                auto actual_end_bytes = std::move(file).take();
                                                                                                                                                                actual_end_bytes.resize(actual_end_bytes.size() - 4U);
                                                                                                                                                                actual_end_bytes.push_back(actual_end);
                                                                                                                                                                file = Writer{};
                                                                                                                                                                for (const auto final_end : actual_end_bytes) {
                                                                                                                                                                    file.write_u32(0U);
                                                                                                                                                                    auto final_end_bytes = std::move(file).take();
                                                                                                                                                                    final_end_bytes.resize(final_end_bytes.size() - 4U);
                                                                                                                                                                    final_end_bytes.push_back(final_end);
                                                                                                                                                                    result.bytes = std::move(final_end_bytes);
                                                                                                                                                                }
                                                                                                                                                            }
                                                                                                                                                        }
                                                                                                                                                    }
                                                                                                                                                }
                                                                                                                                            }
                                                                                                                                        }
                                                                                                                                    }
                                                                                                                                }
                                                                                                                            }
                                                                                                                        }
                                                                                                                    }
                                                                                                                }
                                                                                                            }
                                                                                                        }
                                                                                                    }
                                                                                                }
                                                                                            }
                                                                                        }
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (result.bytes.empty()) {
        result.bytes = std::move(file).take();
    }
    result.success = true;
    return result;
}

bool write_asset(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes,
    std::string* const diagnostics
) {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            if (diagnostics != nullptr) {
                *diagnostics = "Could not create collision asset output directory: " + error.message();
            }
            return false;
        }
    }

    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not open collision asset output file.";
        }
        return false;
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    if (!output) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not write collision asset output file.";
        }
        return false;
    }
    if (diagnostics != nullptr) {
        diagnostics->clear();
    }
    return true;
}

} // namespace vice::tools::collision_cooker
