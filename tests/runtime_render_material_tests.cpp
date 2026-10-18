#include <vice/render/material_asset.hpp>
#include <vice/runtime/scene.hpp>
#include <vice/world/world.hpp>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>

namespace {

void write_bytes(const std::filesystem::path& path, const std::span<const std::byte> bytes) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    assert(output);
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    assert(output);
}

vice::world::PersistentId required_id(const char* text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    assert(parsed.has_value());
    return *parsed;
}

} // namespace

int main() {
    using namespace vice;

    const auto root = std::filesystem::temp_directory_path() / "vice_runtime_render_material_tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    assert(!error);

    const auto material_id = required_id("70000000-0000-4000-8000-000000000001");
    render::RenderMaterialAsset asset;
    std::copy(material_id.bytes().begin(), material_id.bytes().end(), asset.stable_id.begin());
    asset.source_fingerprint = 42U;
    asset.base_color = {0.2F, 0.4F, 0.8F, 1.0F};
    asset.vertex_shader_manifest = "surface.shaders/vertex/shader.vice.json";
    asset.fragment_shader_manifest = "surface.shaders/fragment/shader.vice.json";
    const auto encoded = render::encode_render_material_asset(asset);
    assert(encoded);

    const auto first = root / "first.vmatc";
    write_bytes(first, encoded.bytes);

    runtime::CookedRenderMaterialCatalog catalog;
    assert(catalog.load(root));
    assert(catalog.diagnostics().empty());
    assert(catalog.records().size() == 1U);
    const world::ResourceId material_resource{material_id};
    const auto* record = catalog.find(material_resource);
    assert(record != nullptr);
    assert(record->asset.base_color.blue == 0.8F);

    const auto duplicate = root / "duplicate.vmatc";
    write_bytes(duplicate, encoded.bytes);
    assert(!catalog.load(root));
    assert(catalog.diagnostics().find(material_id.to_string()) != std::string::npos);
    std::filesystem::remove(duplicate, error);
    assert(!error);
    assert(catalog.load(root));

    world::World world_state;
    const world::RegionDesc region{
        .id = world::RegionId{1U},
        .simulation_island = world::SimulationIslandId{1U},
        .origin = {},
        .streaming_cell = {},
        .fidelity = world::RegionFidelity::full,
        .bounds = world::RegionBounds{
            .min = {-10.0F, -10.0F, -10.0F},
            .max = {10.0F, 10.0F, 10.0F},
        },
    };
    assert(world_state.register_region(region));
    const world::ResourceId mesh_resource{
        required_id("50000000-0000-4000-8000-000000000001")
    };
    assert(world_state.create_resource(mesh_resource).has_value());
    assert(world_state.create_resource(material_resource).has_value());

    const auto entity = world_state.create_entity(
        required_id("10000000-0000-4000-8000-000000000001")
    );
    assert(entity.has_value());
    assert(world_state.set_spatial_state(
        *entity,
        world::SpatialState{
            .global_position = {},
            .region = region.id,
        }
    ));
    assert(world_state.set_resource_reference(
        *entity,
        world::ResourceReference{
            .role = world::ResourceRole::render_mesh,
            .resource = mesh_resource,
        }
    ));
    assert(world_state.set_resource_reference(
        *entity,
        world::ResourceReference{
            .role = world::ResourceRole::render_material,
            .resource = material_resource,
        }
    ));

    const auto instances = runtime::collect_render_mesh_instances(world_state);
    assert(instances.size() == 1U);
    assert(instances.front().mesh == mesh_resource);
    assert(instances.front().material == std::optional{material_resource});

    std::filesystem::remove_all(root, error);
    assert(!error);
    return 0;
}
