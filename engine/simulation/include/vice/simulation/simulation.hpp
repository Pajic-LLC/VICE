#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace vice::simulation {

struct DomainId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return value != 0;
    }

    constexpr auto operator<=>(const DomainId&) const noexcept = default;
};

enum class Fidelity : std::uint8_t {
    off,
    reduced,
    full,
};

using MaterialPropertyValue = std::variant<bool, std::int64_t, double, std::string>;

struct MaterialProperty {
    std::string owner;
    std::string name;
    MaterialPropertyValue value;
};

class Material {
public:
    bool set_property(
        std::string owner,
        std::string name,
        MaterialPropertyValue value
    );

    [[nodiscard]] const MaterialProperty* find_property(
        std::string_view owner,
        std::string_view name
    ) const noexcept;

    bool remove_property(std::string_view owner, std::string_view name) noexcept;

    [[nodiscard]] std::span<const MaterialProperty> properties() const noexcept {
        return properties_;
    }

private:
    std::vector<MaterialProperty> properties_;
};

struct InteractionEvent {
    DomainId source{};
    std::optional<DomainId> target;
    std::string kind;
    std::vector<std::byte> payload;
};

class InteractionEmitter {
public:
    virtual ~InteractionEmitter() = default;

    virtual bool emit(
        std::optional<DomainId> target,
        std::string kind,
        std::vector<std::byte> payload = {}
    ) = 0;
};

struct StepContext {
    std::uint64_t step_index = 0;
    double fixed_delta_seconds = 1.0 / 60.0;
    std::span<const InteractionEvent> incoming;
};

class Domain {
public:
    virtual ~Domain() = default;

    [[nodiscard]] virtual DomainId id() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual Fidelity fidelity() const noexcept = 0;
    virtual void set_fidelity(Fidelity fidelity) noexcept = 0;
    virtual bool step(const StepContext& context, InteractionEmitter& emitter) = 0;
};

struct DomainDebugSnapshot {
    DomainId id{};
    std::string name;
    Fidelity fidelity = Fidelity::off;
    std::uint64_t completed_steps = 0;
    std::uint64_t received_interactions = 0;
    std::uint64_t emitted_interactions = 0;
};

class Coordinator {
public:
    Coordinator() = default;
    explicit Coordinator(double fixed_delta_seconds) noexcept;

    bool register_domain(Domain& domain) noexcept;
    bool unregister_domain(DomainId id) noexcept;

    bool set_fixed_delta_seconds(double seconds) noexcept;
    [[nodiscard]] double fixed_delta_seconds() const noexcept {
        return fixed_delta_seconds_;
    }

    [[nodiscard]] std::uint64_t step_index() const noexcept {
        return step_index_;
    }

    bool step();

    [[nodiscard]] std::vector<DomainDebugSnapshot> debug_snapshots() const;

private:
    struct DomainEntry {
        Domain* domain = nullptr;
        std::uint64_t completed_steps = 0;
        std::uint64_t received_interactions = 0;
        std::uint64_t emitted_interactions = 0;
    };

    [[nodiscard]] bool contains_domain(DomainId id) const noexcept;

    double fixed_delta_seconds_ = 1.0 / 60.0;
    std::uint64_t step_index_ = 0;
    std::vector<DomainEntry> domains_;
    std::vector<InteractionEvent> pending_interactions_;
};

} // namespace vice::simulation
