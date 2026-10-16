#pragma once

#include <vice/math/math.hpp>
#include <vice/simulation/simulation.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace vice::rigid_body {

enum class MotionType : std::uint8_t {
    static_body,
    dynamic_body,
};

struct DomainConfig {
    std::uint32_t max_bodies = 4096;
    std::uint32_t max_body_pairs = 4096;
    std::uint32_t max_contact_constraints = 2048;
    std::uint32_t temporary_allocator_bytes = 10U * 1024U * 1024U;
    std::uint32_t worker_threads = 0;
};

struct BoxBodyDesc {
    simulation::ParticipantId participant{};
    MotionType motion = MotionType::dynamic_body;
    math::Vec3 half_extents{0.5F, 0.5F, 0.5F};
    math::Vec3 position{};
    math::Quat orientation{};
    math::Vec3 linear_velocity{};
    float friction = 0.5F;
    float restitution = 0.0F;
};

struct BodyState {
    math::Vec3 position{};
    math::Quat orientation{};
    math::Vec3 linear_velocity{};
    bool active = false;
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

    bool create_box(const BoxBodyDesc& desc);
    bool destroy_body(simulation::ParticipantId participant);

    [[nodiscard]] std::optional<BodyState> body_state(
        simulation::ParticipantId participant
    ) const noexcept;

    [[nodiscard]] std::size_t body_count() const noexcept;

private:
    class Impl;

    explicit Domain(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace vice::rigid_body
