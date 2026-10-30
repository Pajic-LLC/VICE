#include <vice/render/mesh_asset.hpp>
#include <vice/render/virtual_geometry_stream.hpp>
#include <vice/runtime/virtual_geometry_streaming.hpp>
#include <vice/tools/mesh_cooker.hpp>
#include <vice/world/world.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    if (!parsed.has_value()) {
        std::cerr << "Invalid test resource ID: " << text << '\n';
        std::terminate();
    }
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

[[nodiscard]] bool fail(const std::string& message) {
    std::cerr << message << '\n';
    return false;
}

[[nodiscard]] bool run_test() {
    using namespace vice;

    const auto source = make_source();
    const auto cooked = tools::mesh_cooker::cook(source);
    if (!cooked.success) {
        return fail("v7 cook failed: " + cooked.diagnostics);
    }
    const auto cooked_repeat = tools::mesh_cooker::cook(source);
    if (!cooked_repeat.success) {
        return fail("repeated v7 cook failed: " + cooked_repeat.diagnostics);
    }
    if (cooked.bytes != cooked_repeat.bytes) {
        return fail("v7 cooking is not deterministic.");
    }

    const auto decoded = render::decode_mesh_asset(cooked.bytes);
    if (!decoded.asset.has_value()) {
        return fail("v7 decode failed: " + decoded.diagnostics);
    }
    if (decoded.asset->version != render::mesh_asset_format_version_v7 ||
        decoded.asset->virtual_geometry_pages.pages.size() != 2U) {
        return fail("v7 decode did not preserve the expected two-page virtual geometry asset.");
    }

    std::error_code temp_error;
    const auto temp_root = std::filesystem::temp_directory_path(temp_error);
    if (temp_error) {
        return fail("Could not resolve the temporary directory: " + temp_error.message());
    }
    const auto path = temp_root / "vice_mesh_asset_v7_streaming_test.vmeshc";
    const auto corrupt_path = temp_root / "vice_mesh_asset_v7_streaming_corrupt.vmeshc";
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
    cleanup_error.clear();
    std::filesystem::remove(corrupt_path, cleanup_error);

    const auto cleanup = [&]() {
        std::error_code error;
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::remove(corrupt_path, error);
    };

    if (!write_bytes(path, cooked.bytes)) {
        cleanup();
        return fail("Could not write the v7 streaming fixture.");
    }

    const auto directory = render::load_virtual_geometry_stream_directory(path);
    if (!directory) {
        cleanup();
        return fail("Could not load the v7 stream directory: " + directory.diagnostics);
    }
    if (directory.directory->pages.size() != 2U ||
        directory.directory->pages[0].page_index != 0U ||
        directory.directory->pages[1].page_index != 1U ||
        directory.directory->pages[0].payload_offset != directory.directory->core_size ||
        directory.directory->pages[1].payload_offset !=
            directory.directory->pages[0].payload_offset +
                directory.directory->pages[0].payload_size) {
        cleanup();
        return fail("v7 stream directory is not canonical.");
    }

    const auto first_page = render::load_virtual_geometry_stream_page(
        path, *directory.directory, 0U
    );
    if (!first_page) {
        cleanup();
        return fail("Could not stream v7 page 0: " + first_page.diagnostics);
    }
    const auto second_page = render::load_virtual_geometry_stream_page(
        path, *directory.directory, 1U
    );
    if (!second_page) {
        cleanup();
        return fail("Could not stream v7 page 1: " + second_page.diagnostics);
    }
    if (first_page.page->indices.size() / 3U != 32U * 21U ||
        second_page.page->indices.size() / 3U != 1U ||
        first_page.page->byte_size() <= second_page.page->byte_size()) {
        cleanup();
        return fail("v7 streamed page geometry does not match the canonical leaf partition.");
    }

    auto corrupt = cooked.bytes;
    const auto corrupt_offset = directory.directory->pages[0].payload_offset + 8U;
    if (corrupt_offset >= corrupt.size()) {
        cleanup();
        return fail("v7 page 0 corruption offset is outside the cooked archive.");
    }
    corrupt[static_cast<std::size_t>(corrupt_offset)] ^= std::byte{0x01U};
    if (!write_bytes(corrupt_path, corrupt)) {
        cleanup();
        return fail("Could not write the corrupted v7 streaming fixture.");
    }
    const auto corrupted_first = render::load_virtual_geometry_stream_page(corrupt_path, 0U);
    if (corrupted_first) {
        cleanup();
        return fail("Corrupted v7 page 0 unexpectedly passed its payload fingerprint.");
    }
    const auto unaffected_second = render::load_virtual_geometry_stream_page(corrupt_path, 1U);
    if (!unaffected_second) {
        cleanup();
        return fail(
            "Corrupting v7 page 0 prevented independent page 1 streaming: " +
            unaffected_second.diagnostics
        );
    }
    if (render::decode_mesh_asset(corrupt).asset.has_value()) {
        cleanup();
        return fail("Full v7 decode accepted a corrupted authoritative page payload.");
    }

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
    if (!requests) {
        cleanup();
        return fail("Could not canonicalize GPU page requests: " + requests.diagnostics);
    }
    if (requests.requests.size() != 2U || requests.requests[0].page_index != 0U ||
        requests.requests[1].page_index != 1U) {
        cleanup();
        return fail("GPU page request canonicalization did not deduplicate by mesh/page identity.");
    }

    auto invalid_gpu_requests = gpu_requests;
    invalid_gpu_requests.front().page_index = 1U;
    if (runtime::build_runtime_virtual_geometry_page_requests(invalid_gpu_requests, plan)) {
        cleanup();
        return fail("GPU page request canonicalization accepted a leaf/page mismatch.");
    }

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
    const auto first_update = pool.update(first_request, meshes);
    if (!first_update) {
        cleanup();
        return fail("Could not admit v7 page 0 into the resident pool: " + first_update.diagnostics);
    }
    if (first_update.admitted !=
            std::vector<runtime::RuntimeVirtualGeometryPageKey>{first_request[0]} ||
        !first_update.evicted.empty() || !pool.resident(first_request[0])) {
        cleanup();
        return fail("Initial v7 page residency admission produced an invalid pool state.");
    }

    const auto repeat_update = pool.update(first_request, meshes);
    if (!repeat_update) {
        cleanup();
        return fail("Resident v7 page hit failed: " + repeat_update.diagnostics);
    }
    if (!repeat_update.admitted.empty() || !repeat_update.evicted.empty() ||
        pool.pages().size() != 1U) {
        cleanup();
        return fail("Resident v7 page hit duplicated or evicted pool storage.");
    }

    const std::array second_request{requests.requests[1]};
    const auto second_update = pool.update(second_request, meshes);
    if (!second_update) {
        cleanup();
        return fail("Could not replace v7 page 0 with page 1: " + second_update.diagnostics);
    }
    if (second_update.admitted !=
            std::vector<runtime::RuntimeVirtualGeometryPageKey>{second_request[0]} ||
        second_update.evicted !=
            std::vector<runtime::RuntimeVirtualGeometryPageKey>{first_request[0]} ||
        pool.resident(first_request[0]) || !pool.resident(second_request[0])) {
        cleanup();
        return fail("One-page virtual geometry budget did not evict deterministically.");
    }

    if (pool.pages().size() != 1U) {
        cleanup();
        return fail("Resident pool did not contain exactly one page before overflow test.");
    }
    const auto before_overflow = pool.pages().front().key;
    const auto overflow_update = pool.update(requests.requests, meshes);
    if (overflow_update) {
        cleanup();
        return fail("Resident pool accepted a working set larger than its byte budget.");
    }
    if (pool.pages().size() != 1U || pool.pages().front().key != before_overflow) {
        cleanup();
        return fail("Over-budget residency update mutated the committed resident pool.");
    }

    const std::span<const runtime::RuntimeVirtualGeometryPageKey> no_leaf_requests{};
    const auto proxy_only_update = pool.update(no_leaf_requests, meshes);
    if (!proxy_only_update) {
        cleanup();
        return fail("Proxy-only residency update failed: " + proxy_only_update.diagnostics);
    }
    if (!proxy_only_update.admitted.empty() || pool.pages().size() != 1U) {
        cleanup();
        return fail("Proxy-only selection unexpectedly forced authoritative leaf residency.");
    }

    cleanup();
    return true;
}

} // namespace

int main() {
    {
        const auto parsed = vice::tools::mesh_cooker::parse_source_manifest(
            "vice-mesh-source 2\nvirtual_geometry dense\nname deforming\n"
            "id 84000000-0000-4000-8000-000000000021\n"
            "vertex 0 0 0 1 1 1 0 0 1 0 0\n"
            "vertex 1 0 0 1 1 1 0 0 1 1 0\n"
            "vertex 0 1 0 1 1 1 0 0 1 0 1\ntriangle 0 1 2\n");
        assert(parsed.source && !parsed.source->build_virtual_geometry_proxies);
        const auto cooked = vice::tools::mesh_cooker::cook(*parsed.source);
        assert(cooked.success);
        const auto decoded = vice::render::decode_mesh_asset(cooked.bytes);
        assert(decoded.asset && decoded.asset->vertices.size() == 3);
        assert(decoded.asset->indices == parsed.source->indices);
        assert(decoded.asset->virtual_geometry_proxies.proxies.empty());
        assert(!vice::tools::mesh_cooker::parse_source_manifest(
            "vice-mesh-source 2\nvirtual_geometry invented\n").source);
    }

    return run_test() ? 0 : 1;
}
