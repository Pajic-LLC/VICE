#include <vice/runtime/gpu_virtual_geometry_feedback_ring.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

struct SceneIdentity {
    world::PersistentId entity{};
    world::ResourceId mesh{};

    friend bool operator==(const SceneIdentity&, const SceneIdentity&) = default;
};

[[nodiscard]] std::vector<SceneIdentity> scene_identity(
    const std::span<const RenderMeshInstance> instances
) {
    std::vector<SceneIdentity> result;
    result.reserve(instances.size());
    for (const auto& instance : instances) {
        result.push_back(SceneIdentity{.entity = instance.entity, .mesh = instance.mesh});
    }
    return result;
}

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

} // namespace

RuntimeGpuVirtualGeometryFeedbackResult build_runtime_gpu_virtual_geometry_root_feedback(
    const RuntimeGpuVirtualGeometryPlan& plan
) {
    RuntimeGpuVirtualGeometryFeedbackResult result;
    if (plan.scene_instance_count == 0U || plan.candidates.empty()) return result;
    if (plan.nodes.empty() || plan.mesh_ids.empty()) {
        result.diagnostics = "Virtual geometry bootstrap requires hierarchy nodes and mesh identities.";
        return result;
    }

    std::vector<std::uint8_t> root_found(plan.scene_instance_count, 0U);
    for (const auto& candidate : plan.candidates) {
        if (candidate.scene_instance_index >= plan.scene_instance_count ||
            candidate.node_index >= plan.nodes.size() || candidate.mesh_index >= plan.mesh_ids.size()) {
            result.diagnostics = "Virtual geometry bootstrap encountered an invalid candidate identity.";
            return result;
        }
        const auto& node = plan.nodes[candidate.node_index];
        if (node.mesh_index != candidate.mesh_index ||
            node.local_node_index != candidate.local_node_index) {
            result.diagnostics = "Virtual geometry bootstrap candidate does not match its hierarchy node.";
            return result;
        }
        if (node.parent_index != render::virtual_geometry_invalid_node) continue;
        if (root_found[candidate.scene_instance_index] != 0U) {
            result.diagnostics = "Virtual geometry bootstrap found multiple roots for one scene instance.";
            return result;
        }
        root_found[candidate.scene_instance_index] = 1U;
        result.node_requests.push_back(RuntimeGpuVirtualGeometryNodeRequest{
            .scene_instance_index = candidate.scene_instance_index,
            .mesh_index = candidate.mesh_index,
            .node_index = candidate.local_node_index,
        });
        if (node.child_count == 0U) {
            if (node.page_count != 1U) {
                result.diagnostics = "Virtual geometry bootstrap root leaf does not map to exactly one page.";
                return result;
            }
            result.page_requests.push_back(RuntimeGpuVirtualGeometryPageRequest{
                .scene_instance_index = candidate.scene_instance_index,
                .mesh_index = candidate.mesh_index,
                .node_index = candidate.local_node_index,
                .page_index = node.page_offset,
            });
        }
    }

    return validate_runtime_gpu_virtual_geometry_feedback(
        result.node_requests,
        result.page_requests,
        plan
    );
}

struct RuntimeGpuVirtualGeometryFeedbackRing::State {
    struct Slot {
        RuntimeGpuVirtualGeometryFeedback feedback;
        RuntimeGpuVirtualGeometryPlan plan;
        std::vector<SceneIdentity> identities;
        render::SubmissionId submission = render::invalid_submission;
        std::uint64_t serial = 0U;
        bool reserved = false;
        bool captured = false;
    };

    render::Device* device = nullptr;
    std::array<Slot, runtime_gpu_virtual_geometry_feedback_lane_count> slots{};
    std::uint64_t next_serial = 1U;
};

RuntimeGpuVirtualGeometryFeedbackRing::RuntimeGpuVirtualGeometryFeedbackRing()
    : state_{std::make_unique<State>()} {}
RuntimeGpuVirtualGeometryFeedbackRing::~RuntimeGpuVirtualGeometryFeedbackRing() = default;
RuntimeGpuVirtualGeometryFeedbackRing::RuntimeGpuVirtualGeometryFeedbackRing(
    RuntimeGpuVirtualGeometryFeedbackRing&&
) noexcept = default;
RuntimeGpuVirtualGeometryFeedbackRing& RuntimeGpuVirtualGeometryFeedbackRing::operator=(
    RuntimeGpuVirtualGeometryFeedbackRing&&
) noexcept = default;

bool RuntimeGpuVirtualGeometryFeedbackRing::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU virtual geometry feedback ring state is unavailable.");
        return false;
    }
    for (auto& slot : state_->slots) {
        if (!slot.feedback.initialize(device, diagnostics)) return false;
        slot.plan = {};
        slot.identities.clear();
        slot.submission = render::invalid_submission;
        slot.serial = 0U;
        slot.reserved = false;
        slot.captured = false;
    }
    state_->device = &device;
    state_->next_serial = 1U;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeGpuVirtualGeometryFeedbackPollResult RuntimeGpuVirtualGeometryFeedbackRing::poll(
    const RuntimeGpuVirtualGeometryPlan& current_plan,
    const std::span<const RenderMeshInstance> current_instances
) {
    RuntimeGpuVirtualGeometryFeedbackPollResult result;
    if (state_ == nullptr || state_->device == nullptr) {
        result.diagnostics = "Runtime GPU virtual geometry feedback ring is not initialized.";
        return result;
    }
    if (current_instances.size() != current_plan.scene_instance_count) {
        result.diagnostics = "Runtime GPU virtual geometry feedback ring received incompatible scene identity input.";
        return result;
    }

    const auto identities = scene_identity(current_instances);
    const render::SubmissionId completed = state_->device->completed_submission();
    std::size_t selected_slot = state_->slots.size();
    std::uint64_t selected_serial = 0U;
    for (std::size_t index = 0U; index < state_->slots.size(); ++index) {
        const auto& slot = state_->slots[index];
        if (slot.reserved || !slot.captured || slot.submission == render::invalid_submission ||
            slot.submission > completed) {
            continue;
        }
        if (slot.plan == current_plan && slot.identities == identities && slot.serial > selected_serial) {
            selected_slot = index;
            selected_serial = slot.serial;
        }
    }

    if (selected_slot < state_->slots.size()) {
        auto feedback = state_->slots[selected_slot].feedback.read(current_plan);
        if (!feedback) {
            result.diagnostics = feedback.diagnostics;
        } else {
            result.feedback = std::move(feedback);
            result.serial = selected_serial;
        }
    }

    for (auto& slot : state_->slots) {
        if (!slot.reserved && slot.submission != render::invalid_submission &&
            slot.submission <= completed) {
            slot.plan = {};
            slot.identities.clear();
            slot.submission = render::invalid_submission;
            slot.serial = 0U;
            slot.captured = false;
        }
    }
    return result;
}

RuntimeGpuVirtualGeometryFeedbackAcquireResult RuntimeGpuVirtualGeometryFeedbackRing::acquire(
    const RuntimeGpuVirtualGeometryPlan& plan,
    const std::span<const RenderMeshInstance> instances
) {
    RuntimeGpuVirtualGeometryFeedbackAcquireResult result;
    if (state_ == nullptr || state_->device == nullptr) {
        result.diagnostics = "Runtime GPU virtual geometry feedback ring is not initialized.";
        return result;
    }
    if (instances.size() != plan.scene_instance_count) {
        result.diagnostics = "Runtime GPU virtual geometry feedback acquisition received incompatible scene identity input.";
        return result;
    }
    if (plan.candidates.empty()) return result;

    std::size_t slot_index = state_->slots.size();
    for (std::size_t index = 0U; index < state_->slots.size(); ++index) {
        const auto& slot = state_->slots[index];
        if (!slot.reserved && slot.submission == render::invalid_submission) {
            slot_index = index;
            break;
        }
    }
    if (slot_index == state_->slots.size()) return result;
    if (state_->next_serial == std::numeric_limits<std::uint64_t>::max()) {
        result.diagnostics = "Runtime GPU virtual geometry feedback serial overflowed.";
        return result;
    }

    auto& slot = state_->slots[slot_index];
    slot.plan = plan;
    slot.identities = scene_identity(instances);
    slot.serial = state_->next_serial++;
    slot.reserved = true;
    slot.captured = false;
    result.ticket = RuntimeGpuVirtualGeometryFeedbackTicket{
        .slot = static_cast<std::uint32_t>(slot_index),
        .serial = slot.serial,
    };
    return result;
}

bool RuntimeGpuVirtualGeometryFeedbackRing::capture(
    render::CommandList& commands,
    const RuntimeGpuVirtualGeometrySelectionResult& selection,
    const RuntimeGpuVirtualGeometryFeedbackTicket ticket,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr || !ticket.valid() ||
        ticket.slot >= state_->slots.size()) {
        set_diagnostics(diagnostics, "Runtime GPU virtual geometry feedback capture received an invalid ticket.");
        return false;
    }
    auto& slot = state_->slots[ticket.slot];
    if (!slot.reserved || slot.serial != ticket.serial || slot.captured ||
        slot.submission != render::invalid_submission) {
        set_diagnostics(diagnostics, "Runtime GPU virtual geometry feedback capture ticket is stale or already used.");
        return false;
    }
    if (!slot.feedback.capture(commands, selection, diagnostics)) return false;
    slot.captured = true;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuVirtualGeometryFeedbackRing::commit(
    const RuntimeGpuVirtualGeometryFeedbackTicket ticket,
    const render::SubmissionId submission,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr || !ticket.valid() ||
        ticket.slot >= state_->slots.size() || submission == render::invalid_submission) {
        set_diagnostics(diagnostics, "Runtime GPU virtual geometry feedback commit received an invalid ticket.");
        return false;
    }
    auto& slot = state_->slots[ticket.slot];
    if (!slot.reserved || !slot.captured || slot.serial != ticket.serial ||
        slot.submission != render::invalid_submission) {
        set_diagnostics(diagnostics, "Runtime GPU virtual geometry feedback ticket is stale or incomplete.");
        return false;
    }
    slot.submission = submission;
    slot.reserved = false;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

void RuntimeGpuVirtualGeometryFeedbackRing::cancel(
    const RuntimeGpuVirtualGeometryFeedbackTicket ticket
) noexcept {
    if (state_ == nullptr || !ticket.valid() || ticket.slot >= state_->slots.size()) return;
    auto& slot = state_->slots[ticket.slot];
    if (!slot.reserved || slot.serial != ticket.serial ||
        slot.submission != render::invalid_submission) {
        return;
    }
    slot.plan = {};
    slot.identities.clear();
    slot.serial = 0U;
    slot.reserved = false;
    slot.captured = false;
}

std::uint32_t RuntimeGpuVirtualGeometryFeedbackRing::in_flight_count() const noexcept {
    if (state_ == nullptr) return 0U;
    std::uint32_t count = 0U;
    for (const auto& slot : state_->slots) {
        if (slot.reserved || slot.submission != render::invalid_submission) ++count;
    }
    return count;
}

} // namespace vice::runtime
