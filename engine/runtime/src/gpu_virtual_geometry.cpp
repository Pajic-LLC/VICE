#include <vice/runtime/gpu_virtual_geometry.hpp>

#include <vice/runtime/virtual_geometry.hpp>

#include "gpu_virtual_geometry_shaders.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

constexpr std::uint32_t virtual_geometry_group_size = 64U;

struct GpuVirtualGeometryConstants {
    float frustum_planes[6][4]{};
    float projection_scale = 0.0F;
    float max_projected_error_pixels = 0.0F;
    std::uint32_t candidate_count = 0U;
    std::uint32_t node_count = 0U;
    std::uint32_t instance_count = 0U;
    std::uint32_t padding[3]{};
};

static_assert(sizeof(GpuVirtualGeometryConstants) == 128U);

struct PlannedMeshRange {
    world::ResourceId resource{};
    std::uint32_t mesh_index = 0U;
    std::uint32_t first_node = 0U;
    std::uint32_t node_count = 0U;
};

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] std::uint32_t grown_capacity(const std::uint32_t required) noexcept {
    if (required <= 1U) return 1U;
    std::uint32_t capacity = 1U;
    while (capacity < required && capacity <= std::numeric_limits<std::uint32_t>::max() / 2U) {
        capacity *= 2U;
    }
    return std::max(capacity, required);
}

[[nodiscard]] render::BufferViewDesc structured_view(
    const std::uint64_t size,
    const std::uint32_t stride
) noexcept {
    return render::BufferViewDesc{
        .offset = 0U,
        .size = size,
        .structure_stride = stride,
    };
}

[[nodiscard]] const CookedMeshRecord* find_mesh(
    const std::span<const CookedMeshRecord> meshes,
    const world::ResourceId resource
) noexcept {
    const auto found = std::find_if(meshes.begin(), meshes.end(), [=](const auto& mesh) {
        return mesh.resource_id == resource;
    });
    return found == meshes.end() ? nullptr : &*found;
}

[[nodiscard]] const PlannedMeshRange* find_planned_mesh(
    const std::span<const PlannedMeshRange> ranges,
    const world::ResourceId resource
) noexcept {
    const auto found = std::find_if(ranges.begin(), ranges.end(), [=](const auto& range) {
        return range.resource == resource;
    });
    return found == ranges.end() ? nullptr : &*found;
}

[[nodiscard]] bool finite_bounds(const render::MeshletBounds& bounds) noexcept {
    return std::isfinite(bounds.center[0]) && std::isfinite(bounds.center[1]) &&
        std::isfinite(bounds.center[2]) && std::isfinite(bounds.radius) && bounds.radius >= 0.0F;
}

[[nodiscard]] bool contains_bound(
    const render::MeshletBounds& outer,
    const render::MeshletBounds& inner
) noexcept {
    if (!finite_bounds(outer) || !finite_bounds(inner)) return false;
    const double dx = static_cast<double>(inner.center[0]) - outer.center[0];
    const double dy = static_cast<double>(inner.center[1]) - outer.center[1];
    const double dz = static_cast<double>(inner.center[2]) - outer.center[2];
    const double center_distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double tolerance = std::max(1.0e-4, static_cast<double>(outer.radius) * 1.0e-5);
    return std::isfinite(center_distance) &&
        center_distance + static_cast<double>(inner.radius) <=
            static_cast<double>(outer.radius) + tolerance;
}

[[nodiscard]] bool validate_virtual_geometry_asset(
    const render::MeshAsset& asset,
    std::string& diagnostics
) {
    const auto& hierarchy = asset.virtual_geometry_proxies.hierarchy;
    const auto& nodes = hierarchy.nodes;
    const auto& pages = asset.virtual_geometry_pages.pages;
    if (nodes.empty() || hierarchy.root_index != 0U || pages.empty() ||
        nodes.size() > std::numeric_limits<std::uint32_t>::max() ||
        pages.size() > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics = "Runtime GPU virtual geometry requires a non-empty root-first v6 hierarchy and page table.";
        return false;
    }

    const auto& root = nodes.front();
    if (root.parent_index != render::virtual_geometry_invalid_node || root.depth != 0U ||
        root.page_offset != 0U || root.page_count != pages.size()) {
        diagnostics = "Runtime GPU virtual geometry rejected an invalid root/page coverage contract.";
        return false;
    }

    std::vector<std::uint32_t> incoming(nodes.size(), 0U);
    std::vector<std::uint8_t> page_coverage(pages.size(), 0U);
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const auto& node = nodes[index];
        if (!finite_bounds(node.bounds) || !std::isfinite(node.geometric_error) ||
            node.geometric_error < 0.0F || node.page_count == 0U ||
            static_cast<std::uint64_t>(node.page_offset) + node.page_count > pages.size()) {
            diagnostics = "Runtime GPU virtual geometry hierarchy contains invalid bounds, error, or page coverage.";
            return false;
        }
        if (index == 0U) {
            if (node.parent_index != render::virtual_geometry_invalid_node) {
                diagnostics = "Runtime GPU virtual geometry root unexpectedly has a parent.";
                return false;
            }
        } else if (node.parent_index == render::virtual_geometry_invalid_node ||
            node.parent_index >= index || node.parent_index >= nodes.size()) {
            diagnostics = "Runtime GPU virtual geometry hierarchy has a non-canonical parent index.";
            return false;
        }

        if (node.child_count == 0U) {
            if (node.first_child != render::virtual_geometry_invalid_node || node.page_count != 1U) {
                diagnostics = "Runtime GPU virtual geometry leaf contract is invalid.";
                return false;
            }
            if (page_coverage[node.page_offset] != 0U) {
                diagnostics = "Runtime GPU virtual geometry leaves overlap an authoritative page.";
                return false;
            }
            page_coverage[node.page_offset] = 1U;
        } else {
            if (node.child_count > render::virtual_geometry_hierarchy_fanout ||
                node.first_child == render::virtual_geometry_invalid_node ||
                node.first_child <= index ||
                static_cast<std::uint64_t>(node.first_child) + node.child_count > nodes.size()) {
                diagnostics = "Runtime GPU virtual geometry hierarchy has an invalid child range.";
                return false;
            }
            std::uint64_t expected_page_offset = node.page_offset;
            std::uint64_t child_page_count = 0U;
            for (std::uint32_t local = 0U; local < node.child_count; ++local) {
                const std::uint32_t child_index = node.first_child + local;
                const auto& child = nodes[child_index];
                if (child.parent_index != index || child.depth != node.depth + 1U ||
                    child.page_offset != expected_page_offset || !contains_bound(node.bounds, child.bounds)) {
                    diagnostics = "Runtime GPU virtual geometry child connectivity or bounds are invalid.";
                    return false;
                }
                const double tolerance = std::max(
                    1.0e-4,
                    static_cast<double>(node.geometric_error) * 1.0e-5
                );
                if (static_cast<double>(child.geometric_error) >
                    static_cast<double>(node.geometric_error) + tolerance) {
                    diagnostics = "Runtime GPU virtual geometry hierarchy error is not monotonic.";
                    return false;
                }
                ++incoming[child_index];
                expected_page_offset += child.page_count;
                child_page_count += child.page_count;
            }
            if (child_page_count != node.page_count ||
                expected_page_offset != static_cast<std::uint64_t>(node.page_offset) + node.page_count) {
                diagnostics = "Runtime GPU virtual geometry children do not exactly cover their parent page range.";
                return false;
            }
        }

        std::string draw_diagnostics;
        if (!runtime_virtual_geometry_draw_batch(
                asset,
                static_cast<std::uint32_t>(index),
                &draw_diagnostics
            )) {
            diagnostics = "Runtime GPU virtual geometry node is not draw-ready: " + draw_diagnostics;
            return false;
        }
    }

    for (std::size_t index = 1U; index < incoming.size(); ++index) {
        if (incoming[index] != 1U) {
            diagnostics = "Runtime GPU virtual geometry hierarchy contains disconnected or multiply referenced nodes.";
            return false;
        }
    }
    if (std::find(page_coverage.begin(), page_coverage.end(), 0U) != page_coverage.end()) {
        diagnostics = "Runtime GPU virtual geometry leaves do not cover every authoritative page exactly once.";
        return false;
    }
    return true;
}

} // namespace

std::optional<std::uint32_t> RuntimeGpuVirtualGeometryPlan::mesh_index(
    const world::ResourceId resource_id
) const noexcept {
    const auto found = std::find(mesh_ids.begin(), mesh_ids.end(), resource_id);
    if (found == mesh_ids.end()) return std::nullopt;
    return static_cast<std::uint32_t>(std::distance(mesh_ids.begin(), found));
}

RuntimeGpuVirtualGeometryPlanBuildResult build_runtime_gpu_virtual_geometry_plan(
    const std::span<const RenderMeshInstance> instances,
    const std::span<const CookedMeshRecord> meshes
) {
    RuntimeGpuVirtualGeometryPlanBuildResult result;
    if (instances.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime GPU virtual geometry scene instance count exceeds 32-bit indexing.";
        return result;
    }
    result.plan.scene_instance_count = static_cast<std::uint32_t>(instances.size());

    std::vector<PlannedMeshRange> ranges;
    for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
        const auto& instance = instances[instance_index];
        const auto* const mesh = find_mesh(meshes, instance.mesh);
        if (mesh == nullptr) {
            result.plan = {};
            result.diagnostics = "Runtime GPU virtual geometry plan references an unknown mesh `" +
                instance.mesh.value.to_string() + "`.";
            return result;
        }
        const auto& hierarchy = mesh->asset.virtual_geometry_proxies.hierarchy;
        if (hierarchy.nodes.empty()) continue;

        const PlannedMeshRange* range = find_planned_mesh(ranges, instance.mesh);
        if (range == nullptr) {
            std::string validation_diagnostics;
            if (!validate_virtual_geometry_asset(mesh->asset, validation_diagnostics)) {
                result.plan = {};
                result.diagnostics = "Runtime GPU virtual geometry rejected mesh `" +
                    instance.mesh.value.to_string() + "`: " + validation_diagnostics;
                return result;
            }
            if (result.plan.mesh_ids.size() >= std::numeric_limits<std::uint32_t>::max() ||
                result.plan.nodes.size() > std::numeric_limits<std::uint32_t>::max() -
                    hierarchy.nodes.size()) {
                result.plan = {};
                result.diagnostics = "Runtime GPU virtual geometry table exceeds 32-bit indexing.";
                return result;
            }

            const std::uint32_t mesh_index = static_cast<std::uint32_t>(result.plan.mesh_ids.size());
            const std::uint32_t first_node = static_cast<std::uint32_t>(result.plan.nodes.size());
            result.plan.mesh_ids.push_back(instance.mesh);
            result.plan.nodes.reserve(result.plan.nodes.size() + hierarchy.nodes.size());
            for (std::size_t local_index = 0U; local_index < hierarchy.nodes.size(); ++local_index) {
                const auto& node = hierarchy.nodes[local_index];
                RuntimeGpuVirtualGeometryNodeRecord record;
                record.bounds_center_radius[0] = node.bounds.center[0];
                record.bounds_center_radius[1] = node.bounds.center[1];
                record.bounds_center_radius[2] = node.bounds.center[2];
                record.bounds_center_radius[3] = node.bounds.radius;
                record.geometric_error = node.geometric_error;
                record.parent_index = node.parent_index == render::virtual_geometry_invalid_node
                    ? render::virtual_geometry_invalid_node
                    : first_node + node.parent_index;
                record.first_child = node.first_child == render::virtual_geometry_invalid_node
                    ? render::virtual_geometry_invalid_node
                    : first_node + node.first_child;
                record.child_count = node.child_count;
                record.page_offset = node.page_offset;
                record.page_count = node.page_count;
                record.depth = node.depth;
                record.mesh_index = mesh_index;
                record.local_node_index = static_cast<std::uint32_t>(local_index);
                result.plan.nodes.push_back(record);
            }
            ranges.push_back(PlannedMeshRange{
                .resource = instance.mesh,
                .mesh_index = mesh_index,
                .first_node = first_node,
                .node_count = static_cast<std::uint32_t>(hierarchy.nodes.size()),
            });
            range = &ranges.back();
        }

        if (result.plan.candidates.size() > std::numeric_limits<std::uint32_t>::max() -
            range->node_count) {
            result.plan = {};
            result.diagnostics = "Runtime GPU virtual geometry candidate stream exceeds 32-bit indexing.";
            return result;
        }
        result.plan.candidates.reserve(result.plan.candidates.size() + range->node_count);
        for (std::uint32_t local_node = 0U; local_node < range->node_count; ++local_node) {
            result.plan.candidates.push_back(RuntimeGpuVirtualGeometryCandidate{
                .scene_instance_index = static_cast<std::uint32_t>(instance_index),
                .node_index = range->first_node + local_node,
                .mesh_index = range->mesh_index,
                .local_node_index = local_node,
            });
        }
    }
    return result;
}

struct RuntimeGpuVirtualGeometrySelector::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> shader;
    std::unique_ptr<render::ComputePipeline> pipeline;
    std::unique_ptr<render::Buffer> constants;
    std::unique_ptr<render::Buffer> zero_upload;
    std::unique_ptr<render::Buffer> node_request_count;
    std::unique_ptr<render::Buffer> page_request_count;
    std::unique_ptr<render::Buffer> node_upload;
    std::unique_ptr<render::Buffer> nodes;
    std::unique_ptr<render::Buffer> candidate_upload;
    std::unique_ptr<render::Buffer> candidates;
    std::unique_ptr<render::Buffer> node_requests;
    std::unique_ptr<render::Buffer> page_requests;
    std::uint32_t node_capacity = 0U;
    std::uint32_t candidate_capacity = 0U;
    std::uint32_t candidate_count = 0U;

    [[nodiscard]] bool ensure_node_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (required == 0U) return true;
        if (required <= node_capacity && node_upload && nodes) return true;
        if (device == nullptr) return false;
        const std::uint32_t next_capacity = grown_capacity(required);
        const std::uint64_t bytes = static_cast<std::uint64_t>(next_capacity) *
            sizeof(RuntimeGpuVirtualGeometryNodeRecord);

        render::BufferDesc upload_desc;
        upload_desc.size = bytes;
        upload_desc.usage = render::BufferUsage::copy_source;
        upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
        upload_desc.debug_name = "VICE Runtime GPU virtual geometry node upload";
        auto next_upload = device->create_buffer(upload_desc);

        render::BufferDesc storage_desc;
        storage_desc.size = bytes;
        storage_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination |
            render::BufferUsage::copy_source;
        storage_desc.memory = render::MemoryLocation::gpu_only;
        storage_desc.debug_name = "VICE Runtime GPU virtual geometry nodes";
        auto next_nodes = device->create_buffer(storage_desc);
        if (!next_upload || !next_nodes) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate GPU virtual geometry node buffers.");
            return false;
        }
        node_upload = std::move(next_upload);
        nodes = std::move(next_nodes);
        node_capacity = next_capacity;
        return true;
    }

    [[nodiscard]] bool ensure_candidate_capacity(
        const std::uint32_t required,
        std::string* const diagnostics
    ) {
        if (required == 0U) return true;
        if (required <= candidate_capacity && candidate_upload && candidates && node_requests && page_requests) {
            return true;
        }
        if (device == nullptr) return false;
        const std::uint32_t next_capacity = grown_capacity(required);
        const std::uint64_t candidate_bytes = static_cast<std::uint64_t>(next_capacity) *
            sizeof(RuntimeGpuVirtualGeometryCandidate);
        const std::uint64_t request_bytes = static_cast<std::uint64_t>(next_capacity) *
            sizeof(RuntimeGpuVirtualGeometryNodeRequest);

        render::BufferDesc upload_desc;
        upload_desc.size = candidate_bytes;
        upload_desc.usage = render::BufferUsage::copy_source;
        upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
        upload_desc.debug_name = "VICE Runtime GPU virtual geometry candidate upload";
        auto next_upload = device->create_buffer(upload_desc);

        render::BufferDesc candidate_desc;
        candidate_desc.size = candidate_bytes;
        candidate_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination |
            render::BufferUsage::copy_source;
        candidate_desc.memory = render::MemoryLocation::gpu_only;
        candidate_desc.debug_name = "VICE Runtime GPU virtual geometry candidates";
        auto next_candidates = device->create_buffer(candidate_desc);

        render::BufferDesc node_request_desc;
        node_request_desc.size = request_bytes;
        node_request_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_source;
        node_request_desc.memory = render::MemoryLocation::gpu_only;
        node_request_desc.debug_name = "VICE Runtime GPU virtual geometry node requests";
        auto next_node_requests = device->create_buffer(node_request_desc);

        render::BufferDesc page_request_desc;
        page_request_desc.size = static_cast<std::uint64_t>(next_capacity) *
            sizeof(RuntimeGpuVirtualGeometryPageRequest);
        page_request_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_source;
        page_request_desc.memory = render::MemoryLocation::gpu_only;
        page_request_desc.debug_name = "VICE Runtime GPU virtual geometry page requests";
        auto next_page_requests = device->create_buffer(page_request_desc);
        if (!next_upload || !next_candidates || !next_node_requests || !next_page_requests) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate GPU virtual geometry request buffers.");
            return false;
        }

        candidate_upload = std::move(next_upload);
        candidates = std::move(next_candidates);
        node_requests = std::move(next_node_requests);
        page_requests = std::move(next_page_requests);
        candidate_capacity = next_capacity;
        return true;
    }
};

RuntimeGpuVirtualGeometrySelector::RuntimeGpuVirtualGeometrySelector()
    : state_{std::make_unique<State>()} {}
RuntimeGpuVirtualGeometrySelector::~RuntimeGpuVirtualGeometrySelector() = default;
RuntimeGpuVirtualGeometrySelector::RuntimeGpuVirtualGeometrySelector(
    RuntimeGpuVirtualGeometrySelector&&
) noexcept = default;
RuntimeGpuVirtualGeometrySelector& RuntimeGpuVirtualGeometrySelector::operator=(
    RuntimeGpuVirtualGeometrySelector&&
) noexcept = default;

bool RuntimeGpuVirtualGeometrySelector::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU virtual geometry selector state is unavailable.");
        return false;
    }

    const bool metal = device.backend() == render::Backend::metal;
    render::ShaderModuleDesc shader_desc;
    shader_desc.stage = render::ShaderStage::compute;
    shader_desc.language = metal
        ? render::ShaderSourceLanguage::metal
        : render::ShaderSourceLanguage::hlsl;
    shader_desc.source = metal
        ? detail::gpu_virtual_geometry_metal_source
        : detail::gpu_virtual_geometry_hlsl_source;
    shader_desc.resources = detail::gpu_virtual_geometry_resources;
    shader_desc.entry_point = "vice_runtime_virtual_geometry_select_cs";
    shader_desc.debug_name = "VICE Runtime GPU virtual geometry selection compute";
    auto shader = device.create_shader_module(shader_desc);
    if (!shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the GPU virtual geometry selection shader.");
        return false;
    }

    render::ComputePipelineDesc pipeline_desc;
    pipeline_desc.compute_shader = shader.get();
    pipeline_desc.thread_group_size = {virtual_geometry_group_size, 1U, 1U};
    pipeline_desc.debug_name = "VICE Runtime GPU virtual geometry selection pipeline";
    auto pipeline = device.create_compute_pipeline(pipeline_desc);
    if (!pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the GPU virtual geometry selection pipeline.");
        return false;
    }

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(GpuVirtualGeometryConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Runtime GPU virtual geometry constants";
    auto constants = device.create_buffer(constants_desc);

    render::BufferDesc zero_desc;
    zero_desc.size = sizeof(std::uint32_t);
    zero_desc.usage = render::BufferUsage::copy_source;
    zero_desc.memory = render::MemoryLocation::cpu_to_gpu;
    zero_desc.debug_name = "VICE Runtime GPU virtual geometry zero upload";
    auto zero_upload = device.create_buffer(zero_desc);

    render::BufferDesc count_desc;
    count_desc.size = sizeof(std::uint32_t);
    count_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_source |
        render::BufferUsage::copy_destination;
    count_desc.memory = render::MemoryLocation::gpu_only;
    count_desc.debug_name = "VICE Runtime GPU virtual geometry request count";
    auto node_request_count = device.create_buffer(count_desc);
    auto page_request_count = device.create_buffer(count_desc);

    const std::uint32_t zero = 0U;
    if (!constants || !zero_upload || !node_request_count || !page_request_count ||
        !zero_upload->write(0U, std::as_bytes(std::span{&zero, 1}))) {
        set_diagnostics(diagnostics, "VICE Runtime could not create GPU virtual geometry control buffers.");
        return false;
    }

    state_->device = &device;
    state_->shader = std::move(shader);
    state_->pipeline = std::move(pipeline);
    state_->constants = std::move(constants);
    state_->zero_upload = std::move(zero_upload);
    state_->node_request_count = std::move(node_request_count);
    state_->page_request_count = std::move(page_request_count);
    state_->node_upload.reset();
    state_->nodes.reset();
    state_->candidate_upload.reset();
    state_->candidates.reset();
    state_->node_requests.reset();
    state_->page_requests.reset();
    state_->node_capacity = 0U;
    state_->candidate_capacity = 0U;
    state_->candidate_count = 0U;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuVirtualGeometrySelector::select(
    render::CommandList& commands,
    const RuntimeGpuVirtualGeometryPlan& plan,
    const RuntimeGpuInstanceTableResult& camera_relative_instances,
    const RuntimeVisibilityFrustum& frustum,
    const float vertical_fov_radians,
    const float viewport_height_pixels,
    const float max_projected_error_pixels,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr || !state_->pipeline ||
        !state_->constants || !state_->zero_upload || !state_->node_request_count ||
        !state_->page_request_count) {
        set_diagnostics(diagnostics, "Runtime GPU virtual geometry selector is not initialized.");
        return false;
    }
    if (!valid(frustum) || !std::isfinite(vertical_fov_radians) ||
        vertical_fov_radians <= 0.0F || vertical_fov_radians >= std::numbers::pi_v<float> ||
        !std::isfinite(viewport_height_pixels) || viewport_height_pixels <= 0.0F ||
        !std::isfinite(max_projected_error_pixels) || max_projected_error_pixels < 0.0F) {
        set_diagnostics(diagnostics, "Runtime GPU virtual geometry selection rejected an invalid camera contract.");
        return false;
    }
    if (plan.nodes.size() > std::numeric_limits<std::uint32_t>::max() ||
        plan.candidates.size() > std::numeric_limits<std::uint32_t>::max() ||
        plan.scene_instance_count != camera_relative_instances.instance_count) {
        set_diagnostics(diagnostics, "Runtime GPU virtual geometry selection received incompatible plan/table sizes.");
        return false;
    }

    const std::uint32_t node_count = static_cast<std::uint32_t>(plan.nodes.size());
    const std::uint32_t candidate_count = static_cast<std::uint32_t>(plan.candidates.size());
    state_->candidate_count = candidate_count;
    if (!commands.copy_buffer(
            *state_->zero_upload,
            0U,
            *state_->node_request_count,
            0U,
            sizeof(std::uint32_t)
        ) || !commands.copy_buffer(
            *state_->zero_upload,
            0U,
            *state_->page_request_count,
            0U,
            sizeof(std::uint32_t)
        )) {
        set_diagnostics(diagnostics, "VICE Runtime could not reset GPU virtual geometry request counts.");
        return false;
    }
    if (candidate_count == 0U) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (node_count == 0U || camera_relative_instances.models == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU virtual geometry selection requires hierarchy nodes and model transforms.");
        return false;
    }
    for (const auto& candidate : plan.candidates) {
        if (candidate.scene_instance_index >= plan.scene_instance_count ||
            candidate.node_index >= node_count || candidate.mesh_index >= plan.mesh_ids.size()) {
            set_diagnostics(diagnostics, "Runtime GPU virtual geometry selection rejected an out-of-range candidate.");
            return false;
        }
        const auto& node = plan.nodes[candidate.node_index];
        if (node.mesh_index != candidate.mesh_index ||
            node.local_node_index != candidate.local_node_index) {
            set_diagnostics(diagnostics, "Runtime GPU virtual geometry selection rejected inconsistent candidate identity.");
            return false;
        }
    }

    if (!state_->ensure_node_capacity(node_count, diagnostics) ||
        !state_->ensure_candidate_capacity(candidate_count, diagnostics)) {
        return false;
    }

    const double tangent = std::tan(static_cast<double>(vertical_fov_radians) * 0.5);
    const double projection_scale = static_cast<double>(viewport_height_pixels) / (2.0 * tangent);
    if (!std::isfinite(projection_scale) || projection_scale <= 0.0 ||
        projection_scale > std::numeric_limits<float>::max()) {
        set_diagnostics(diagnostics, "Runtime GPU virtual geometry selection projection scale is invalid.");
        return false;
    }

    GpuVirtualGeometryConstants constants{};
    for (std::size_t index = 0U; index < frustum.planes.size(); ++index) {
        constants.frustum_planes[index][0] = frustum.planes[index].normal.x;
        constants.frustum_planes[index][1] = frustum.planes[index].normal.y;
        constants.frustum_planes[index][2] = frustum.planes[index].normal.z;
        constants.frustum_planes[index][3] = frustum.planes[index].distance;
    }
    constants.projection_scale = static_cast<float>(projection_scale);
    constants.max_projected_error_pixels = max_projected_error_pixels;
    constants.candidate_count = candidate_count;
    constants.node_count = node_count;
    constants.instance_count = plan.scene_instance_count;

    const auto node_bytes = std::as_bytes(std::span{plan.nodes});
    const auto candidate_bytes = std::as_bytes(std::span{plan.candidates});
    if (!state_->constants->write(0U, std::as_bytes(std::span{&constants, 1})) ||
        !state_->node_upload->write(0U, node_bytes) ||
        !state_->candidate_upload->write(0U, candidate_bytes) ||
        !commands.copy_buffer(
            *state_->node_upload, 0U, *state_->nodes, 0U, node_bytes.size()
        ) || !commands.copy_buffer(
            *state_->candidate_upload, 0U, *state_->candidates, 0U, candidate_bytes.size()
        )) {
        set_diagnostics(diagnostics, "VICE Runtime could not upload GPU virtual geometry selection data.");
        return false;
    }

    const auto count_view = structured_view(sizeof(std::uint32_t), sizeof(std::uint32_t));
    const auto nodes_view = structured_view(node_bytes.size(), sizeof(RuntimeGpuVirtualGeometryNodeRecord));
    const auto candidates_view = structured_view(
        candidate_bytes.size(), sizeof(RuntimeGpuVirtualGeometryCandidate)
    );
    const auto models_view = structured_view(
        static_cast<std::uint64_t>(plan.scene_instance_count) * sizeof(math::Mat4),
        sizeof(math::Mat4)
    );
    const auto node_requests_view = structured_view(
        static_cast<std::uint64_t>(state_->candidate_capacity) *
            sizeof(RuntimeGpuVirtualGeometryNodeRequest),
        sizeof(RuntimeGpuVirtualGeometryNodeRequest)
    );
    const auto page_requests_view = structured_view(
        static_cast<std::uint64_t>(state_->candidate_capacity) *
            sizeof(RuntimeGpuVirtualGeometryPageRequest),
        sizeof(RuntimeGpuVirtualGeometryPageRequest)
    );

    const std::uint32_t groups =
        (candidate_count + virtual_geometry_group_size - 1U) / virtual_geometry_group_size;
    if (!commands.set_compute_pipeline(*state_->pipeline) ||
        !commands.set_compute_constant_buffer(
            detail::gpu_virtual_geometry_constants_binding,
            *state_->constants,
            0U,
            sizeof(GpuVirtualGeometryConstants)
        ) || !commands.set_compute_storage_buffer(
            detail::gpu_virtual_geometry_nodes_binding,
            *state_->nodes,
            nodes_view
        ) || !commands.set_compute_storage_buffer(
            detail::gpu_virtual_geometry_candidates_binding,
            *state_->candidates,
            candidates_view
        ) || !commands.set_compute_storage_buffer(
            detail::gpu_virtual_geometry_models_binding,
            *camera_relative_instances.models,
            models_view
        ) || !commands.set_compute_storage_buffer(
            detail::gpu_virtual_geometry_node_count_binding,
            *state_->node_request_count,
            count_view
        ) || !commands.set_compute_storage_buffer(
            detail::gpu_virtual_geometry_node_requests_binding,
            *state_->node_requests,
            node_requests_view
        ) || !commands.set_compute_storage_buffer(
            detail::gpu_virtual_geometry_page_count_binding,
            *state_->page_request_count,
            count_view
        ) || !commands.set_compute_storage_buffer(
            detail::gpu_virtual_geometry_page_requests_binding,
            *state_->page_requests,
            page_requests_view
        ) || !commands.dispatch(render::ComputeDispatchDesc{
            .group_count_x = groups,
            .group_count_y = 1U,
            .group_count_z = 1U,
        })) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode GPU virtual geometry selection.");
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeGpuVirtualGeometrySelectionResult RuntimeGpuVirtualGeometrySelector::result() const noexcept {
    if (state_ == nullptr) return {};
    return RuntimeGpuVirtualGeometrySelectionResult{
        .node_request_count = state_->node_request_count.get(),
        .node_requests = state_->node_requests.get(),
        .page_request_count = state_->page_request_count.get(),
        .page_requests = state_->page_requests.get(),
        .request_capacity = state_->candidate_capacity,
        .candidate_count = state_->candidate_count,
    };
}

} // namespace vice::runtime
