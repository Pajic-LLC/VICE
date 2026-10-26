#include <vice/rigid_body/rigid_body.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/EstimateCollisionResponse.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CompoundShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeIDPair.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace vice::rigid_body {
namespace {

class JoltRuntime final {
public:
    JoltRuntime() {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }

    ~JoltRuntime() {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }

    JoltRuntime(const JoltRuntime&) = delete;
    JoltRuntime& operator=(const JoltRuntime&) = delete;
};

[[nodiscard]] JoltRuntime& runtime() {
    static JoltRuntime instance;
    return instance;
}

namespace object_layers {
constexpr JPH::ObjectLayer static_body = 0;
constexpr JPH::ObjectLayer dynamic_body = 1;
constexpr JPH::ObjectLayer count = 2;
} // namespace object_layers

namespace broad_phase_layers {
const JPH::BroadPhaseLayer static_body{0};
const JPH::BroadPhaseLayer dynamic_body{1};
constexpr JPH::uint count = 2;
} // namespace broad_phase_layers

class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    BroadPhaseLayerInterface() {
        object_to_broad_phase_[object_layers::static_body] = broad_phase_layers::static_body;
        object_to_broad_phase_[object_layers::dynamic_body] = broad_phase_layers::dynamic_body;
    }

    [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override {
        return broad_phase_layers::count;
    }

    [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(
        const JPH::ObjectLayer layer
    ) const override {
        return object_to_broad_phase_[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    [[nodiscard]] const char* GetBroadPhaseLayerName(
        const JPH::BroadPhaseLayer layer
    ) const override {
        if (layer == broad_phase_layers::static_body) {
            return "static";
        }
        if (layer == broad_phase_layers::dynamic_body) {
            return "dynamic";
        }
        return "unknown";
    }
#endif

private:
    JPH::BroadPhaseLayer object_to_broad_phase_[object_layers::count];
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    [[nodiscard]] bool ShouldCollide(
        const JPH::ObjectLayer object_layer,
        const JPH::BroadPhaseLayer broad_phase_layer
    ) const override {
        if (object_layer == object_layers::static_body) {
            return broad_phase_layer == broad_phase_layers::dynamic_body;
        }
        if (object_layer == object_layers::dynamic_body) {
            return true;
        }
        return false;
    }
};

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    [[nodiscard]] bool ShouldCollide(
        const JPH::ObjectLayer left,
        const JPH::ObjectLayer right
    ) const override {
        if (left == object_layers::static_body) {
            return right == object_layers::dynamic_body;
        }
        if (left == object_layers::dynamic_body) {
            return right == object_layers::static_body || right == object_layers::dynamic_body;
        }
        return false;
    }
};

[[nodiscard]] bool finite(const math::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finite(const math::Quat value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] float length_squared(const math::Vec3 value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

[[nodiscard]] float length_squared(const math::Quat value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
}

[[nodiscard]] JPH::Quat jolt_quaternion(const math::Quat value) noexcept {
    const float inverse_length = 1.0F / std::sqrt(length_squared(value));
    return JPH::Quat{
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
        value.w * inverse_length,
    };
}

[[nodiscard]] JPH::RVec3 jolt_position(const math::Vec3 value) noexcept {
    return JPH::RVec3{value.x, value.y, value.z};
}

[[nodiscard]] JPH::Vec3 jolt_vector(const math::Vec3 value) noexcept {
    return JPH::Vec3{value.x, value.y, value.z};
}

template <typename Vector>
[[nodiscard]] math::Vec3 vec3(const Vector& value) noexcept {
    return math::Vec3{
        static_cast<float>(value.GetX()),
        static_cast<float>(value.GetY()),
        static_cast<float>(value.GetZ()),
    };
}

[[nodiscard]] math::Vec3 point_between(
    const JPH::RVec3 point1,
    const JPH::RVec3 point2
) noexcept {
    return math::Vec3{
        static_cast<float>((point1.GetX() + point2.GetX()) * 0.5),
        static_cast<float>((point1.GetY() + point2.GetY()) * 0.5),
        static_cast<float>((point1.GetZ() + point2.GetZ()) * 0.5),
    };
}

[[nodiscard]] bool valid_config(const DomainConfig& config) noexcept {
    return config.max_bodies > 0U &&
        config.max_bodies <= JPH::BodyID::cMaxBodyIndex + 1U &&
        config.max_body_pairs > 0U &&
        config.max_contact_constraints > 0U &&
        config.temporary_allocator_bytes > 0U;
}

[[nodiscard]] std::uint32_t worker_count(const DomainConfig& config) noexcept {
    if (config.worker_threads > 0U) {
        return config.worker_threads;
    }
    const auto hardware_threads = std::max(1U, std::thread::hardware_concurrency());
    return hardware_threads > 1U ? hardware_threads - 1U : 0U;
}

[[nodiscard]] bool valid_nonnegative(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] bool valid_positive(const float value) noexcept {
    return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] bool valid_pose(const math::Vec3 position, const math::Quat orientation) noexcept {
    return finite(position) && finite(orientation) && length_squared(orientation) > 0.000001F;
}

[[nodiscard]] bool valid_query_direction(
    const math::Vec3 direction,
    const float max_distance
) noexcept {
    return finite(direction) && valid_positive(max_distance) && length_squared(direction) > 0.000001F;
}

[[nodiscard]] JPH::Vec3 query_displacement(
    const math::Vec3 direction,
    const float max_distance
) noexcept {
    const float inverse_length = 1.0F / std::sqrt(length_squared(direction));
    return JPH::Vec3{
        direction.x * inverse_length * max_distance,
        direction.y * inverse_length * max_distance,
        direction.z * inverse_length * max_distance,
    };
}

[[nodiscard]] float triangle_area_squared(
    const math::Vec3 a,
    const math::Vec3 b,
    const math::Vec3 c
) noexcept {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float abz = b.z - a.z;
    const float acx = c.x - a.x;
    const float acy = c.y - a.y;
    const float acz = c.z - a.z;
    const float cross_x = aby * acz - abz * acy;
    const float cross_y = abz * acx - abx * acz;
    const float cross_z = abx * acy - aby * acx;
    return cross_x * cross_x + cross_y * cross_y + cross_z * cross_z;
}

[[nodiscard]] bool valid_mesh(const TriangleMeshShapeDesc& desc) noexcept {
    if (desc.vertices.size() < 3U || desc.indices.size() < 3U ||
        desc.indices.size() % 3U != 0U) {
        return false;
    }
    if (!std::all_of(
            desc.vertices.begin(),
            desc.vertices.end(),
            [](const math::Vec3 value) { return finite(value); }
        )) {
        return false;
    }
    for (std::size_t index = 0; index < desc.indices.size(); index += 3U) {
        const std::uint32_t a = desc.indices[index];
        const std::uint32_t b = desc.indices[index + 1U];
        const std::uint32_t c = desc.indices[index + 2U];
        if (a >= desc.vertices.size() || b >= desc.vertices.size() || c >= desc.vertices.size() ||
            a == b || b == c || a == c ||
            triangle_area_squared(desc.vertices[a], desc.vertices[b], desc.vertices[c]) <= 1.0e-12F) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_heightfield(const HeightFieldShapeDesc& desc) noexcept {
    if (desc.sample_count < 4U || desc.sample_count > 16'384U ||
        !finite(desc.offset) || !valid_positive(desc.sample_spacing_x) ||
        !valid_positive(desc.sample_spacing_z) || !valid_positive(desc.vertical_scale)) {
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(desc.sample_count);
    if (count > std::numeric_limits<std::size_t>::max() / count ||
        desc.samples.size() != count * count) {
        return false;
    }
    bool has_surface = false;
    for (const float sample : desc.samples) {
        if (sample == heightfield_hole_sample) {
            continue;
        }
        if (!std::isfinite(sample)) {
            return false;
        }
        has_surface = true;
    }
    return has_surface;
}

[[nodiscard]] bool read_material_scalar(
    const simulation::Material& material,
    const std::string_view name,
    std::optional<float>& output
) noexcept {
    const auto* property = material.find_property(material_property_owner, name);
    if (property == nullptr) {
        output.reset();
        return true;
    }

    double value = 0.0;
    if (const auto* number = std::get_if<double>(&property->value)) {
        value = *number;
    } else if (const auto* integer = std::get_if<std::int64_t>(&property->value)) {
        value = static_cast<double>(*integer);
    } else {
        return false;
    }

    if (!std::isfinite(value) || value < 0.0) {
        return false;
    }

    const float converted = static_cast<float>(value);
    if (!std::isfinite(converted)) {
        return false;
    }
    output = converted;
    return true;
}

[[nodiscard]] std::optional<JPH::RefConst<JPH::Shape>> create_leaf_shape(
    const ShapeDesc& desc
) {
    return std::visit(
        [](const auto& shape_desc) -> std::optional<JPH::RefConst<JPH::Shape>> {
            using T = std::decay_t<decltype(shape_desc)>;
            if constexpr (std::is_same_v<T, BoxShapeDesc>) {
                if (!finite(shape_desc.half_extents) ||
                    shape_desc.half_extents.x <= 0.0F ||
                    shape_desc.half_extents.y <= 0.0F ||
                    shape_desc.half_extents.z <= 0.0F) {
                    return std::nullopt;
                }
                JPH::BoxShapeSettings settings{jolt_vector(shape_desc.half_extents)};
                const auto result = settings.Create();
                if (result.HasError()) {
                    return std::nullopt;
                }
                return JPH::RefConst<JPH::Shape>{result.Get()};
            } else if constexpr (std::is_same_v<T, SphereShapeDesc>) {
                if (!valid_positive(shape_desc.radius)) {
                    return std::nullopt;
                }
                JPH::SphereShapeSettings settings{shape_desc.radius};
                const auto result = settings.Create();
                if (result.HasError()) {
                    return std::nullopt;
                }
                return JPH::RefConst<JPH::Shape>{result.Get()};
            } else if constexpr (std::is_same_v<T, CapsuleShapeDesc>) {
                if (!std::isfinite(shape_desc.half_height) || shape_desc.half_height < 0.0F ||
                    !valid_positive(shape_desc.radius)) {
                    return std::nullopt;
                }
                JPH::CapsuleShapeSettings settings{shape_desc.half_height, shape_desc.radius};
                const auto result = settings.Create();
                if (result.HasError()) {
                    return std::nullopt;
                }
                return JPH::RefConst<JPH::Shape>{result.Get()};
            } else if constexpr (std::is_same_v<T, ConvexHullShapeDesc>) {
                if (shape_desc.points.size() < 4U ||
                    !std::all_of(
                        shape_desc.points.begin(),
                        shape_desc.points.end(),
                        [](const math::Vec3 value) { return finite(value); }
                    )) {
                    return std::nullopt;
                }
                JPH::Array<JPH::Vec3> points;
                points.reserve(shape_desc.points.size());
                for (const auto point : shape_desc.points) {
                    points.push_back(jolt_vector(point));
                }
                JPH::ConvexHullShapeSettings settings{points};
                const auto result = settings.Create();
                if (result.HasError()) {
                    return std::nullopt;
                }
                return JPH::RefConst<JPH::Shape>{result.Get()};
            } else if constexpr (std::is_same_v<T, TriangleMeshShapeDesc>) {
                if (!valid_mesh(shape_desc)) {
                    return std::nullopt;
                }
                JPH::VertexList vertices;
                vertices.reserve(shape_desc.vertices.size());
                for (const auto vertex : shape_desc.vertices) {
                    vertices.emplace_back(vertex.x, vertex.y, vertex.z);
                }
                JPH::IndexedTriangleList triangles;
                triangles.reserve(shape_desc.indices.size() / 3U);
                for (std::size_t index = 0; index < shape_desc.indices.size(); index += 3U) {
                    triangles.emplace_back(
                        shape_desc.indices[index],
                        shape_desc.indices[index + 1U],
                        shape_desc.indices[index + 2U]
                    );
                }
                JPH::MeshShapeSettings settings{std::move(vertices), std::move(triangles)};
                const auto result = settings.Create();
                if (result.HasError()) {
                    return std::nullopt;
                }
                return JPH::RefConst<JPH::Shape>{result.Get()};
            } else if constexpr (std::is_same_v<T, HeightFieldShapeDesc>) {
                if (!valid_heightfield(shape_desc)) {
                    return std::nullopt;
                }
                std::vector<float> samples = shape_desc.samples;
                for (float& sample : samples) {
                    if (sample == heightfield_hole_sample) {
                        sample = JPH::HeightFieldShapeConstants::cNoCollisionValue;
                    }
                }
                JPH::HeightFieldShapeSettings settings{
                    samples.data(),
                    jolt_vector(shape_desc.offset),
                    JPH::Vec3{
                        shape_desc.sample_spacing_x,
                        shape_desc.vertical_scale,
                        shape_desc.sample_spacing_z,
                    },
                    shape_desc.sample_count
                };
                settings.mBlockSize = 2;
                settings.mBitsPerSample = 16;
                const auto result = settings.Create();
                if (result.HasError()) {
                    return std::nullopt;
                }
                return JPH::RefConst<JPH::Shape>{result.Get()};
            } else {
                return std::nullopt;
            }
        },
        desc
    );
}

[[nodiscard]] bool shape_references(const ShapeDesc& desc, const ShapeId target) noexcept {
    const auto* compound = std::get_if<CompoundShapeDesc>(&desc);
    if (compound == nullptr) {
        return false;
    }
    return std::any_of(
        compound->children.begin(),
        compound->children.end(),
        [target](const CompoundShapeChild& child) {
            return child.shape == target;
        }
    );
}

} // namespace

class Domain::Impl final {
public:
    struct BodyEntry {
        simulation::ParticipantId participant{};
        simulation::MaterialId material{};
        ShapeId shape{};
        MotionType motion = MotionType::dynamic_body;
        bool trigger = false;
        JPH::BodyID body_id{};
    };

    struct MaterialEntry {
        simulation::MaterialId id{};
        std::optional<float> friction;
        std::optional<float> restitution;
    };

    struct MaterialInteractionEntry {
        simulation::MaterialId first{};
        simulation::MaterialId second{};
        MaterialInteraction interaction;
    };

    struct ShapeEntry {
        ShapeId id{};
        ShapeDesc desc;
        JPH::RefConst<JPH::Shape> shape;
    };

    class ContactListener final : public JPH::ContactListener {
    public:
        explicit ContactListener(Impl& owner) noexcept : owner_{owner} {}

        void OnContactAdded(
            const JPH::Body& body1,
            const JPH::Body& body2,
            const JPH::ContactManifold& manifold,
            JPH::ContactSettings& settings
        ) override {
            owner_.on_contact(ContactPhase::began, body1, body2, manifold, settings);
        }

        void OnContactPersisted(
            const JPH::Body& body1,
            const JPH::Body& body2,
            const JPH::ContactManifold& manifold,
            JPH::ContactSettings& settings
        ) override {
            owner_.on_contact(ContactPhase::persisted, body1, body2, manifold, settings);
        }

        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
            owner_.on_contact_removed(pair);
        }

    private:
        Impl& owner_;
    };

    Impl(
        const simulation::DomainId id,
        std::string name,
        const DomainConfig& config
    ) : id_{id},
        name_{std::move(name)},
        config_{config},
        temp_allocator_{config.temporary_allocator_bytes},
        job_system_{
            JPH::cMaxPhysicsJobs,
            JPH::cMaxPhysicsBarriers,
            static_cast<int>(worker_count(config))
        },
        contact_listener_{*this} {
        (void)runtime();
        physics_system_.Init(
            config_.max_bodies,
            0,
            config_.max_body_pairs,
            config_.max_contact_constraints,
            broad_phase_layer_interface_,
            object_vs_broad_phase_filter_,
            object_layer_pair_filter_
        );
        physics_system_.SetContactListener(&contact_listener_);
    }

    ~Impl() {
        physics_system_.SetContactListener(nullptr);
        auto& body_interface = physics_system_.GetBodyInterface();
        for (const auto& body : bodies_) {
            body_interface.RemoveBody(body.body_id);
            body_interface.DestroyBody(body.body_id);
        }
    }

    [[nodiscard]] const BodyEntry* find(
        const simulation::ParticipantId participant
    ) const noexcept {
        const auto found = std::find_if(
            bodies_.begin(),
            bodies_.end(),
            [&](const BodyEntry& body) {
                return body.participant == participant;
            }
        );
        return found != bodies_.end() ? &*found : nullptr;
    }

    [[nodiscard]] BodyEntry* find(const simulation::ParticipantId participant) noexcept {
        const auto found = std::find_if(
            bodies_.begin(),
            bodies_.end(),
            [&](const BodyEntry& body) {
                return body.participant == participant;
            }
        );
        return found != bodies_.end() ? &*found : nullptr;
    }

    [[nodiscard]] const BodyEntry* find(const JPH::BodyID id) const noexcept {
        const auto found = std::find_if(
            bodies_.begin(),
            bodies_.end(),
            [id](const BodyEntry& body) {
                return body.body_id == id;
            }
        );
        return found != bodies_.end() ? &*found : nullptr;
    }

    [[nodiscard]] const MaterialEntry* find_material(
        const simulation::MaterialId id
    ) const noexcept {
        const auto found = std::find_if(
            materials_.begin(),
            materials_.end(),
            [id](const MaterialEntry& material) {
                return material.id == id;
            }
        );
        return found != materials_.end() ? &*found : nullptr;
    }

    [[nodiscard]] const ShapeEntry* find_shape(const ShapeId id) const noexcept {
        const auto found = std::find_if(
            shapes_.begin(),
            shapes_.end(),
            [id](const ShapeEntry& shape) {
                return shape.id == id;
            }
        );
        return found != shapes_.end() ? &*found : nullptr;
    }

    [[nodiscard]] static std::pair<simulation::MaterialId, simulation::MaterialId>
    ordered_materials(
        const simulation::MaterialId first,
        const simulation::MaterialId second
    ) noexcept {
        if (second.value < first.value) {
            return {second, first};
        }
        return {first, second};
    }

    [[nodiscard]] const MaterialInteractionEntry* find_material_interaction(
        const simulation::MaterialId first,
        const simulation::MaterialId second
    ) const noexcept {
        if (!first || !second) {
            return nullptr;
        }
        const auto ordered = ordered_materials(first, second);
        const auto found = std::find_if(
            material_interactions_.begin(),
            material_interactions_.end(),
            [&](const MaterialInteractionEntry& entry) {
                return entry.first == ordered.first && entry.second == ordered.second;
            }
        );
        return found != material_interactions_.end() ? &*found : nullptr;
    }

    [[nodiscard]] bool accepts_query_body(
        const BodyEntry& body,
        const QueryFilter& filter
    ) const noexcept {
        if (filter.exclude_participant.has_value() &&
            body.participant == *filter.exclude_participant) {
            return false;
        }
        if (body.trigger && !filter.include_triggers) {
            return false;
        }
        if (body.motion == MotionType::static_body && !filter.include_static) {
            return false;
        }
        if (body.motion != MotionType::static_body && !filter.include_dynamic) {
            return false;
        }
        return true;
    }

    void apply_material_interaction(
        const BodyEntry& first,
        const BodyEntry& second,
        JPH::ContactSettings& settings
    ) const noexcept {
        const auto* interaction = find_material_interaction(first.material, second.material);
        if (interaction == nullptr) {
            return;
        }
        if (interaction->interaction.friction.has_value()) {
            settings.mCombinedFriction = *interaction->interaction.friction;
        }
        if (interaction->interaction.restitution.has_value()) {
            settings.mCombinedRestitution = *interaction->interaction.restitution;
        }
    }

    bool create_native_body(
        const BodyDesc& desc,
        const JPH::Shape* shape,
        const ShapeId shape_id
    ) {
        const auto* material = desc.material ? find_material(desc.material) : nullptr;
        if (!desc.participant || find(desc.participant) != nullptr || shape == nullptr ||
            (desc.material && material == nullptr) ||
            !valid_pose(desc.position, desc.orientation) ||
            !finite(desc.linear_velocity) || !finite(desc.angular_velocity) ||
            !valid_nonnegative(desc.friction) || !valid_nonnegative(desc.restitution)) {
            return false;
        }

        const bool dynamic = desc.motion == MotionType::dynamic_body;
        const bool kinematic = desc.motion == MotionType::kinematic_body;
        const bool moving = dynamic || kinematic;
        if ((!moving && desc.motion != MotionType::static_body) || (moving && shape->MustBeStatic())) {
            return false;
        }

        JPH::BodyCreationSettings body_settings{
            shape,
            jolt_position(desc.position),
            jolt_quaternion(desc.orientation),
            dynamic ? JPH::EMotionType::Dynamic : (kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static),
            moving ? object_layers::dynamic_body : object_layers::static_body
        };
        body_settings.mFriction = material != nullptr && material->friction.has_value()
            ? *material->friction
            : desc.friction;
        body_settings.mRestitution = material != nullptr && material->restitution.has_value()
            ? *material->restitution
            : desc.restitution;
        body_settings.mIsSensor = desc.trigger;
        body_settings.mUserData = desc.participant.value;
        if (moving) {
            body_settings.mLinearVelocity = jolt_vector(desc.linear_velocity);
            body_settings.mAngularVelocity = jolt_vector(desc.angular_velocity);
        }

        auto& body_interface = physics_system_.GetBodyInterface();
        const JPH::BodyID body_id = body_interface.CreateAndAddBody(
            body_settings,
            moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate
        );
        if (body_id.IsInvalid()) {
            return false;
        }

        try {
            bodies_.push_back(BodyEntry{
                .participant = desc.participant,
                .material = desc.material,
                .shape = shape_id,
                .motion = desc.motion,
                .trigger = desc.trigger,
                .body_id = body_id,
            });
        } catch (...) {
            body_interface.RemoveBody(body_id);
            body_interface.DestroyBody(body_id);
            throw;
        }
        return true;
    }

    void on_contact(
        const ContactPhase phase,
        const JPH::Body& body1,
        const JPH::Body& body2,
        const JPH::ContactManifold& manifold,
        JPH::ContactSettings& settings
    ) {
        const simulation::ParticipantId participant1{body1.GetUserData()};
        const simulation::ParticipantId participant2{body2.GetUserData()};
        const auto* entry1 = find(participant1);
        const auto* entry2 = find(participant2);
        if (entry1 == nullptr || entry2 == nullptr) {
            return;
        }

        apply_material_interaction(*entry1, *entry2, settings);

        const bool has_points = !manifold.mRelativeContactPointsOn1.empty() &&
            !manifold.mRelativeContactPointsOn2.empty();
        const JPH::RVec3 point1 = has_points
            ? manifold.GetWorldSpaceContactPointOn1(0)
            : manifold.mBaseOffset;
        const JPH::RVec3 point2 = has_points
            ? manifold.GetWorldSpaceContactPointOn2(0)
            : manifold.mBaseOffset;

        const JPH::Vec3 relative_velocity =
            body2.GetPointVelocity(point2) - body1.GetPointVelocity(point1);
        const float closing_speed = std::max(
            0.0F,
            -relative_velocity.Dot(manifold.mWorldSpaceNormal)
        );

        const bool trigger = entry1->trigger || entry2->trigger ||
            body1.IsSensor() || body2.IsSensor() || settings.mIsSensor;

        float estimated_normal_impulse = 0.0F;
        if (!trigger) {
            JPH::CollisionEstimationResult estimate;
            JPH::EstimateCollisionResponse(
                body1,
                body2,
                manifold,
                estimate,
                settings.mCombinedFriction,
                settings.mCombinedRestitution
            );
            for (const float impulse : estimate.mContactImpulse) {
                estimated_normal_impulse += std::abs(impulse);
            }
        }

        ContactEvent event{
            .phase = phase,
            .kind = trigger ? ContactKind::trigger : ContactKind::collision,
            .first = participant1,
            .second = participant2,
            .first_material = entry1->material,
            .second_material = entry2->material,
            .point = point_between(point1, point2),
            .normal = vec3(manifold.mWorldSpaceNormal),
            .penetration_depth = manifold.mPenetrationDepth,
            .closing_speed = closing_speed,
            .estimated_normal_impulse = estimated_normal_impulse,
            .combined_friction = settings.mCombinedFriction,
            .combined_restitution = settings.mCombinedRestitution,
        };

        if (const auto* interaction = find_material_interaction(
                entry1->material,
                entry2->material
            )) {
            event.material_interaction = interaction->interaction.label;
        }

        if (event.second.value < event.first.value) {
            std::swap(event.first, event.second);
            std::swap(event.first_material, event.second_material);
            event.normal.x = -event.normal.x;
            event.normal.y = -event.normal.y;
            event.normal.z = -event.normal.z;
        }

        const JPH::SubShapeIDPair pair{
            body1.GetID(),
            manifold.mSubShapeID1,
            body2.GetID(),
            manifold.mSubShapeID2
        };

        std::scoped_lock lock{contact_mutex_};
        contact_cache_[pair] = event;
        pending_contact_events_.push_back(std::move(event));
    }

    void on_contact_removed(const JPH::SubShapeIDPair& pair) {
        std::scoped_lock lock{contact_mutex_};
        const auto found = contact_cache_.find(pair);
        if (found == contact_cache_.end()) {
            return;
        }

        ContactEvent event = found->second;
        event.phase = ContactPhase::ended;
        event.penetration_depth = 0.0F;
        event.closing_speed = 0.0F;
        event.estimated_normal_impulse = 0.0F;
        pending_contact_events_.push_back(std::move(event));
        contact_cache_.erase(found);
    }

    void publish_contact_events() {
        {
            std::scoped_lock lock{contact_mutex_};
            contact_events_ = std::move(pending_contact_events_);
            pending_contact_events_.clear();
        }

        std::sort(
            contact_events_.begin(),
            contact_events_.end(),
            [](const ContactEvent& left, const ContactEvent& right) {
                return std::tuple{
                    left.first.value,
                    left.second.value,
                    static_cast<std::uint8_t>(left.kind),
                    static_cast<std::uint8_t>(left.phase),
                    left.first_material.value,
                    left.second_material.value,
                    left.point.x,
                    left.point.y,
                    left.point.z
                } < std::tuple{
                    right.first.value,
                    right.second.value,
                    static_cast<std::uint8_t>(right.kind),
                    static_cast<std::uint8_t>(right.phase),
                    right.first_material.value,
                    right.second_material.value,
                    right.point.x,
                    right.point.y,
                    right.point.z
                };
            }
        );
    }

    simulation::DomainId id_{};
    std::string name_;
    DomainConfig config_{};
    simulation::Fidelity fidelity_ = simulation::Fidelity::full;
    BroadPhaseLayerInterface broad_phase_layer_interface_;
    ObjectVsBroadPhaseFilter object_vs_broad_phase_filter_;
    ObjectLayerPairFilter object_layer_pair_filter_;
    JPH::PhysicsSystem physics_system_;
    JPH::TempAllocatorImpl temp_allocator_;
    JPH::JobSystemThreadPool job_system_;
    ContactListener contact_listener_;
    std::vector<BodyEntry> bodies_;
    std::vector<MaterialEntry> materials_;
    std::vector<MaterialInteractionEntry> material_interactions_;
    std::vector<ShapeEntry> shapes_;
    std::mutex contact_mutex_;
    std::unordered_map<JPH::SubShapeIDPair, ContactEvent> contact_cache_;
    std::vector<ContactEvent> pending_contact_events_;
    std::vector<ContactEvent> contact_events_;
};

std::unique_ptr<Domain> Domain::create(
    const simulation::DomainId id,
    std::string name,
    const DomainConfig config
) {
    if (!id || name.empty() || !valid_config(config)) {
        return nullptr;
    }
    (void)runtime();
    return std::unique_ptr<Domain>{new Domain{
        std::make_unique<Impl>(id, std::move(name), config)
    }};
}

Domain::Domain(std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)} {}

Domain::~Domain() = default;

simulation::DomainId Domain::id() const noexcept {
    return impl_->id_;
}

std::string_view Domain::name() const noexcept {
    return impl_->name_;
}

simulation::Fidelity Domain::fidelity() const noexcept {
    return impl_->fidelity_;
}

void Domain::set_fidelity(const simulation::Fidelity fidelity) noexcept {
    impl_->fidelity_ = fidelity;
}

void Domain::step(
    const simulation::StepContext& context,
    simulation::InteractionEmitter& emitter
) {
    (void)emitter;
    const auto collision_steps = std::max(
        1,
        static_cast<int>(std::ceil(context.fixed_delta_seconds * 60.0))
    );
    (void)impl_->physics_system_.Update(
        static_cast<float>(context.fixed_delta_seconds),
        collision_steps,
        &impl_->temp_allocator_,
        &impl_->job_system_
    );
    impl_->publish_contact_events();
}

bool Domain::register_material(
    const simulation::MaterialId id,
    const simulation::Material& material
) {
    if (!id || impl_->find_material(id) != nullptr) {
        return false;
    }

    Impl::MaterialEntry entry{.id = id};
    if (!read_material_scalar(material, material_friction_property, entry.friction) ||
        !read_material_scalar(material, material_restitution_property, entry.restitution)) {
        return false;
    }

    impl_->materials_.push_back(entry);
    return true;
}

bool Domain::unregister_material(const simulation::MaterialId id) {
    const auto material = std::find_if(
        impl_->materials_.begin(),
        impl_->materials_.end(),
        [id](const Impl::MaterialEntry& entry) {
            return entry.id == id;
        }
    );
    if (material == impl_->materials_.end()) {
        return false;
    }

    const bool in_use = std::any_of(
        impl_->bodies_.begin(),
        impl_->bodies_.end(),
        [id](const Impl::BodyEntry& entry) {
            return entry.material == id;
        }
    );
    if (in_use) {
        return false;
    }

    impl_->materials_.erase(material);
    std::erase_if(
        impl_->material_interactions_,
        [id](const Impl::MaterialInteractionEntry& entry) {
            return entry.first == id || entry.second == id;
        }
    );
    return true;
}

bool Domain::set_material_interaction(
    const simulation::MaterialId first,
    const simulation::MaterialId second,
    MaterialInteraction interaction
) {
    if (!first || !second || impl_->find_material(first) == nullptr ||
        impl_->find_material(second) == nullptr ||
        (interaction.friction.has_value() && !valid_nonnegative(*interaction.friction)) ||
        (interaction.restitution.has_value() && !valid_nonnegative(*interaction.restitution)) ||
        (!interaction.friction.has_value() && !interaction.restitution.has_value() &&
            interaction.label.empty())) {
        return false;
    }

    const auto ordered = Impl::ordered_materials(first, second);
    const auto found = std::find_if(
        impl_->material_interactions_.begin(),
        impl_->material_interactions_.end(),
        [&](const Impl::MaterialInteractionEntry& entry) {
            return entry.first == ordered.first && entry.second == ordered.second;
        }
    );
    if (found != impl_->material_interactions_.end()) {
        found->interaction = std::move(interaction);
        return true;
    }

    impl_->material_interactions_.push_back(Impl::MaterialInteractionEntry{
        .first = ordered.first,
        .second = ordered.second,
        .interaction = std::move(interaction),
    });
    return true;
}

bool Domain::remove_material_interaction(
    const simulation::MaterialId first,
    const simulation::MaterialId second
) {
    if (!first || !second) {
        return false;
    }

    const auto ordered = Impl::ordered_materials(first, second);
    const auto found = std::find_if(
        impl_->material_interactions_.begin(),
        impl_->material_interactions_.end(),
        [&](const Impl::MaterialInteractionEntry& entry) {
            return entry.first == ordered.first && entry.second == ordered.second;
        }
    );
    if (found == impl_->material_interactions_.end()) {
        return false;
    }
    impl_->material_interactions_.erase(found);
    return true;
}

bool Domain::register_shape(const ShapeId id, const ShapeDesc& desc) {
    if (!id || impl_->find_shape(id) != nullptr) {
        return false;
    }

    std::optional<JPH::RefConst<JPH::Shape>> shape;
    if (const auto* compound = std::get_if<CompoundShapeDesc>(&desc)) {
        if (compound->children.empty()) {
            return false;
        }
        JPH::StaticCompoundShapeSettings settings;
        for (const auto& child : compound->children) {
            if (!child.shape || child.shape == id ||
                !valid_pose(child.position, child.orientation)) {
                return false;
            }
            const auto* child_shape = impl_->find_shape(child.shape);
            if (child_shape == nullptr) {
                return false;
            }
            settings.AddShape(
                jolt_vector(child.position),
                jolt_quaternion(child.orientation),
                child_shape->shape.GetPtr()
            );
        }
        const auto result = settings.Create();
        if (result.HasError()) {
            return false;
        }
        shape = JPH::RefConst<JPH::Shape>{result.Get()};
    } else {
        shape = create_leaf_shape(desc);
    }

    if (!shape.has_value()) {
        return false;
    }
    impl_->shapes_.push_back(Impl::ShapeEntry{
        .id = id,
        .desc = desc,
        .shape = *shape,
    });
    return true;
}

bool Domain::unregister_shape(const ShapeId id) {
    const auto found = std::find_if(
        impl_->shapes_.begin(),
        impl_->shapes_.end(),
        [id](const Impl::ShapeEntry& entry) {
            return entry.id == id;
        }
    );
    if (found == impl_->shapes_.end()) {
        return false;
    }
    const bool body_in_use = std::any_of(
        impl_->bodies_.begin(),
        impl_->bodies_.end(),
        [id](const Impl::BodyEntry& body) {
            return body.shape == id;
        }
    );
    const bool shape_in_use = std::any_of(
        impl_->shapes_.begin(),
        impl_->shapes_.end(),
        [id](const Impl::ShapeEntry& shape) {
            return shape.id != id && shape_references(shape.desc, id);
        }
    );
    if (body_in_use || shape_in_use) {
        return false;
    }
    impl_->shapes_.erase(found);
    return true;
}

bool Domain::create_body(const BodyDesc& desc) {
    const auto* shape = impl_->find_shape(desc.shape);
    if (!desc.shape || shape == nullptr) {
        return false;
    }
    return impl_->create_native_body(desc, shape->shape.GetPtr(), desc.shape);
}

bool Domain::create_box(const BoxBodyDesc& desc) {
    if (!finite(desc.half_extents) || desc.half_extents.x <= 0.0F ||
        desc.half_extents.y <= 0.0F || desc.half_extents.z <= 0.0F) {
        return false;
    }

    JPH::BoxShapeSettings settings{jolt_vector(desc.half_extents)};
    const auto shape_result = settings.Create();
    if (shape_result.HasError()) {
        return false;
    }

    BodyDesc body;
    body.participant = desc.participant;
    body.material = desc.material;
    body.motion = desc.motion;
    body.position = desc.position;
    body.orientation = desc.orientation;
    body.linear_velocity = desc.linear_velocity;
    body.angular_velocity = desc.angular_velocity;
    body.friction = desc.friction;
    body.restitution = desc.restitution;
    body.trigger = desc.trigger;
    return impl_->create_native_body(body, shape_result.Get(), ShapeId{});
}

bool Domain::destroy_body(const simulation::ParticipantId participant) {
    auto* body = impl_->find(participant);
    if (body == nullptr) {
        return false;
    }

    const JPH::BodyID body_id = body->body_id;
    auto& body_interface = impl_->physics_system_.GetBodyInterface();
    body_interface.RemoveBody(body_id);
    body_interface.DestroyBody(body_id);
    std::erase_if(
        impl_->bodies_,
        [&](const Impl::BodyEntry& entry) {
            return entry.participant == participant;
        }
    );
    return true;
}

bool Domain::set_body_active(
    const simulation::ParticipantId participant,
    const bool active
) {
    auto* body = impl_->find(participant);
    if (body == nullptr) {
        return false;
    }

    if (body->motion == MotionType::static_body) {
        return !active;
    }

    auto& body_interface = impl_->physics_system_.GetBodyInterface();
    if (active) {
        body_interface.ActivateBody(body->body_id);
    } else {
        body_interface.DeactivateBody(body->body_id);
    }
    return true;
}

std::optional<BodyState> Domain::body_state(
    const simulation::ParticipantId participant
) const noexcept {
    const auto* body = impl_->find(participant);
    if (body == nullptr) {
        return std::nullopt;
    }

    const auto& body_interface = impl_->physics_system_.GetBodyInterface();
    const JPH::RVec3 position = body_interface.GetCenterOfMassPosition(body->body_id);
    const JPH::Quat orientation = body_interface.GetRotation(body->body_id);
    const JPH::Vec3 velocity = body_interface.GetLinearVelocity(body->body_id);
    const JPH::Vec3 angular_velocity = body_interface.GetAngularVelocity(body->body_id);
    return BodyState{
        .position = vec3(position),
        .orientation = math::Quat{
            orientation.GetX(),
            orientation.GetY(),
            orientation.GetZ(),
            orientation.GetW(),
        },
        .linear_velocity = vec3(velocity),
        .angular_velocity = vec3(angular_velocity),
        .active = body_interface.IsActive(body->body_id),
    };
}

std::optional<QueryHit> Domain::raycast(
    const RaycastDesc& desc,
    const QueryFilter& filter
) const {
    if (!finite(desc.origin) || !valid_query_direction(desc.direction, desc.max_distance) ||
        (!filter.include_static && !filter.include_dynamic)) {
        return std::nullopt;
    }

    const JPH::Vec3 displacement = query_displacement(desc.direction, desc.max_distance);
    const JPH::RRayCast ray{jolt_position(desc.origin), displacement};
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    impl_->physics_system_.GetNarrowPhaseQuery().CastRay(
        ray,
        JPH::RayCastSettings{},
        collector
    );

    const auto& body_interface = impl_->physics_system_.GetBodyInterface();
    std::optional<QueryHit> closest;
    for (const auto& hit : collector.mHits) {
        const auto* body = impl_->find(hit.mBodyID);
        if (body == nullptr || !impl_->accepts_query_body(*body, filter)) {
            continue;
        }
        if (!std::isfinite(hit.mFraction) || hit.mFraction < 0.0F || hit.mFraction > 1.0F) {
            continue;
        }

        const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
        const auto transformed = body_interface.GetTransformedShape(hit.mBodyID);
        const JPH::Vec3 normal = transformed.GetWorldSpaceSurfaceNormal(
            hit.mSubShapeID2,
            point
        );
        QueryHit converted{
            .participant = body->participant,
            .material = body->material,
            .trigger = body->trigger,
            .point = vec3(point),
            .normal = vec3(normal),
            .distance = hit.mFraction * desc.max_distance,
            .fraction = hit.mFraction,
            .penetration_depth = 0.0F,
        };
        if (!closest.has_value() || converted.fraction < closest->fraction ||
            (converted.fraction == closest->fraction &&
                converted.participant.value < closest->participant.value)) {
            closest = converted;
        }
    }
    return closest;
}

std::vector<QueryHit> Domain::overlap_shape(
    const ShapePose& pose,
    const QueryFilter& filter
) const {
    std::vector<QueryHit> results;
    const auto* shape = impl_->find_shape(pose.shape);
    if (shape == nullptr || !pose.shape || shape->shape->MustBeStatic() ||
        !valid_pose(pose.position, pose.orientation) ||
        (!filter.include_static && !filter.include_dynamic)) {
        return results;
    }

    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    JPH::CollideShapeSettings settings;
    impl_->physics_system_.GetNarrowPhaseQuery().CollideShape(
        shape->shape.GetPtr(),
        JPH::Vec3::sOne(),
        JPH::RMat44::sRotationTranslation(
            jolt_quaternion(pose.orientation),
            jolt_position(pose.position)
        ),
        settings,
        JPH::RVec3::sZero(),
        collector
    );

    for (const auto& hit : collector.mHits) {
        const auto* body = impl_->find(hit.mBodyID2);
        if (body == nullptr || !impl_->accepts_query_body(*body, filter)) {
            continue;
        }
        JPH::Vec3 normal = -hit.mPenetrationAxis;
        if (normal.LengthSq() > 0.000001F) {
            normal = normal.Normalized();
        } else {
            normal = JPH::Vec3::sZero();
        }
        results.push_back(QueryHit{
            .participant = body->participant,
            .material = body->material,
            .trigger = body->trigger,
            .point = vec3(hit.mContactPointOn2),
            .normal = vec3(normal),
            .distance = 0.0F,
            .fraction = 0.0F,
            .penetration_depth = std::max(0.0F, hit.mPenetrationDepth),
        });
    }

    std::sort(
        results.begin(),
        results.end(),
        [](const QueryHit& left, const QueryHit& right) {
            if (left.participant.value != right.participant.value) {
                return left.participant.value < right.participant.value;
            }
            if (left.penetration_depth != right.penetration_depth) {
                return left.penetration_depth > right.penetration_depth;
            }
            return std::tuple{left.point.x, left.point.y, left.point.z} <
                std::tuple{right.point.x, right.point.y, right.point.z};
        }
    );
    results.erase(
        std::unique(
            results.begin(),
            results.end(),
            [](const QueryHit& left, const QueryHit& right) {
                return left.participant == right.participant;
            }
        ),
        results.end()
    );
    return results;
}

std::optional<QueryHit> Domain::sweep_shape(
    const SweepDesc& desc,
    const QueryFilter& filter
) const {
    const auto* shape = impl_->find_shape(desc.pose.shape);
    if (shape == nullptr || !desc.pose.shape || shape->shape->MustBeStatic() ||
        !valid_pose(desc.pose.position, desc.pose.orientation) ||
        !valid_query_direction(desc.direction, desc.max_distance) ||
        (!filter.include_static && !filter.include_dynamic)) {
        return std::nullopt;
    }

    const JPH::Vec3 displacement = query_displacement(desc.direction, desc.max_distance);
    const JPH::RShapeCast cast{
        shape->shape.GetPtr(),
        JPH::Vec3::sOne(),
        JPH::RMat44::sRotationTranslation(
            jolt_quaternion(desc.pose.orientation),
            jolt_position(desc.pose.position)
        ),
        displacement
    };
    JPH::AllHitCollisionCollector<JPH::CastShapeCollector> collector;
    impl_->physics_system_.GetNarrowPhaseQuery().CastShape(
        cast,
        JPH::ShapeCastSettings{},
        JPH::RVec3::sZero(),
        collector
    );

    std::optional<QueryHit> closest;
    for (const auto& hit : collector.mHits) {
        const auto* body = impl_->find(hit.mBodyID2);
        if (body == nullptr || !impl_->accepts_query_body(*body, filter)) {
            continue;
        }
        if (!std::isfinite(hit.mFraction) || hit.mFraction < 0.0F || hit.mFraction > 1.0F) {
            continue;
        }
        JPH::Vec3 normal = -hit.mPenetrationAxis;
        if (normal.LengthSq() > 0.000001F) {
            normal = normal.Normalized();
        } else {
            normal = JPH::Vec3::sZero();
        }
        QueryHit converted{
            .participant = body->participant,
            .material = body->material,
            .trigger = body->trigger,
            .point = vec3(hit.mContactPointOn2),
            .normal = vec3(normal),
            .distance = hit.mFraction * desc.max_distance,
            .fraction = hit.mFraction,
            .penetration_depth = std::max(0.0F, hit.mPenetrationDepth),
        };
        if (!closest.has_value() || converted.fraction < closest->fraction ||
            (converted.fraction == closest->fraction &&
                converted.participant.value < closest->participant.value)) {
            closest = converted;
        }
    }
    return closest;
}

std::span<const ContactEvent> Domain::contact_events() const noexcept {
    return impl_->contact_events_;
}

std::size_t Domain::body_count() const noexcept {
    return impl_->bodies_.size();
}

std::size_t Domain::material_count() const noexcept {
    return impl_->materials_.size();
}

std::size_t Domain::material_interaction_count() const noexcept {
    return impl_->material_interactions_.size();
}

std::size_t Domain::shape_count() const noexcept {
    return impl_->shapes_.size();
}

} // namespace vice::rigid_body
