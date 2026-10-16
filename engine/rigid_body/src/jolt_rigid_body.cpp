#include <vice/rigid_body/rigid_body.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
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

} // namespace

class Domain::Impl final {
public:
    struct BodyEntry {
        simulation::ParticipantId participant{};
        JPH::BodyID body_id{};
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
        } {
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
    }

    ~Impl() {
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
    std::vector<BodyEntry> bodies_;
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
}

bool Domain::create_box(const BoxBodyDesc& desc) {
    if (!desc.participant || impl_->find(desc.participant) != nullptr ||
        !finite(desc.half_extents) || !finite(desc.position) || !finite(desc.linear_velocity) ||
        desc.half_extents.x <= 0.0F || desc.half_extents.y <= 0.0F ||
        desc.half_extents.z <= 0.0F || !std::isfinite(desc.friction) ||
        !std::isfinite(desc.restitution) || desc.friction < 0.0F ||
        desc.restitution < 0.0F) {
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
        JPH::Quat::sIdentity(),
        dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
        dynamic ? object_layers::dynamic_body : object_layers::static_body
    };
    body_settings.mFriction = desc.friction;
    body_settings.mRestitution = desc.restitution;
    if (dynamic) {
        body_settings.mLinearVelocity = JPH::Vec3{
            desc.linear_velocity.x,
            desc.linear_velocity.y,
            desc.linear_velocity.z
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

std::optional<BodyState> Domain::body_state(
    const simulation::ParticipantId participant
) const noexcept {
    const auto* body = impl_->find(participant);
    if (body == nullptr) {
        return std::nullopt;
    }

    const auto& body_interface = impl_->physics_system_.GetBodyInterface();
    const JPH::RVec3 position = body_interface.GetCenterOfMassPosition(body->body_id);
    const JPH::Vec3 velocity = body_interface.GetLinearVelocity(body->body_id);
    return BodyState{
        .position = math::Vec3{
            static_cast<float>(position.GetX()),
            static_cast<float>(position.GetY()),
            static_cast<float>(position.GetZ()),
        },
        .linear_velocity = math::Vec3{
            velocity.GetX(),
            velocity.GetY(),
            velocity.GetZ(),
        },
        .active = body_interface.IsActive(body->body_id),
    };
}

std::size_t Domain::body_count() const noexcept {
    return impl_->bodies_.size();
}

} // namespace vice::rigid_body
