#include <vice/rigid_body/rigid_body.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/EstimateCollisionResponse.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeIDPair.h>
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

[[nodiscard]] math::Vec3 vec3(const JPH::Vec3 value) noexcept {
    return math::Vec3{value.GetX(), value.GetY(), value.GetZ()};
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

} // namespace

class Domain::Impl final {
public:
    struct BodyEntry {
        simulation::ParticipantId participant{};
        simulation::MaterialId material{};
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

    class ContactListener final : public JPH::ContactListener {
    public:
        explicit ContactListener(Impl& owner) noexcept : owner_{owner} {}

        void OnContactAdded(
            const JPH::Body& body1,
            const JPH::Body& body2,
            const JPH::ContactManifold& manifold,
            JPH::ContactSettings& settings
        ) override {
            owner_.on_contact(
                ContactPhase::began,
                body1,
                body2,
                manifold,
                settings
            );
        }

        void OnContactPersisted(
            const JPH::Body& body1,
            const JPH::Body& body2,
            const JPH::ContactManifold& manifold,
            JPH::ContactSettings& settings
        ) override {
            owner_.on_contact(
                ContactPhase::persisted,
                body1,
                body2,
                manifold,
                settings
            );
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

bool Domain::create_box(const BoxBodyDesc& desc) {
    const float orientation_length_squared = length_squared(desc.orientation);
    const auto* material = desc.material ? impl_->find_material(desc.material) : nullptr;
    if (!desc.participant || impl_->find(desc.participant) != nullptr ||
        (desc.material && material == nullptr) ||
        !finite(desc.half_extents) || !finite(desc.position) || !finite(desc.orientation) ||
        !finite(desc.linear_velocity) || !finite(desc.angular_velocity) ||
        orientation_length_squared <= 0.000001F ||
        desc.half_extents.x <= 0.0F || desc.half_extents.y <= 0.0F ||
        desc.half_extents.z <= 0.0F || !valid_nonnegative(desc.friction) ||
        !valid_nonnegative(desc.restitution)) {
        return false;
    }

    JPH::BoxShapeSettings shape_settings{JPH::Vec3{
        desc.half_extents.x,
        desc.half_extents.y,
        desc.half_extents.z
    }};
    const auto shape_result = shape_settings.Create();
    if (shape_result.HasError()) {
        return false;
    }

    const bool dynamic = desc.motion == MotionType::dynamic_body;
    JPH::BodyCreationSettings body_settings{
        shape_result.Get(),
        JPH::RVec3{desc.position.x, desc.position.y, desc.position.z},
        jolt_quaternion(desc.orientation),
        dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
        dynamic ? object_layers::dynamic_body : object_layers::static_body
    };
    body_settings.mFriction = material != nullptr && material->friction.has_value()
        ? *material->friction
        : desc.friction;
    body_settings.mRestitution = material != nullptr && material->restitution.has_value()
        ? *material->restitution
        : desc.restitution;
    body_settings.mIsSensor = desc.trigger;
    body_settings.mUserData = desc.participant.value;
    if (dynamic) {
        body_settings.mLinearVelocity = JPH::Vec3{
            desc.linear_velocity.x,
            desc.linear_velocity.y,
            desc.linear_velocity.z
        };
        body_settings.mAngularVelocity = JPH::Vec3{
            desc.angular_velocity.x,
            desc.angular_velocity.y,
            desc.angular_velocity.z
        };
    }

    auto& body_interface = impl_->physics_system_.GetBodyInterface();
    const JPH::BodyID body_id = body_interface.CreateAndAddBody(
        body_settings,
        dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate
    );
    if (body_id.IsInvalid()) {
        return false;
    }

    try {
        impl_->bodies_.push_back(Impl::BodyEntry{
            .participant = desc.participant,
            .material = desc.material,
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
        .position = math::Vec3{
            static_cast<float>(position.GetX()),
            static_cast<float>(position.GetY()),
            static_cast<float>(position.GetZ()),
        },
        .orientation = math::Quat{
            orientation.GetX(),
            orientation.GetY(),
            orientation.GetZ(),
            orientation.GetW(),
        },
        .linear_velocity = math::Vec3{
            velocity.GetX(),
            velocity.GetY(),
            velocity.GetZ(),
        },
        .angular_velocity = math::Vec3{
            angular_velocity.GetX(),
            angular_velocity.GetY(),
            angular_velocity.GetZ(),
        },
        .active = body_interface.IsActive(body->body_id),
    };
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

} // namespace vice::rigid_body
