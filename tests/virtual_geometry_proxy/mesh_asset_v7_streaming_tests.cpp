#include <vice/render/mesh_asset.hpp>
#include <vice/render/virtual_geometry_stream.hpp>
#include <vice/runtime/virtual_geometry_streaming.hpp>
#include <vice/tools/mesh_cooker.hpp>
#include <vice/world/world.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] vice::tools::mesh_cooker::MeshSource make_source() {
    using namespace vice;
    tools::mesh_cooker::MeshSource source;
    source.version = tools::mesh_cooker::mesh_source_format_version_v2;
    source.name = "virtual_geometry_v7_stream";
    source.stable_id[0] = 0x77U;

    constexpr std::uint32_t triangle_count = 32U * 21U + 1U;
    source.vertices.reserve(static_cast<std::size_t>(triangle_count) * 3U);
    source.indices.reserve(static_cast<std::size_t>(triangle_count) * 3U);
    for (std::uint32_t triangle = 0U; triangle < triangle_count; ++triangle) {
        const auto base_index = static_cast<std::uint32_t>(source.vertices.size());
        const float base_x = static_cast<float>(triangle) * 2.0F;
        for (std::uint32_t corner = 0U; corner < 3U; ++corner) {
            render::MeshVertex vertex;
            vertex.position[0] = base_x + (corner == 1U ? 1.0F : 0.0F);
            vertex.position[1] = 0.0F;
            vertex.position[2] = corner == 2U ? 1.0F : 0.0F;
            vertex.color[0] = 0.2F;
            vertex.color[1] = 0.5F;
            vertex.color[2] = 0.8F;
            vertex.normal[1] = 1.0F;
            source.vertices.push_back(vertex);
        }
        source.indices.push_back(base_index);
        source.indices.push_back(base_index + 2U);
        source.indices.push_back(base_index + 1U);
    }
    return source;
}

[[nodiscard]] bool write_bytes(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes
) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) return false;
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    return output.good();
}

} // namespace

int main() {
    using namespace vice;

    const auto source = make_source();
    const auto cooked = tools::mesh_cooker::cook(source);
    const auto cooked_repeat = tools::mesh_cooker::cook(source);
    assert(cooked.success);
    assert(cooked_repeat.success);
    assert(cooked.bytes == cooked_repeat.bytes);

    const auto decoded = render::decode_mesh_asset(cooked.bytes);
    assert(decoded.asset.has_value());
    assert(decoded.asset->version == render::mesh_asset_format_version_v7);
    assert(decoded.asset->virtual_geometry_pages.pages.size() == 2U);

    const auto temp_root = std::filesystem::temp_directory_path();
    const auto path = temp_root / "vice_mesh_asset_v7_streaming_test.vmeshc";
    const auto corrupt_path = temp_root / "vice_mesh_asset_v7_streaming_corrupt.vmeshc";
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
    std::filesystem::remove(corrupt_path, cleanup_error);
    assert(write_bytes(path, cooked.bytes));

    const auto directory = render::load_virtual_geometry_stream_directory(path);
    assert(directory);
    assert(directory.directory->pages.size() == 2U);
    assert(directory.directory->pages[0].page_index == 0U);
    assert(directory.directory->pages[1].page_index == 1U);
    assert(directory.directory->pages[0].payload_offset == directory.directory->core_size);
    assert(
        directory.directory->pages[1].payload_offset ==
        directory.directory->pages[0].payload_offset + directory.directory->pages[0].payload_size
    );

    const auto first_page = render::load_virtual_geometry_stream_page(path, *directory.directory, 0U);
    const auto second_page = render::load_virtual_geometry_stream_page(path, *directory.directory, 1U);
    assert(first_page);
    assert(second_page);
    assert(first_page.page->indices.size() / 3U == 32U * 21U);
    assert(second_page.page->indices.size() / 3U == 1U);
    assert(first_page.page->byte_size() > second_page.page->byte_size());

    auto corrupt = cooked.bytes;
    const auto corrupt_offset = directory.directory->pages[0].payload_offset + 8U;
    assert(corrupt_offset < corrupt.size());
    corrupt[static_cast<std::size_t>(corrupt_offset)] ^= std::byte{0x01U};
    assert(write_bytes(corrupt_path, corrupt));
    assert(!render::load_virtual_geometry_stream_page(corrupt_path, 0U));
    assert(render::load_virtual_geometry_stream_page(corrupt_path, 1U));
    assert(!render::decode_mesh_asset(corrupt).asset.has_value());

    const world::ResourceId mesh_id{
        required_id("87000000-0000-4000-8000-000000000001")
    };
    runtime::RuntimeGpuVirtualGeometryPlan plan;
    plan.mesh_ids = {mesh_id};
    plan.scene_instance_count = 2U;
    plan.nodes = {
        runtime::RuntimeGpuVirtualGeometryNodeRecord{
            .child_count = 0U,
            .page_offset = 0U,
            .page_count = 1U,
            .mesh_index = 0U,
            .local_node_index = 0U,
        },
        runtime::RuntimeGpuVirtualGeometryNodeRecord{
            .child_count = 0U,
            .page_offset = 1U,
            .page_count = 1U,
            .mesh_index = 0U,
            .local_node_index = 1U,
        },
    };
    const std::vector<runtime::RuntimeGpuVirtualGeometryPageRequest> gpu_requests{
        runtime::RuntimeGpuVirtualGeometryPageRequest{
            .scene_instance_index = 0U,
            .mesh_index = 0U,
            .node_index = 0U,
            .page_index = 0U,
        },
        runtime::RuntimeGpuVirtualGeometryPageRequest{
            .scene_instance_index = 1U,
            .mesh_index = 0U,
            .node_index = 0U,
            .page_index = 0U,
        },
        runtime::RuntimeGpuVirtualGeometryPageRequest{
            .scene_instance_index = 1U,
            .mesh_index = 0U,
            .node_index = 1U,
            .page_index = 1U,
        },
    };
    const auto requests = runtime::build_runtime_virtual_geometry_page_requests(gpu_requests, plan);
    assert(requests);
    assert(requests.requests.size() == 2U);
    assert(requests.requests[0].page_index == 0U);
    assert(requests.requests[1].page_index == 1U);

    auto invalid_gpu_requests = gpu_requests;
    invalid_gpu_requests.front().page_index = 1U;
    assert(!runtime::build_runtime_virtual_geometry_page_requests(invalid_gpu_requests, plan));

    const std::vector<runtime::CookedMeshRecord> meshes{
        runtime::CookedMeshRecord{
            .resource_id = mesh_id,
            .path = path,
            .asset = *decoded.asset,
            .runtime_generated = false,
        },
    };

    const std::uint64_t one_page_budget = first_page.page->byte_size();
    runtime::RuntimeVirtualGeometryResidentPool pool{one_page_budget};
    const std::array first_request{requests.requests[0]};
    auto first_update = pool.update(first_request, meshes);
    assert(first_update);
    assert(first_update.admitted == std::vector<runtime::RuntimeVirtualGeometryPageKey>{first_request[0]});
    assert(first_update.evicted.empty());
    assert(pool.resident(first_request[0]));

    const auto repeat_update = pool.update(first_request, meshes);
    assert(repeat_update);
    assert(repeat_update.admitted.empty());
    assert(repeat_update.evicted.empty());
    assert(pool.pages().size() == 1U);

    const std::array second_request{requests.requests[1]};
    const auto second_update = pool.update(second_request, meshes);
    assert(second_update);
    assert(second_update.admitted == std::vector<runtime::RuntimeVirtualGeometryPageKey>{second_request[0]});
    assert(second_update.evicted == std::vector<runtime::RuntimeVirtualGeometryPageKey>{first_request[0]});
    assert(!pool.resident(first_request[0]));
    assert(pool.resident(second_request[0]));

    const auto before_overflow = pool.pages().front().key;
    const auto overflow_update = pool.update(requests.requests, meshes);
    assert(!overflow_update);
    assert(pool.pages().size() == 1U);
    assert(pool.pages().front().key == before_overflow);

    const std::span<const runtime::RuntimeVirtualGeometryPageKey> no_leaf_requests{};
    const auto proxy_only_update = pool.update(no_leaf_requests, meshes);
    assert(proxy_only_update);
    assert(proxy_only_update.admitted.empty());
    assert(pool.pages().size() == 1U);

    std::filesystem::remove(path, cleanup_error);
    std::filesystem::remove(corrupt_path, cleanup_error);
    return 0;
}
