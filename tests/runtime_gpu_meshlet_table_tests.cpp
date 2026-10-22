#include <vice/render/meshlet.hpp>
#include <vice/runtime/gpu_meshlet_table.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    if (!parsed.has_value()) {
        std::cerr << "Invalid test UUID: " << text << '\n';
        std::terminate();
    }
    return *parsed;
}

[[nodiscard]] vice::render::MeshVertex vertex(
    const float x,
    const float y,
    const float z
) {
    vice::render::MeshVertex value;
    value.position[0] = x;
    value.position[1] = y;
    value.position[2] = z;
    return value;
}

[[nodiscard]] vice::runtime::CookedMeshRecord mesh_record(
    const vice::world::PersistentId id,
    std::vector<vice::render::MeshVertex> vertices,
    std::vector<std::uint32_t> indices,
    const bool clustered
) {
    using namespace vice;
    runtime::CookedMeshRecord record;
    record.resource_id = world::ResourceId{id};
    record.asset.version = clustered
        ? render::mesh_asset_format_version_v4
        : render::mesh_asset_format_version_v3;
    record.asset.vertices = std::move(vertices);
    record.asset.indices = std::move(indices);
    if (clustered) {
        const auto built = render::build_meshlets(record.asset.vertices, record.asset.indices);
        if (!built) {
            std::cerr << "Could not build test meshlets: " << built.diagnostics << '\n';
            std::terminate();
        }
        record.asset.meshlet_geometry = *built.geometry;
    }
    return record;
}

template <typename T>
[[nodiscard]] bool same_bytes(
    const std::span<const T> left,
    const std::span<const T> right
) noexcept {
    return left.size() == right.size() &&
        (left.empty() || std::memcmp(left.data(), right.data(), left.size_bytes()) == 0);
}

[[nodiscard]] std::unique_ptr<vice::render::Buffer> make_readback(
    vice::render::Device& device,
    const std::uint64_t size,
    const char* const name
) {
    vice::render::BufferDesc desc;
    desc.size = size;
    desc.usage = vice::render::BufferUsage::copy_destination;
    desc.memory = vice::render::MemoryLocation::gpu_to_cpu;
    desc.debug_name = name;
    return device.create_buffer(desc);
}

[[nodiscard]] bool verify_mesh_reconstruction(
    const vice::runtime::RuntimeGpuMeshletTablePlan& plan,
    const std::uint32_t mesh_index,
    const std::span<const std::uint32_t> expected_indices
) {
    if (mesh_index >= plan.meshes.size()) return false;
    const auto& mesh = plan.meshes[mesh_index];
    std::vector<std::uint32_t> reconstructed;
    reconstructed.reserve(expected_indices.size());
    for (std::uint32_t offset = 0U; offset < mesh.meshlet_count; ++offset) {
        const auto& cluster = plan.meshlets[mesh.first_meshlet + offset];
        for (std::uint32_t triangle = 0U; triangle < cluster.triangle_count; ++triangle) {
            for (std::uint32_t corner = 0U; corner < 3U; ++corner) {
                const std::uint32_t local = plan.triangle_indices[
                    cluster.triangle_offset + triangle * 3U + corner
                ];
                if (local >= cluster.vertex_count) return false;
                reconstructed.push_back(plan.vertex_indices[cluster.vertex_offset + local]);
            }
        }
    }
    return reconstructed.size() == expected_indices.size() &&
        std::equal(reconstructed.begin(), reconstructed.end(), expected_indices.begin());
}

[[nodiscard]] bool test_plan() {
    using namespace vice;
    using namespace vice::runtime;

    const auto mesh_a_id = required_id("73000000-0000-4000-8000-000000000001");
    const auto mesh_b_id = required_id("73000000-0000-4000-8000-000000000002");
    const auto mesh_c_id = required_id("73000000-0000-4000-8000-000000000003");

    const std::vector<render::MeshVertex> quad_vertices{
        vertex(-1.0F, 0.0F, -1.0F),
        vertex(1.0F, 0.0F, -1.0F),
        vertex(-1.0F, 0.0F, 1.0F),
        vertex(1.0F, 0.0F, 1.0F),
    };
    const std::vector<std::uint32_t> quad_indices{0U, 1U, 2U, 2U, 1U, 3U};
    const std::vector<render::MeshVertex> triangle_vertices{
        vertex(0.0F, 0.0F, 0.0F),
        vertex(2.0F, 0.0F, 0.0F),
        vertex(0.0F, 2.0F, 0.0F),
    };
    const std::vector<std::uint32_t> triangle_indices{0U, 1U, 2U};

    const auto mesh_a = mesh_record(mesh_a_id, quad_vertices, quad_indices, true);
    const auto mesh_b = mesh_record(mesh_b_id, triangle_vertices, triangle_indices, true);
    const auto mesh_c = mesh_record(mesh_c_id, triangle_vertices, triangle_indices, false);
    const std::array forward_input{mesh_a, mesh_b, mesh_c};
    const std::array reverse_input{mesh_c, mesh_b, mesh_a};

    const auto forward = build_runtime_gpu_meshlet_table_plan(forward_input);
    const auto reverse = build_runtime_gpu_meshlet_table_plan(reverse_input);
    if (!forward || !reverse) {
        std::cerr << "Could not build deterministic GPU meshlet plans: "
                  << forward.diagnostics << ' ' << reverse.diagnostics << '\n';
        return false;
    }
    if (forward.plan.mesh_ids != reverse.plan.mesh_ids ||
        !same_bytes<RuntimeGpuMeshRecord>(forward.plan.meshes, reverse.plan.meshes) ||
        !same_bytes<RuntimeGpuMeshletRecord>(forward.plan.meshlets, reverse.plan.meshlets) ||
        forward.plan.vertex_indices != reverse.plan.vertex_indices ||
        forward.plan.triangle_indices != reverse.plan.triangle_indices) {
        std::cerr << "GPU meshlet plan depends on input enumeration order.\n";
        return false;
    }

    if (forward.plan.meshes.size() != 3U || forward.plan.meshlets.size() != 2U ||
        forward.plan.mesh_index(world::ResourceId{mesh_a_id}) != std::optional<std::uint32_t>{0U} ||
        forward.plan.mesh_index(world::ResourceId{mesh_b_id}) != std::optional<std::uint32_t>{1U} ||
        forward.plan.mesh_index(world::ResourceId{mesh_c_id}) != std::optional<std::uint32_t>{2U}) {
        std::cerr << "GPU meshlet stable mesh ordering is incorrect.\n";
        return false;
    }

    const auto& mesh_a_gpu = forward.plan.meshes[0];
    const auto& mesh_b_gpu = forward.plan.meshes[1];
    const auto& mesh_c_gpu = forward.plan.meshes[2];
    if (mesh_a_gpu.first_meshlet != 0U || mesh_a_gpu.meshlet_count != 1U ||
        mesh_b_gpu.first_meshlet != 1U || mesh_b_gpu.meshlet_count != 1U ||
        mesh_c_gpu.first_meshlet != 2U || mesh_c_gpu.meshlet_count != 0U) {
        std::cerr << "GPU mesh-to-cluster ranges are incorrect.\n";
        return false;
    }

    const auto& cluster_a = forward.plan.meshlets[0];
    const auto& cluster_b = forward.plan.meshlets[1];
    if (cluster_a.mesh_index != 0U || cluster_a.source_first_index != 0U ||
        cluster_a.source_index_count != quad_indices.size() ||
        cluster_a.vertex_count != 4U || cluster_a.triangle_count != 2U ||
        cluster_b.mesh_index != 1U || cluster_b.source_first_index != 0U ||
        cluster_b.source_index_count != triangle_indices.size() ||
        cluster_b.vertex_offset != cluster_a.vertex_offset + cluster_a.vertex_count ||
        cluster_b.triangle_offset != cluster_a.triangle_offset + cluster_a.source_index_count) {
        std::cerr << "GPU cluster records did not preserve flattened/source ranges.\n";
        return false;
    }

    if (!verify_mesh_reconstruction(forward.plan, 0U, quad_indices) ||
        !verify_mesh_reconstruction(forward.plan, 1U, triangle_indices)) {
        std::cerr << "GPU meshlet streams do not reconstruct source mesh indices.\n";
        return false;
    }

    auto malformed = mesh_a;
    malformed.asset.meshlet_geometry.meshlets[0].triangle_offset = 1U;
    const std::array malformed_input{malformed};
    const auto malformed_plan = build_runtime_gpu_meshlet_table_plan(malformed_input);
    if (malformed_plan) {
        std::cerr << "GPU meshlet plan accepted non-canonical cluster offsets.\n";
        return false;
    }

    const RuntimeGpuMeshletTableBuildResult empty = build_runtime_gpu_meshlet_table_plan({});
    if (!empty || !empty.plan.meshes.empty() || !empty.plan.meshlets.empty() ||
        !empty.plan.vertex_indices.empty() || !empty.plan.triangle_indices.empty()) {
        std::cerr << "Empty GPU meshlet plan is not safe.\n";
        return false;
    }

    return true;
}

[[nodiscard]] bool test_gpu_upload() {
    using namespace vice;
    using namespace vice::runtime;

    const auto mesh_a_id = required_id("74000000-0000-4000-8000-000000000001");
    const auto mesh_b_id = required_id("74000000-0000-4000-8000-000000000002");
    const auto mesh_a = mesh_record(
        mesh_a_id,
        {
            vertex(-1.0F, 0.0F, -1.0F),
            vertex(1.0F, 0.0F, -1.0F),
            vertex(-1.0F, 0.0F, 1.0F),
            vertex(1.0F, 0.0F, 1.0F),
        },
        {0U, 1U, 2U, 2U, 1U, 3U},
        true
    );
    const auto mesh_b = mesh_record(
        mesh_b_id,
        {
            vertex(0.0F, 0.0F, 0.0F),
            vertex(1.0F, 0.0F, 0.0F),
            vertex(0.0F, 1.0F, 0.0F),
        },
        {0U, 1U, 2U},
        true
    );
    const std::array meshes{mesh_b, mesh_a};
    const auto built = build_runtime_gpu_meshlet_table_plan(meshes);
    if (!built) {
        std::cerr << "Could not build GPU meshlet upload plan: " << built.diagnostics << '\n';
        return false;
    }

    auto device = render::Device::create();
    if (!device) {
        std::cerr << "Could not create Tier 1 device for GPU meshlet table smoke.\n";
        return false;
    }
    RuntimeGpuMeshletTable table;
    std::string diagnostics;
    if (!table.initialize(*device, &diagnostics)) {
        std::cerr << "Could not initialize GPU meshlet table: " << diagnostics << '\n';
        return false;
    }

    auto commands = device->create_command_list();
    if (!commands || !table.upload(*commands, built.plan, &diagnostics)) {
        std::cerr << "Could not encode GPU meshlet upload: " << diagnostics << '\n';
        return false;
    }
    const auto gpu = table.result();
    if (gpu.meshes == nullptr || gpu.meshlets == nullptr || gpu.vertex_indices == nullptr ||
        gpu.triangle_indices == nullptr || gpu.mesh_count != built.plan.meshes.size() ||
        gpu.meshlet_count != built.plan.meshlets.size() ||
        gpu.vertex_index_count != built.plan.vertex_indices.size() ||
        gpu.triangle_index_count != built.plan.triangle_indices.size()) {
        std::cerr << "GPU meshlet table exposed invalid upload state.\n";
        return false;
    }

    const std::uint64_t mesh_bytes = built.plan.meshes.size() * sizeof(RuntimeGpuMeshRecord);
    const std::uint64_t meshlet_bytes = built.plan.meshlets.size() * sizeof(RuntimeGpuMeshletRecord);
    const std::uint64_t vertex_bytes = built.plan.vertex_indices.size() * sizeof(std::uint32_t);
    const std::uint64_t triangle_bytes = built.plan.triangle_indices.size() * sizeof(std::uint32_t);
    auto mesh_readback = make_readback(*device, mesh_bytes, "GPU mesh records readback");
    auto meshlet_readback = make_readback(*device, meshlet_bytes, "GPU meshlet records readback");
    auto vertex_readback = make_readback(*device, vertex_bytes, "GPU meshlet vertices readback");
    auto triangle_readback = make_readback(*device, triangle_bytes, "GPU meshlet triangles readback");
    if (!mesh_readback || !meshlet_readback || !vertex_readback || !triangle_readback ||
        !commands->copy_buffer(*gpu.meshes, 0U, *mesh_readback, 0U, mesh_bytes) ||
        !commands->copy_buffer(*gpu.meshlets, 0U, *meshlet_readback, 0U, meshlet_bytes) ||
        !commands->copy_buffer(*gpu.vertex_indices, 0U, *vertex_readback, 0U, vertex_bytes) ||
        !commands->copy_buffer(*gpu.triangle_indices, 0U, *triangle_readback, 0U, triangle_bytes)) {
        std::cerr << "Could not encode GPU meshlet readback copies.\n";
        return false;
    }

    const auto submission = device->submit(std::move(commands));
    if (submission == render::invalid_submission || !device->wait_for_submission(submission)) {
        std::cerr << "GPU meshlet upload submission failed.\n";
        return false;
    }

    std::vector<RuntimeGpuMeshRecord> actual_meshes(built.plan.meshes.size());
    std::vector<RuntimeGpuMeshletRecord> actual_meshlets(built.plan.meshlets.size());
    std::vector<std::uint32_t> actual_vertices(built.plan.vertex_indices.size());
    std::vector<std::uint32_t> actual_triangles(built.plan.triangle_indices.size());
    if (!mesh_readback->read(0U, std::as_writable_bytes(std::span{actual_meshes})) ||
        !meshlet_readback->read(0U, std::as_writable_bytes(std::span{actual_meshlets})) ||
        !vertex_readback->read(0U, std::as_writable_bytes(std::span{actual_vertices})) ||
        !triangle_readback->read(0U, std::as_writable_bytes(std::span{actual_triangles})) ||
        !same_bytes<RuntimeGpuMeshRecord>(actual_meshes, built.plan.meshes) ||
        !same_bytes<RuntimeGpuMeshletRecord>(actual_meshlets, built.plan.meshlets) ||
        actual_vertices != built.plan.vertex_indices ||
        actual_triangles != built.plan.triangle_indices) {
        std::cerr << "GPU meshlet readback did not match source table data.\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    if (!test_plan()) return 1;
    if (!test_gpu_upload()) return 1;
    return 0;
}
