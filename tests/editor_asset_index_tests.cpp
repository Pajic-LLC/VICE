#include <vice/editor/asset_index.hpp>
#include <vice/tools/mesh_cooker.hpp>
#include <vice/world/persistent_id.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string_view>

int main() {
    using namespace vice;

    const auto root = std::filesystem::temp_directory_path() / "vice_editor_asset_index_test";
    const auto source_root = root / "source";
    const auto cooked_root = root / "cooked";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(source_root / "meshes", error);
    assert(!error);
    std::filesystem::create_directories(source_root / "shaders", error);
    assert(!error);
    std::filesystem::create_directories(cooked_root / "meshes", error);
    assert(!error);

    constexpr std::string_view mesh_source = R"(
vice-mesh-source 1
name indexed_mesh
id 50000000-0000-4000-8000-000000000055
vertex -2 -1 -3 1 0 0
vertex 4 0 2 0 1 0
vertex 0 5 1 0 0 1
triangle 0 1 2
)";
    {
        std::ofstream output{source_root / "meshes" / "indexed_mesh.vmesh"};
        output << mesh_source;
        assert(output.good());
    }
    {
        std::ofstream output{source_root / "shaders" / "basic.hlsl"};
        output << "float4 main() : SV_Target0 { return 1; }\n";
        assert(output.good());
    }

    const auto parsed = tools::mesh_cooker::parse_source_manifest(mesh_source);
    assert(parsed.source.has_value());
    const auto cooked = tools::mesh_cooker::cook(*parsed.source);
    assert(cooked.success);
    std::string diagnostics;
    assert(tools::mesh_cooker::write_asset(
        cooked_root / "meshes" / "indexed_mesh.vmeshc",
        cooked.bytes,
        &diagnostics
    ));
    assert(diagnostics.empty());

    editor::AssetIndex index;
    assert(index.refresh(source_root, cooked_root));
    assert(index.diagnostics().empty());
    assert(index.assets().size() == 2U);

    const auto resource_uuid = world::PersistentId::parse("50000000-0000-4000-8000-000000000055");
    assert(resource_uuid.has_value());
    const world::ResourceId resource_id{*resource_uuid};
    const auto* mesh = index.find_resource(resource_id);
    assert(mesh != nullptr);
    assert(mesh->kind == editor::AssetKind::mesh);
    assert(mesh->relative_path.generic_string() == "meshes/indexed_mesh.vmesh");
    assert(mesh->cooked_available);
    assert(mesh->cooked_valid);
    assert(mesh->instantiable_mesh());
    assert(mesh->format_version == render::mesh_asset_format_version);
    assert(mesh->source_fingerprint == cooked.source_fingerprint);
    assert(mesh->summary == "3 vertices, 1 triangles");
    assert(mesh->mesh_bounds.has_value());
    assert(mesh->mesh_bounds->minimum.x == -2.0F);
    assert(mesh->mesh_bounds->minimum.y == -1.0F);
    assert(mesh->mesh_bounds->minimum.z == -3.0F);
    assert(mesh->mesh_bounds->maximum.x == 4.0F);
    assert(mesh->mesh_bounds->maximum.y == 5.0F);
    assert(mesh->mesh_bounds->maximum.z == 2.0F);

    bool shader_found = false;
    for (const auto& asset : index.assets()) {
        if (asset.kind == editor::AssetKind::shader_source) {
            shader_found = true;
            assert(asset.name == "basic");
            assert(!asset.cooked_available);
            assert(!asset.mesh_bounds.has_value());
        }
    }
    assert(shader_found);

    std::filesystem::remove_all(root, error);
    return 0;
}
