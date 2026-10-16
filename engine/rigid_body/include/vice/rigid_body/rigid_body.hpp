#pragma once

#include <vice/math/math.hpp>
#include <vice/simulation/simulation.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vice::rigid_body {

inline constexpr std::string_view material_property_owner = "rigid_body";
inline constexpr std::string_view material_friction_property = "friction";
inline constexpr std::string_view material_restitution_property = "restitution";
inline constexpr float heightfield_hole_sample = std::numeric_limits<float>::infinity();

struct ShapeId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return value != 0;
    }

    constexpr auto operator<=>(const ShapeId&) const noexcept = default;
};

enum class MotionType : std::uint8_t {
    static_body,
    dynamic_body,
};

enum class ContactPhase : std::uint8_t {
    began,
    persisted,
    ended,
};

enum class ContactKind : std::uint8_t {
    collision,
    trigger,
};

struct DomainConfig {
    std::uint32_t max_bodies = 4096;
    std::uint32_t max_body_pairs = 4096;
    std::uint32_t max_contact_constraints = 2048;
    std::uint32_t temporary_allocator_bytes = 10U * 1024U * 1024U;
    std::uint32_t worker_threads = 0;
};

struct MaterialInteraction {
    std::optional<float> friction;
    std::optional<float> restitution;
    std::string label;
};

struct BoxShapeDesc {
    math::Vec3 half_extents{0.5F, 0.5F, 0.5F};
};

struct SphereShapeDesc {
    float radius = 0.5F;
};

struct CapsuleShapeDesc {
    float half_height = 0.5F;
    float radius = 0.25F;
};

struct ConvexHullShapeDesc {
    std::vector<math::Vec3> points;
};

struct TriangleMeshShapeDesc {
    std::vector<math::Vec3> vertices;
    std::vector<std::uint32_t> indices;
};

struct HeightFieldShapeDesc {
    std::uint32_t sample_count = 0;
    math::Vec3 offset{};
    float sample_spacing_x = 1.0F;
    float sample_spacing_z = 1.0F;
    float vertical_scale = 1.0F;
    std::vector<float> samples;
};

struct CompoundShapeChild {
    ShapeId shape{};
    math::Vec3 position{};
    math::Quat orientation{};
};

struct CompoundShapeDesc {
    std::vector<CompoundShapeChild> children;
};

using ShapeDesc = std::variant<
    BoxShapeDesc,
    SphereShapeDesc,
    CapsuleShapeDesc,
    ConvexHullShapeDesc,
    TriangleMeshShapeDesc,
    HeightFieldShapeDesc,
    CompoundShapeDesc
>;

struct BodyDesc {
    simulation::ParticipantId participant{};
    simulation::MaterialId material{};
    ShapeId shape{};
    MotionType motion = MotionType::dynamic_body;
    math::Vec3 position{};
    math::Quat orientation{};
    math::Vec3 linear_velocity{};
    math::Vec3 angular_velocity{};
    float friction = 0.5F;
    float restitution = 0.0F;
    bool trigger = false;
};

struct BoxBodyDesc {
    simulation::ParticipantId participant{};
    simulation::MaterialId material{};
    MotionType motion = MotionType::dynamic_body;
    math::Vec3 half_extents{0.5F, 0.5F, 0.5F};
    math::Vec3 position{};
    math::Quat orientation{};
    math::Vec3 linear_velocity{};
    math::Vec3 angular_velocity{};
    float friction = 0.5F;
    float restitution = 0.0F;
    bool trigger = false;
};

struct BodyState {
    math::Vec3 position{};
    math::Quat orientation{};
    math::Vec3 linear_velocity{};
    math::Vec3 angular_velocity{};
    bool active = false;
};

struct ContactEvent {
    ContactPhase phase = ContactPhase::began;
    ContactKind kind = ContactKind::collision;
    simulation::ParticipantId first{};
    simulation::ParticipantId second{};
    simulation::MaterialId first_material{};
    simulation::MaterialId second_material{};
    math::Vec3 point{};
    math::Vec3 normal{};
    float penetration_depth = 0.0F;
    float closing_speed = 0.0F;
    float estimated_normal_impulse = 0.0F;
    float combined_friction = 0.0F;
    float combined_restitution = 0.0F;
    std::string material_interaction;
};

struct QueryFilter {
    bool include_static = true;
    bool include_dynamic = true;
    bool include_triggers = true;
    std::optional<simulation::ParticipantId> exclude_participant;
};

struct RaycastDesc {
    math::Vec3 origin{};
    math::Vec3 direction{0.0F, -1.0F, 0.0F};
    float max_distance = 1.0F;
};

struct ShapePose {
    ShapeId shape{};
    math::Vec3 position{};
    math::Quat orientation{};
};

struct SweepDesc {
    ShapePose pose{};
    math::Vec3 direction{0.0F, -1.0F, 0.0F};
    float max_distance = 1.0F;
};

struct QueryHit {
    simulation::ParticipantId participant{};
    simulation::MaterialId material{};
    bool trigger = false;
    math::Vec3 point{};
    math::Vec3 normal{};
    float distance = 0.0F;
    float fraction = 0.0F;
    float penetration_depth = 0.0F;
};

class Domain final : public simulation::Domain {
public:
    [[nodiscard]] static std::unique_ptr<Domain> create(
        simulation::DomainId id,
        std::string name,
        DomainConfig config = {}
    );

    ~Domain() override;

    Domain(const Domain&) = delete;
    Domain& operator=(const Domain&) = delete;
    Domain(Domain&&) = delete;
    Domain& operator=(Domain&&) = delete;

    [[nodiscard]] simulation::DomainId id() const noexcept override;
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] simulation::Fidelity fidelity() const noexcept override;
    void set_fidelity(simulation::Fidelity fidelity) noexcept override;
    void step(
        const simulation::StepContext& context,
        simulation::InteractionEmitter& emitter
    ) override;

    bool register_material(
        simulation::MaterialId id,
        const simulation::Material& material
    );
    bool unregister_material(simulation::MaterialId id);

    bool set_material_interaction(
        simulation::MaterialId first,
        simulation::MaterialId second,
        MaterialInteraction interaction
    );
    bool remove_material_interaction(
        simulation::MaterialId first,
        simulation::MaterialId second
    );

    bool register_shape(ShapeId id, const ShapeDesc& desc);
    bool unregister_shape(ShapeId id);

    bool create_body(const BodyDesc& desc);
    bool create_box(const BoxBodyDesc& desc);
    bool destroy_body(simulation::ParticipantId participant);
    bool set_body_active(simulation::ParticipantId participant, bool active);

    [[nodiscard]] std::optional<BodyState> body_state(
        simulation::ParticipantId participant
    ) const noexcept;

    [[nodiscard]] std::optional<QueryHit> raycast(
        const RaycastDesc& desc,
        const QueryFilter& filter = {}
    ) const;

    [[nodiscard]] std::vector<QueryHit> overlap_shape(
        const ShapePose& pose,
        const QueryFilter& filter = {}
    ) const;

    [[nodiscard]] std::optional<QueryHit> sweep_shape(
        const SweepDesc& desc,
        const QueryFilter& filter = {}
    ) const;

    [[nodiscard]] std::span<const ContactEvent> contact_events() const noexcept;
    [[nodiscard]] std::size_t body_count() const noexcept;
    [[nodiscard]] std::size_t material_count() const noexcept;
    [[nodiscard]] std::size_t material_interaction_count() const noexcept;
    [[nodiscard]] std::size_t shape_count() const noexcept;

private:
    class Impl;

    explicit Domain(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace vice::rigid_body
