#include <vice/runtime/gpu_meshlet_table.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] std::string resource_key(const world::ResourceId resource_id) {
    return resource_id.value.to_string();
}

[[nodiscard]] bool resource_less(
    const world::ResourceId left,
    const world::ResourceId right
) {
    return resource_key(left) < resource_key(right);
}

[[nodiscard]] bool finite_bounds(const render::MeshletBounds& bounds) noexcept {
    return std::isfinite(bounds.center[0]) &&
        std::isfinite(bounds.center[1]) &&
        std::isfinite(bounds.center[2]) &&
        std::isfinite(bounds.radius) && bounds.radius >= 0.0F;
}

[[nodiscard]] bool append_fits_u32(
    const std::size_t current,
    const std::size_t additional
) noexcept {
    constexpr auto maximum = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    return current <= maximum && additional <= maximum - current;
}

[[nodiscard]] std::uint32_t grown_capacity(const std::uint32_t required) noexcept {
    if (required <= 1U) return 1U;
    std::uint32_t capacity = 1U;
    while (capacity < required && capacity <= std::numeric_limits<std::uint32_t>::max() / 2U) {
        capacity *= 2U;
    }
    return std::max(capacity, required);
}

struct GpuStream {
    std::unique_ptr<render::Buffer> upload;
    std::unique_ptr<render::Buffer> gpu;
    std::uint32_t capacity = 0U;
};

[[nodiscard]] bool ensure_stream(
    render::Device& device,
    GpuStream& stream,
    const std::uint32_t required,
    const std::uint64_t stride,
    const std::string_view upload_name,
    const std::string_view gpu_name,
    std::string* const diagnostics
) {
    if (required == 0U) return true;
    if (required <= stream.capacity && stream.upload && stream.gpu) return true;

    const std::uint32_t next_capacity = grown_capacity(required);
    if (stride != 0U && next_capacity > std::numeric_limits<std::uint64_t>::max() / stride) {
        set_diagnostics(diagnostics, "Runtime GPU meshlet table allocation size overflowed.");
        return false;
    }
    const std::uint64_t size = static_cast<std::uint64_t>(next_capacity) * stride;

    render::BufferDesc upload_desc;
    upload_desc.size = size;
    upload_desc.usage = render::BufferUsage::copy_source;
    upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
    upload_desc.debug_name = std::string{upload_name};
    auto next_upload = device.create_buffer(upload_desc);

    render::BufferDesc gpu_desc;
    gpu_desc.size = size;
    gpu_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination |
        render::BufferUsage::copy_source;
    gpu_desc.memory = render::MemoryLocation::gpu_only;
    gpu_desc.debug_name = std::string{gpu_name};
    auto next_gpu = device.create_buffer(gpu_desc);

    if (!next_upload || !next_gpu) {
        set_diagnostics(diagnostics, "VICE Runtime could not allocate GPU meshlet table buffers.");
        return false;
    }

    stream.upload = std::move(next_upload);
    stream.gpu = std::move(next_gpu);
    stream.capacity = next_capacity;
    return true;
}

template <typename T>
[[nodiscard]] bool upload_stream(
    render::Device& device,
    render::CommandList& commands,
    GpuStream& stream,
    const std::span<const T> values,
    const std::string_view upload_name,
    const std::string_view gpu_name,
    std::string* const diagnostics
) {
    if (values.empty()) return true;
    if (values.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(diagnostics, "Runtime GPU meshlet stream exceeds the 32-bit count contract.");
        return false;
    }
    const auto count = static_cast<std::uint32_t>(values.size());
    if (!ensure_stream(
            device,
            stream,
            count,
            sizeof(T),
            upload_name,
            gpu_name,
            diagnostics
        )) {
        return false;
    }
    const auto bytes = std::as_bytes(values);
    if (!stream.upload->write(0U, bytes) ||
        !commands.copy_buffer(*stream.upload, 0U, *stream.gpu, 0U, bytes.size())) {
        set_diagnostics(diagnostics, "VICE Runtime could not upload GPU meshlet table data.");
        return false;
    }
    return true;
}

} // namespace

std::optional<std::uint32_t> RuntimeGpuMeshletTablePlan::mesh_index(
    const world::ResourceId resource_id
) const noexcept {
    const auto found = std::lower_bound(mesh_ids.begin(), mesh_ids.end(), resource_id, resource_less);
    if (found == mesh_ids.end() || *found != resource_id) return std::nullopt;
    return static_cast<std::uint32_t>(std::distance(mesh_ids.begin(), found));
}

RuntimeGpuMeshletTableBuildResult build_runtime_gpu_meshlet_table_plan(
    const std::span<const CookedMeshRecord> meshes
) {
    RuntimeGpuMeshletTableBuildResult result;
    if (meshes.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime GPU mesh table exceeds the 32-bit mesh index contract.";
        return result;
    }

    std::vector<const CookedMeshRecord*> ordered;
    ordered.reserve(meshes.size());
    for (const auto& mesh : meshes) {
        if (!mesh.resource_id.valid()) {
            result.diagnostics = "Runtime GPU meshlet table rejected a mesh without a stable ID.";
            return result;
        }
        ordered.push_back(&mesh);
    }
    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const auto* left, const auto* right) {
            return resource_less(left->resource_id, right->resource_id);
        }
    );
    for (std::size_t index = 1U; index < ordered.size(); ++index) {
        if (ordered[index - 1U]->resource_id == ordered[index]->resource_id) {
            result.diagnostics = "Runtime GPU meshlet table rejected duplicate stable mesh IDs.";
            return result;
        }
    }

    result.plan.meshes.reserve(ordered.size());
    result.plan.mesh_ids.reserve(ordered.size());
    for (std::size_t mesh_index = 0U; mesh_index < ordered.size(); ++mesh_index) {
        const auto& cooked = *ordered[mesh_index];
        if (cooked.asset.version < render::mesh_asset_min_supported_version ||
            cooked.asset.version > render::mesh_asset_format_version) {
            result.diagnostics = "Runtime GPU meshlet table rejected an unsupported cooked mesh version.";
            return result;
        }

        RuntimeGpuMeshRecord mesh_record;
        mesh_record.first_meshlet = static_cast<std::uint32_t>(result.plan.meshlets.size());
        result.plan.mesh_ids.push_back(cooked.resource_id);

        if (cooked.asset.version < render::mesh_asset_format_version_v4) {
            result.plan.meshes.push_back(mesh_record);
            continue;
        }

        const auto& geometry = cooked.asset.meshlet_geometry;
        if (geometry.meshlets.empty() || geometry.vertex_indices.empty() ||
            geometry.triangle_indices.empty()) {
            result.diagnostics = "Runtime GPU meshlet table rejected empty v4 cluster metadata.";
            return result;
        }
        if (!append_fits_u32(result.plan.meshlets.size(), geometry.meshlets.size()) ||
            !append_fits_u32(result.plan.vertex_indices.size(), geometry.vertex_indices.size()) ||
            !append_fits_u32(result.plan.triangle_indices.size(), geometry.triangle_indices.size())) {
            result.diagnostics = "Runtime GPU meshlet table exceeds the 32-bit flattened stream contract.";
            return result;
        }

        const std::uint32_t vertex_base = static_cast<std::uint32_t>(result.plan.vertex_indices.size());
        const std::uint32_t triangle_base = static_cast<std::uint32_t>(result.plan.triangle_indices.size());
        std::uint32_t expected_vertex_offset = 0U;
        std::uint32_t expected_triangle_offset = 0U;
        for (const auto& meshlet : geometry.meshlets) {
            if (meshlet.vertex_count == 0U || meshlet.vertex_count > render::meshlet_max_vertices ||
                meshlet.triangle_count == 0U || meshlet.triangle_count > render::meshlet_max_triangles ||
                meshlet.vertex_offset != expected_vertex_offset ||
                meshlet.triangle_offset != expected_triangle_offset ||
                !finite_bounds(meshlet.bounds)) {
                result.diagnostics = "Runtime GPU meshlet table rejected invalid or non-canonical cluster metadata.";
                return result;
            }
            const std::uint64_t vertex_end = static_cast<std::uint64_t>(meshlet.vertex_offset) +
                meshlet.vertex_count;
            const std::uint64_t triangle_index_count =
                static_cast<std::uint64_t>(meshlet.triangle_count) * 3U;
            const std::uint64_t triangle_end = static_cast<std::uint64_t>(meshlet.triangle_offset) +
                triangle_index_count;
            if (vertex_end > geometry.vertex_indices.size() ||
                triangle_end > geometry.triangle_indices.size() ||
                triangle_end > cooked.asset.indices.size()) {
                result.diagnostics = "Runtime GPU meshlet table rejected out-of-range cluster metadata.";
                return result;
            }
            if (static_cast<std::uint64_t>(vertex_base) + meshlet.vertex_offset >
                    std::numeric_limits<std::uint32_t>::max() ||
                static_cast<std::uint64_t>(triangle_base) + meshlet.triangle_offset >
                    std::numeric_limits<std::uint32_t>::max()) {
                result.diagnostics = "Runtime GPU meshlet table cluster offsets overflowed 32 bits.";
                return result;
            }

            RuntimeGpuMeshletRecord record;
            record.bounds_center_radius[0] = meshlet.bounds.center[0];
            record.bounds_center_radius[1] = meshlet.bounds.center[1];
            record.bounds_center_radius[2] = meshlet.bounds.center[2];
            record.bounds_center_radius[3] = meshlet.bounds.radius;
            record.vertex_offset = vertex_base + meshlet.vertex_offset;
            record.vertex_count = meshlet.vertex_count;
            record.triangle_offset = triangle_base + meshlet.triangle_offset;
            record.triangle_count = meshlet.triangle_count;
            record.source_first_index = meshlet.triangle_offset;
            record.source_index_count = meshlet.triangle_count * 3U;
            record.mesh_index = static_cast<std::uint32_t>(mesh_index);
            result.plan.meshlets.push_back(record);

            expected_vertex_offset += meshlet.vertex_count;
            expected_triangle_offset += meshlet.triangle_count * 3U;
        }
        if (expected_vertex_offset != geometry.vertex_indices.size() ||
            expected_triangle_offset != geometry.triangle_indices.size() ||
            expected_triangle_offset != cooked.asset.indices.size()) {
            result.diagnostics = "Runtime GPU meshlet table rejected incomplete cluster streams.";
            return result;
        }

        mesh_record.meshlet_count = static_cast<std::uint32_t>(geometry.meshlets.size());
        result.plan.meshes.push_back(mesh_record);
        result.plan.vertex_indices.insert(
            result.plan.vertex_indices.end(),
            geometry.vertex_indices.begin(),
            geometry.vertex_indices.end()
        );
        result.plan.triangle_indices.reserve(
            result.plan.triangle_indices.size() + geometry.triangle_indices.size()
        );
        for (const std::uint8_t local_index : geometry.triangle_indices) {
            result.plan.triangle_indices.push_back(local_index);
        }
    }

    return result;
}

struct RuntimeGpuMeshletTable::State {
    render::Device* device = nullptr;
    GpuStream meshes;
    GpuStream meshlets;
    GpuStream vertex_indices;
    GpuStream triangle_indices;
    std::uint32_t mesh_count = 0U;
    std::uint32_t meshlet_count = 0U;
    std::uint32_t vertex_index_count = 0U;
    std::uint32_t triangle_index_count = 0U;
};

RuntimeGpuMeshletTable::RuntimeGpuMeshletTable() : state_{std::make_unique<State>()} {}
RuntimeGpuMeshletTable::~RuntimeGpuMeshletTable() = default;
RuntimeGpuMeshletTable::RuntimeGpuMeshletTable(RuntimeGpuMeshletTable&&) noexcept = default;
RuntimeGpuMeshletTable& RuntimeGpuMeshletTable::operator=(RuntimeGpuMeshletTable&&) noexcept = default;

bool RuntimeGpuMeshletTable::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU meshlet table state is unavailable.");
        return false;
    }
    state_->device = &device;
    state_->meshes = {};
    state_->meshlets = {};
    state_->vertex_indices = {};
    state_->triangle_indices = {};
    state_->mesh_count = 0U;
    state_->meshlet_count = 0U;
    state_->vertex_index_count = 0U;
    state_->triangle_index_count = 0U;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeGpuMeshletTable::upload(
    render::CommandList& commands,
    const RuntimeGpuMeshletTablePlan& plan,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime GPU meshlet table is not initialized.");
        return false;
    }
    if (plan.meshes.size() > std::numeric_limits<std::uint32_t>::max() ||
        plan.meshlets.size() > std::numeric_limits<std::uint32_t>::max() ||
        plan.vertex_indices.size() > std::numeric_limits<std::uint32_t>::max() ||
        plan.triangle_indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(diagnostics, "Runtime GPU meshlet table upload exceeds the 32-bit count contract.");
        return false;
    }

    if (!upload_stream(
            *state_->device,
            commands,
            state_->meshes,
            std::span<const RuntimeGpuMeshRecord>{plan.meshes},
            "VICE Runtime GPU mesh record upload",
            "VICE Runtime GPU mesh record table",
            diagnostics
        ) || !upload_stream(
            *state_->device,
            commands,
            state_->meshlets,
            std::span<const RuntimeGpuMeshletRecord>{plan.meshlets},
            "VICE Runtime GPU meshlet record upload",
            "VICE Runtime GPU meshlet record table",
            diagnostics
        ) || !upload_stream(
            *state_->device,
            commands,
            state_->vertex_indices,
            std::span<const std::uint32_t>{plan.vertex_indices},
            "VICE Runtime GPU meshlet vertex-remap upload",
            "VICE Runtime GPU meshlet vertex-remap table",
            diagnostics
        ) || !upload_stream(
            *state_->device,
            commands,
            state_->triangle_indices,
            std::span<const std::uint32_t>{plan.triangle_indices},
            "VICE Runtime GPU meshlet triangle-index upload",
            "VICE Runtime GPU meshlet triangle-index table",
            diagnostics
        )) {
        return false;
    }

    state_->mesh_count = static_cast<std::uint32_t>(plan.meshes.size());
    state_->meshlet_count = static_cast<std::uint32_t>(plan.meshlets.size());
    state_->vertex_index_count = static_cast<std::uint32_t>(plan.vertex_indices.size());
    state_->triangle_index_count = static_cast<std::uint32_t>(plan.triangle_indices.size());
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeGpuMeshletTableResult RuntimeGpuMeshletTable::result() const noexcept {
    if (state_ == nullptr) return {};
    return RuntimeGpuMeshletTableResult{
        .meshes = state_->mesh_count > 0U ? state_->meshes.gpu.get() : nullptr,
        .meshlets = state_->meshlet_count > 0U ? state_->meshlets.gpu.get() : nullptr,
        .vertex_indices = state_->vertex_index_count > 0U ? state_->vertex_indices.gpu.get() : nullptr,
        .triangle_indices = state_->triangle_index_count > 0U ? state_->triangle_indices.gpu.get() : nullptr,
        .mesh_count = state_->mesh_count,
        .meshlet_count = state_->meshlet_count,
        .vertex_index_count = state_->vertex_index_count,
        .triangle_index_count = state_->triangle_index_count,
    };
}

} // namespace vice::runtime
