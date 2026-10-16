#pragma once

#include <vice/math/math.hpp>
#include <vice/simulation/simulation.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace vice::rigid_body {

inline constexpr std::string_view material_property_owner = "rigid_body";
inline constexpr std::string_view material_friction_property = "friction";
inline constexpr std::string_view material_restitution_property = "restitution";

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

    bool create_box(const BoxBodyDesc& desc);
    bool destroy_body(simulation::ParticipantId participant);
    bool set_body_active(simulation::ParticipantId participant, bool active);

    [[nodiscard]] std::optional<BodyState> body_state(
        simulation::ParticipantId participant
    ) const noexcept;

    [[nodiscard]] std::span<const ContactEvent> contact_events() const noexcept;
    [[nodiscard]] std::size_t body_count() const noexcept;
    [[nodiscard]] std::size_t material_count() const noexcept;
    [[nodiscard]] std::size_t material_interaction_count() const noexcept;

private:
    class Impl;

    explicit Domain(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace vice::rigid_body
