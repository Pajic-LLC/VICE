#include <vice/simulation/simulation.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace vice::simulation {

bool Material::set_property(
    std::string owner,
    std::string name,
    MaterialPropertyValue value
) {
    if (owner.empty() || name.empty()) {
        return false;
    }

    const auto existing = std::find_if(
        properties_.begin(),
        properties_.end(),
        [&](const MaterialProperty& property) {
            return property.owner == owner && property.name == name;
        }
    );
    if (existing != properties_.end()) {
        existing->value = std::move(value);
        return true;
    }

    properties_.push_back(MaterialProperty{
        .owner = std::move(owner),
        .name = std::move(name),
        .value = std::move(value),
    });
    return true;
}

const MaterialProperty* Material::find_property(
    const std::string_view owner,
    const std::string_view name
) const noexcept {
    const auto found = std::find_if(
        properties_.begin(),
        properties_.end(),
        [&](const MaterialProperty& property) {
            return property.owner == owner && property.name == name;
        }
    );
    return found != properties_.end() ? &*found : nullptr;
}

bool Material::remove_property(
    const std::string_view owner,
    const std::string_view name
) noexcept {
    const auto found = std::find_if(
        properties_.begin(),
        properties_.end(),
        [&](const MaterialProperty& property) {
            return property.owner == owner && property.name == name;
        }
    );
    if (found == properties_.end()) {
        return false;
    }
    properties_.erase(found);
    return true;
}

Coordinator::Coordinator(const double fixed_delta_seconds) noexcept {
    (void)set_fixed_delta_seconds(fixed_delta_seconds);
}

bool Coordinator::register_domain(Domain& domain) {
    const DomainId id = domain.id();
    if (!id || domain.name().empty() || contains_domain(id)) {
        return false;
    }

    domains_.push_back(DomainEntry{.domain = &domain});
    return true;
}

bool Coordinator::unregister_domain(const DomainId id) noexcept {
    const auto found = std::find_if(
        domains_.begin(),
        domains_.end(),
        [&](const DomainEntry& entry) {
            return entry.domain != nullptr && entry.domain->id() == id;
        }
    );
    if (found == domains_.end()) {
        return false;
    }

    domains_.erase(found);
    std::erase_if(
        pending_interactions_,
        [&](const InteractionEvent& event) {
            return event.source == id || (event.target.has_value() && *event.target == id);
        }
    );
    return true;
}

bool Coordinator::set_fixed_delta_seconds(const double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return false;
    }
    fixed_delta_seconds_ = seconds;
    return true;
}

void Coordinator::step() {
    const auto started = std::chrono::steady_clock::now();
    std::vector<InteractionEvent> emitted_interactions;

    for (auto& entry : domains_) {
        if (entry.domain == nullptr || entry.domain->fidelity() == Fidelity::off) {
            continue;
        }

        const DomainId domain_id = entry.domain->id();
        std::vector<InteractionEvent> incoming;
        for (const auto& interaction : pending_interactions_) {
            if (!interaction.target.has_value() || *interaction.target == domain_id) {
                incoming.push_back(interaction);
            }
        }

        std::uint64_t emitted_count = 0;
        class StepEmitter final : public InteractionEmitter {
        public:
            StepEmitter(
                Coordinator& coordinator,
                const DomainId source,
                std::vector<InteractionEvent>& output,
                std::uint64_t& emitted_count
            ) : coordinator_{coordinator}, source_{source}, output_{output},
                emitted_count_{emitted_count} {}

            bool emit(
                std::optional<DomainId> target,
                std::string kind,
                std::vector<std::byte> payload
            ) override {
                if (kind.empty() || (target.has_value() && !coordinator_.contains_domain(*target))) {
                    return false;
                }
                output_.push_back(InteractionEvent{
                    .source = source_,
                    .target = target,
                    .kind = std::move(kind),
                    .payload = std::move(payload),
                });
                ++emitted_count_;
                return true;
            }

        private:
            Coordinator& coordinator_;
            DomainId source_{};
            std::vector<InteractionEvent>& output_;
            std::uint64_t& emitted_count_;
        } emitter{*this, domain_id, emitted_interactions, emitted_count};

        const StepContext context{
            .step_index = step_index_,
            .fixed_delta_seconds = fixed_delta_seconds_,
            .incoming = incoming,
        };
        entry.domain->step(context, emitter);

        ++entry.completed_steps;
        entry.received_interactions += incoming.size();
        entry.emitted_interactions += emitted_count;
    }

    pending_interactions_ = std::move(emitted_interactions);
    ++step_index_;

    const auto finished = std::chrono::steady_clock::now();
    last_step_wall_seconds_ = std::chrono::duration<double>(finished - started).count();
    recent_step_wall_seconds_[recent_step_cursor_] = last_step_wall_seconds_;
    recent_step_cursor_ = (recent_step_cursor_ + 1U) % timing_history_capacity;
    recent_step_count_ = std::min(recent_step_count_ + 1U, timing_history_capacity);
}

std::vector<DomainDebugSnapshot> Coordinator::debug_snapshots() const {
    std::vector<DomainDebugSnapshot> snapshots;
    snapshots.reserve(domains_.size());
    for (const auto& entry : domains_) {
        if (entry.domain == nullptr) {
            continue;
        }
        snapshots.push_back(DomainDebugSnapshot{
            .id = entry.domain->id(),
            .name = std::string{entry.domain->name()},
            .fidelity = entry.domain->fidelity(),
            .completed_steps = entry.completed_steps,
            .received_interactions = entry.received_interactions,
            .emitted_interactions = entry.emitted_interactions,
        });
    }
    return snapshots;
}

CoordinatorStats Coordinator::stats() const noexcept {
    CoordinatorStats result;
    result.fixed_delta_seconds = fixed_delta_seconds_;
    result.step_index = step_index_;
    result.last_step_wall_seconds = last_step_wall_seconds_;

    double total_step_seconds = 0.0;
    for (std::size_t index = 0; index < recent_step_count_; ++index) {
        total_step_seconds += recent_step_wall_seconds_[index];
    }
    if (recent_step_count_ > 0U) {
        result.recent_step_average_seconds = total_step_seconds / static_cast<double>(recent_step_count_);
    }

    for (const auto& entry : domains_) {
        if (entry.domain == nullptr) continue;
        ++result.registered_domains;
        switch (entry.domain->fidelity()) {
        case Fidelity::full: ++result.full_domains; break;
        case Fidelity::reduced: ++result.reduced_domains; break;
        case Fidelity::off: ++result.off_domains; break;
        }
    }
    return result;
}

bool Coordinator::contains_domain(const DomainId id) const noexcept {
    return id && std::any_of(
        domains_.begin(),
        domains_.end(),
        [&](const DomainEntry& entry) {
            return entry.domain != nullptr && entry.domain->id() == id;
        }
    );
}

} // namespace vice::simulation
