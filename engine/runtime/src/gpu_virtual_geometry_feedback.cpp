#include <vice/runtime/gpu_virtual_geometry_feedback.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] bool node_request_less(
    const RuntimeGpuVirtualGeometryNodeRequest& left,
    const RuntimeGpuVirtualGeometryNodeRequest& right
) noexcept {
    return std::tie(left.scene_instance_index, left.mesh_index, left.node_index) <
        std::tie(right.scene_instance_index, right.mesh_index, right.node_index);
}

[[nodiscard]] bool page_request_less(
    const RuntimeGpuVirtualGeometryPageRequest& left,
    const RuntimeGpuVirtualGeometryPageRequest& right
) noexcept {
    return std::tie(left.scene_instance_index, left.mesh_index, left.node_index, left.page_index) <
        std::tie(right.scene_instance_index, right.mesh_index, right.node_index, right.page_index);
}

struct PlanNodeLookup {
    const RuntimeGpuVirtualGeometryNodeRecord* node = nullptr;
    std::uint32_t global_index = render::virtual_geometry_invalid_node;
};

[[nodiscard]] PlanNodeLookup find_plan_node(
    const RuntimeGpuVirtualGeometryPlan& plan,
    const std::uint32_t mesh_index,
    const std::uint32_t local_node_index
) noexcept {
    for (std::size_t index = 0U; index < plan.nodes.size(); ++index) {
        const auto& node = plan.nodes[index];
        if (node.mesh_index == mesh_index && node.local_node_index == local_node_index) {
            if (index > std::numeric_limits<std::uint32_t>::max()) return {};
            return PlanNodeLookup{
                .node = &node,
                .global_index = static_cast<std::uint32_t>(index),
            };
        }
    }
    return {};
}

[[nodiscard]] bool has_node_request(
    const std::span<const RuntimeGpuVirtualGeometryNodeRequest> requests,
    const std::uint32_t scene_instance_index,
    const std::uint32_t mesh_index,
    const std::uint32_t node_index
) noexcept {
    const RuntimeGpuVirtualGeometryNodeRequest key{
        .scene_instance_index = scene_instance_index,
        .mesh_index = mesh_index,
        .node_index = node_index,
    };
    return std::binary_search(requests.begin(), requests.end(), key, node_request_less);
}

[[nodiscard]] bool has_page_request(
    const std::span<const RuntimeGpuVirtualGeometryPageRequest> requests,
    const std::uint32_t scene_instance_index,
    const std::uint32_t mesh_index,
    const std::uint32_t node_index,
    const std::uint32_t page_index
) noexcept {
    const RuntimeGpuVirtualGeometryPageRequest key{
        .scene_instance_index = scene_instance_index,
        .mesh_index = mesh_index,
        .node_index = node_index,
        .page_index = page_index,
    };
    return std::binary_search(requests.begin(), requests.end(), key, page_request_less);
}

[[nodiscard]] std::unique_ptr<render::Buffer> make_readback(
    render::Device& device,
    const std::uint64_t size,
    const char* const debug_name
) {
    render::BufferDesc desc;
    desc.size = size;
    desc.usage = render::BufferUsage::copy_destination;
    desc.memory = render::MemoryLocation::gpu_to_cpu;
    desc.debug_name = debug_name;
    return device.create_buffer(desc);
}

} // namespace

RuntimeGpuVirtualGeometryFeedbackResult validate_runtime_gpu_virtual_geometry_feedback(
    const std::span<const RuntimeGpuVirtualGeometryNodeRequest> node_requests,
    const std::span<const RuntimeGpuVirtualGeometryPageRequest> page_requests,
    const RuntimeGpuVirtualGeometryPlan& plan
) {
    RuntimeGpuVirtualGeometryFeedbackResult result;
    result.node_requests.assign(node_requests.begin(), node_requests.end());
    result.page_requests.assign(page_requests.begin(), page_requests.end());
    std::sort(result.node_requests.begin(), result.node_requests.end(), node_request_less);
    std::sort(result.page_requests.begin(), result.page_requests.end(), page_request_less);

    for (std::size_t index = 0U; index < result.node_requests.size(); ++index) {
        const auto& request = result.node_requests[index];
        if (request.reserved != 0U || request.scene_instance_index >= plan.scene_instance_count ||
            request.mesh_index >= plan.mesh_ids.size()) {
            result.diagnostics = "GPU virtual geometry feedback contains an invalid node request identity.";
            return result;
        }
        const auto lookup = find_plan_node(plan, request.mesh_index, request.node_index);
        if (lookup.node == nullptr || lookup.node->mesh_index != request.mesh_index ||
            lookup.node->local_node_index != request.node_index) {
            result.diagnostics = "GPU virtual geometry feedback references an unknown hierarchy node.";
            return result;
        }
        if (index > 0U && !node_request_less(result.node_requests[index - 1U], request)) {
            result.diagnostics = "GPU virtual geometry feedback contains a duplicate selected node.";
            return result;
        }
    }

    for (std::size_t index = 0U; index < result.page_requests.size(); ++index) {
        const auto& request = result.page_requests[index];
        if (request.scene_instance_index >= plan.scene_instance_count ||
            request.mesh_index >= plan.mesh_ids.size()) {
            result.diagnostics = "GPU virtual geometry feedback contains an invalid page request identity.";
            return result;
        }
        const auto lookup = find_plan_node(plan, request.mesh_index, request.node_index);
        if (lookup.node == nullptr || lookup.node->child_count != 0U ||
            lookup.node->page_count != 1U || lookup.node->page_offset != request.page_index) {
            result.diagnostics = "GPU virtual geometry feedback page request does not reference its canonical leaf.";
            return result;
        }
        if (!has_node_request(
                result.node_requests,
                request.scene_instance_index,
                request.mesh_index,
                request.node_index
            )) {
            result.diagnostics = "GPU virtual geometry feedback requested a page for an unselected leaf node.";
            return result;
        }
        if (index > 0U && !page_request_less(result.page_requests[index - 1U], request)) {
            result.diagnostics = "GPU virtual geometry feedback contains a duplicate page request.";
            return result;
        }
    }

    for (const auto& request : result.node_requests) {
        const auto lookup = find_plan_node(plan, request.mesh_index, request.node_index);
        if (lookup.node == nullptr) {
            result.diagnostics = "GPU virtual geometry feedback could not resolve a selected node.";
            return result;
        }
        if (lookup.node->child_count == 0U &&
            !has_page_request(
                result.page_requests,
                request.scene_instance_index,
                request.mesh_index,
                request.node_index,
                lookup.node->page_offset
            )) {
            result.diagnostics = "GPU virtual geometry feedback selected a leaf without requesting its page.";
            return result;
        }

        std::uint32_t parent_index = lookup.node->parent_index;
        for (std::size_t step = 0U;
             step < plan.nodes.size() && parent_index != render::virtual_geometry_invalid_node;
             ++step) {
            if (parent_index >= plan.nodes.size()) {
                result.diagnostics = "GPU virtual geometry feedback encountered an invalid hierarchy parent.";
                return result;
            }
            const auto& parent = plan.nodes[parent_index];
            if (parent.mesh_index != request.mesh_index) {
                result.diagnostics = "GPU virtual geometry feedback crossed mesh ownership in the hierarchy.";
                return result;
            }
            if (has_node_request(
                    result.node_requests,
                    request.scene_instance_index,
                    request.mesh_index,
                    parent.local_node_index
                )) {
                result.diagnostics = "GPU virtual geometry feedback selected both an ancestor and descendant.";
                return result;
            }
            parent_index = parent.parent_index;
        }
        if (parent_index != render::virtual_geometry_invalid_node) {
            result.diagnostics = "GPU virtual geometry feedback hierarchy parent chain did not terminate.";
            return result;
        }
    }

    return result;
}

struct RuntimeGpuVirtualGeometryFeedback::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::Buffer> node_count_readback;
    std::unique_ptr<render::Buffer> page_count_readback;
    std::unique_ptr<render::Buffer> node_requests_readback;
    std::unique_ptr<render::Buffer> page_requests_readback;
    std::uint32_t request_capacity = 0U;

    [[nodiscard]] bool ensure_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (device == nullptr || required == 0U) {
            set_diagnostics(diagnostics, "GPU virtual geometry feedback requires a non-zero request capacity.");
            return false;
        }
        if (required <= request_capacity && node_requests_readback && page_requests_readback) {
            return true;
        }
        const std::uint64_t node_bytes = static_cast<std::uint64_t>(required) *
            sizeof(RuntimeGpuVirtualGeometryNodeRequest);
        const std::uint64_t page_bytes = static_cast<std::uint64_t>(required) *
            sizeof(RuntimeGpuVirtualGeometryPageRequest);
        auto next_nodes = make_readback(
            *device, node_bytes, "VICE Runtime virtual geometry node feedback readback"
        );
        auto next_pages = make_readback(
            *device, page_bytes, "VICE Runtime virtual geometry page feedback readback"
        );
        if (!next_nodes || !next_pages) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate virtual geometry feedback readback buffers.");
            return false;
        }
        node_requests_readback = std::move(next_nodes);
        page_requests_readback = std::move(next_pages);
        request_capacity = required;
        return true;
    }
};

RuntimeGpuVirtualGeometryFeedback::RuntimeGpuVirtualGeometryFeedback()
    : state_{std::make_unique<State>()} {}
RuntimeGpuVirtualGeometryFeedback::~RuntimeGpuVirtualGeometryFeedback() = default;
RuntimeGpuVirtualGeometryFeedback::RuntimeGpuVirtualGeometryFeedback(
    RuntimeGpuVirtualGeometryFeedback&&
) noexcept = default;
RuntimeGpuVirtualGeometryFeedback& RuntimeGpuVirtualGeometryFeedback::operator=(
    RuntimeGpuVirtualGeometryFeedback&&
) noexcept = default;

bool RuntimeGpuVirtualGeometryFeedback::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU virtual geometry feedback state is unavailable.");
        return false;
    }
    auto node_count = make_readback(
        device, sizeof(std::uint32_t), "VICE Runtime virtual geometry node count readback"
    );
    auto page_count = make_readback(
        device, sizeof(std::uint32_t), "VICE Runtime virtual geometry page count readback"
    );
    if (!node_count || !page_count) {
        set_diagnostics(diagnostics, "VICE Runtime could not allocate virtual geometry feedback counters.");
        return false;
    }
    state_->device = &device;
    state_->node_count_readback = std::move(node_count);
    state_->page_count_readback = std::move(page_count);
    state_->node_requests_readback.reset();
    state_->page_requests_readback.reset();
    state_->request_capacity = 0U;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuVirtualGeometryFeedback::capture(
    render::CommandList& commands,
    const RuntimeGpuVirtualGeometrySelectionResult& selection,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr || !state_->node_count_readback ||
        !state_->page_count_readback || selection.node_request_count == nullptr ||
        selection.node_requests == nullptr || selection.page_request_count == nullptr ||
        selection.page_requests == nullptr || selection.request_capacity == 0U) {
        set_diagnostics(diagnostics, "Runtime GPU virtual geometry feedback received an invalid selection result.");
        return false;
    }
    if (!state_->ensure_capacity(selection.request_capacity, diagnostics)) return false;

    const std::uint64_t node_bytes = static_cast<std::uint64_t>(selection.request_capacity) *
        sizeof(RuntimeGpuVirtualGeometryNodeRequest);
    const std::uint64_t page_bytes = static_cast<std::uint64_t>(selection.request_capacity) *
        sizeof(RuntimeGpuVirtualGeometryPageRequest);
    if (!commands.copy_buffer(
            *selection.node_request_count,
            0U,
            *state_->node_count_readback,
            0U,
            sizeof(std::uint32_t)
        ) || !commands.copy_buffer(
            *selection.page_request_count,
            0U,
            *state_->page_count_readback,
            0U,
            sizeof(std::uint32_t)
        ) || !commands.copy_buffer(
            *selection.node_requests,
            0U,
            *state_->node_requests_readback,
            0U,
            node_bytes
        ) || !commands.copy_buffer(
            *selection.page_requests,
            0U,
            *state_->page_requests_readback,
            0U,
            page_bytes
        )) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode virtual geometry feedback readback copies.");
        return false;
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeGpuVirtualGeometryFeedbackResult RuntimeGpuVirtualGeometryFeedback::read(
    const RuntimeGpuVirtualGeometryPlan& plan
) const {
    RuntimeGpuVirtualGeometryFeedbackResult result;
    if (state_ == nullptr || state_->device == nullptr || !state_->node_count_readback ||
        !state_->page_count_readback || !state_->node_requests_readback ||
        !state_->page_requests_readback || state_->request_capacity == 0U) {
        result.diagnostics = "Runtime GPU virtual geometry feedback has no captured result.";
        return result;
    }

    std::uint32_t node_count = 0U;
    std::uint32_t page_count = 0U;
    if (!state_->node_count_readback->read(
            0U, std::as_writable_bytes(std::span{&node_count, 1})
        ) || !state_->page_count_readback->read(
            0U, std::as_writable_bytes(std::span{&page_count, 1})
        )) {
        result.diagnostics = "VICE Runtime could not read virtual geometry feedback counters.";
        return result;
    }
    if (node_count > state_->request_capacity || page_count > state_->request_capacity) {
        result.diagnostics = "GPU virtual geometry feedback count exceeds the captured request capacity.";
        return result;
    }

    std::vector<RuntimeGpuVirtualGeometryNodeRequest> nodes(node_count);
    std::vector<RuntimeGpuVirtualGeometryPageRequest> pages(page_count);
    if ((node_count != 0U && !state_->node_requests_readback->read(
            0U, std::as_writable_bytes(std::span{nodes})
        )) || (page_count != 0U && !state_->page_requests_readback->read(
            0U, std::as_writable_bytes(std::span{pages})
        ))) {
        result.diagnostics = "VICE Runtime could not read virtual geometry feedback requests.";
        return result;
    }
    return validate_runtime_gpu_virtual_geometry_feedback(nodes, pages, plan);
}

} // namespace vice::runtime
