#include <vice/runtime/virtual_geometry.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

vice::world::PersistentId virtual_geometry_test_id(const std::uint8_t seed) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[0] = seed;
    bytes[6] = 0x40U;
    bytes[8] = 0x80U;
    return vice::world::PersistentId{bytes};
}

vice::render::MeshVertex virtual_geometry_test_vertex(const float x, const float z) {
    vice::render::MeshVertex result;
    result.position[0] = x;
    result.position[1] = 0.0F;
    result.position[2] = z;
    result.normal[1] = 1.0F;
    return result;
}

vice::render::MeshAsset virtual_geometry_test_asset() {
    using namespace vice;
    render::MeshAsset asset;
    asset.version = render::mesh_asset_format_version_v6;
    asset.vertices = {
        virtual_geometry_test_vertex(-2.0F, 0.0F),
        virtual_geometry_test_vertex(-1.0F, 0.0F),
        virtual_geometry_test_vertex(-1.5F, 1.0F),
        virtual_geometry_test_vertex(1.0F, 0.0F),
        virtual_geometry_test_vertex(2.0F, 0.0F),
        virtual_geometry_test_vertex(1.5F, 1.0F),
    };
    asset.indices = {0U, 1U, 2U, 3U, 4U, 5U};
    asset.virtual_geometry_pages.pages = {
        render::VirtualGeometryPage{
            .meshlet_offset = 0U,
            .meshlet_count = 1U,
            .triangle_index_offset = 0U,
            .triangle_index_count = 3U,
            .bounds = render::MeshletBounds{.center = {-1.5F, 0.0F, 0.5F}, .radius = 1.0F},
        },
        render::VirtualGeometryPage{
            .meshlet_offset = 1U,
            .meshlet_count = 1U,
            .triangle_index_offset = 3U,
            .triangle_index_count = 3U,
            .bounds = render::MeshletBounds{.center = {1.5F, 0.0F, 0.5F}, .radius = 1.0F},
        },
    };
    asset.virtual_geometry_proxies.hierarchy.root_index = 0U;
    asset.virtual_geometry_proxies.hierarchy.nodes = {
        render::VirtualGeometryHierarchyNode{
            .parent_index = render::virtual_geometry_invalid_node,
            .first_child = 1U,
            .child_count = 2U,
            .page_offset = 0U,
            .page_count = 2U,
            .depth = 0U,
            .bounds = render::MeshletBounds{.center = {0.0F, 0.0F, 0.5F}, .radius = 3.0F},
            .geometric_error = 1.0F,
        },
        render::VirtualGeometryHierarchyNode{
            .parent_index = 0U,
            .first_child = render::virtual_geometry_invalid_node,
            .child_count = 0U,
            .page_offset = 0U,
            .page_count = 1U,
            .depth = 1U,
            .bounds = render::MeshletBounds{.center = {-1.5F, 0.0F, 0.5F}, .radius = 1.0F},
            .geometric_error = 0.0F,
        },
        render::VirtualGeometryHierarchyNode{
            .parent_index = 0U,
            .first_child = render::virtual_geometry_invalid_node,
            .child_count = 0U,
            .page_offset = 1U,
            .page_count = 1U,
            .depth = 1U,
            .bounds = render::MeshletBounds{.center = {1.5F, 0.0F, 0.5F}, .radius = 1.0F},
            .geometric_error = 0.0F,
        },
    };
    asset.virtual_geometry_proxies.proxies = {
        render::VirtualGeometryProxyMesh{
            .node_index = 0U,
            .source_triangle_count = 2U,
            .direct_geometric_error = 1.0F,
            .vertices = {
                virtual_geometry_test_vertex(-2.0F, 0.0F),
                virtual_geometry_test_vertex(2.0F, 0.0F),
                virtual_geometry_test_vertex(0.0F, 1.0F),
            },
            .indices = {0U, 1U, 2U},
        },
    };
    return asset;
}

vice::runtime::RenderMeshInstance virtual_geometry_test_instance(
    const vice::world::ResourceId mesh,
    const std::uint8_t entity_seed,
    const double z
) {
    vice::runtime::RenderMeshInstance result;
    result.entity = virtual_geometry_test_id(entity_seed);
    result.mesh = mesh;
    result.spatial.global_position.z = z;
    return result;
}

[[nodiscard]] bool test_runtime_virtual_geometry_submission() {
    using namespace vice;

    const world::ResourceId mesh{virtual_geometry_test_id(0x21U)};
    const auto asset = virtual_geometry_test_asset();
    const runtime::CookedMeshRecord record{.resource_id = mesh, .asset = asset};

    const auto root_id = runtime::runtime_virtual_geometry_variant_resource_id(mesh, 0U);
    assert(root_id.valid());
    assert(root_id == runtime::runtime_virtual_geometry_variant_resource_id(mesh, 0U));
    assert(root_id != runtime::runtime_virtual_geometry_variant_resource_id(mesh, 1U));

    const auto root_variant = runtime::build_runtime_virtual_geometry_variant_asset(asset, 0U);
    assert(root_variant);
    assert(root_variant.asset->version == render::mesh_asset_format_version_v3);
    assert(root_variant.asset->vertices == asset.virtual_geometry_proxies.proxies[0].vertices);
    assert(root_variant.asset->indices == asset.virtual_geometry_proxies.proxies[0].indices);
    assert(root_variant.asset->meshlet_geometry.meshlets.empty());
    assert(root_variant.asset->virtual_geometry_proxies.hierarchy.nodes.empty());

    const auto leaf_variant = runtime::build_runtime_virtual_geometry_variant_asset(asset, 2U);
    assert(leaf_variant);
    assert(leaf_variant.asset->vertices.size() == 3U);
    assert(leaf_variant.asset->indices == std::vector<std::uint32_t>({0U, 1U, 2U}));
    assert(leaf_variant.asset->vertices[0] == asset.vertices[3]);
    assert(leaf_variant.asset->vertices[2] == asset.vertices[5]);

    const auto combined = runtime::build_runtime_virtual_geometry_buffer_plan(asset);
    assert(combined);
    assert(combined.plan->vertices.size() == asset.vertices.size() + 3U);
    assert(combined.plan->indices.size() == asset.indices.size() + 3U);

    const auto root_draw = runtime::runtime_virtual_geometry_draw_batch(asset, 0U);
    assert(root_draw.has_value());
    assert(root_draw->index_count == 3U);
    assert(root_draw->first_index == 6U);
    assert(root_draw->vertex_offset == 6);
    const auto leaf_draw = runtime::runtime_virtual_geometry_draw_batch(asset, 2U);
    assert(leaf_draw.has_value());
    assert(leaf_draw->index_count == 3U);
    assert(leaf_draw->first_index == 3U);
    assert(leaf_draw->vertex_offset == 0);

    const auto frustum = runtime::runtime_visibility_frustum(
        {0.0F, 0.0F, -1.0F},
        {0.0F, 1.0F, 0.0F},
        1.0471975512F,
        1.0F,
        0.1F,
        1000.0F
    );
    assert(runtime::valid(frustum));
    const std::vector<runtime::RenderMeshInstance> input{
        virtual_geometry_test_instance(mesh, 0x31U, -10.0),
        virtual_geometry_test_instance(mesh, 0x32U, -500.0),
    };
    const std::vector<runtime::CookedMeshRecord> meshes{record};
    const auto selected = runtime::select_runtime_virtual_geometry_instances(
        input,
        meshes,
        frustum,
        world::GlobalPosition{},
        1.0471975512F,
        1080.0F,
        5.0F
    );
    assert(selected);
    assert(selected.instances.size() == 3U);
    assert(selected.instances[0].entity == input[0].entity);
    assert(selected.instances[1].entity == input[0].entity);
    assert(selected.instances[0].geometry_node_index == 1U);
    assert(selected.instances[1].geometry_node_index == 2U);
    assert(selected.instances[2].entity == input[1].entity);
    assert(selected.instances[2].geometry_node_index == 0U);

    auto malformed = asset;
    malformed.virtual_geometry_proxies.proxies.clear();
    assert(!runtime::build_runtime_virtual_geometry_variant_asset(malformed, 0U));
    assert(!runtime::runtime_virtual_geometry_draw_batch(malformed, 0U));

    render::MeshAsset legacy = asset;
    legacy.virtual_geometry_proxies = {};
    const std::vector<runtime::CookedMeshRecord> legacy_meshes{{.resource_id = mesh, .asset = legacy}};
    const auto legacy_selected = runtime::select_runtime_virtual_geometry_instances(
        std::span<const runtime::RenderMeshInstance>{input.data(), 1U},
        legacy_meshes,
        frustum,
        world::GlobalPosition{},
        1.0471975512F,
        1080.0F,
        5.0F
    );
    assert(legacy_selected);
    assert(legacy_selected.instances.size() == 1U);
    assert(legacy_selected.instances[0].geometry_node_index == render::virtual_geometry_invalid_node);

    return true;
}

} // namespace
