#include <vice/runtime/scene.hpp>
#include <vice/tools/mesh_cooker.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "runtime scene test failed: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    using namespace vice;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) / "vice_runtime_scene_test_52000000";
    require(!error, "temporary directory path");
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root / "content" / "meshes", error);
    require(!error, "runtime content directory");

    const auto mesh_id = world::PersistentId::parse("52000000-0000-4000-8000-000000000001");
    require(mesh_id.has_value(), "mesh stable ID parse");

    tools::mesh_cooker::MeshSource source;
    source.name = "runtime triangle";
    source.stable_id = mesh_id->bytes();
    source.vertices = {
        render::MeshVertex{{-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}},
        render::MeshVertex{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}},
        render::MeshVertex{{0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}},
    };
    source.indices = {0U, 1U, 2U};
    const auto cooked = tools::mesh_cooker::cook(source);
    require(cooked.success, "test mesh cooks");

    const auto first_mesh = root / "content" / "meshes" / "triangle.vmeshc";
    std::string diagnostics;
    require(
        tools::mesh_cooker::write_asset(first_mesh, cooked.bytes, &diagnostics),
        "test mesh writes"
    );
    require(diagnostics.empty(), "test mesh write diagnostics");

    runtime::CookedMeshCatalog catalog;
    require(catalog.load(root / "content"), "runtime mesh catalog loads");
    require(catalog.diagnostics().empty(), "runtime mesh catalog diagnostics");
    require(catalog.records().size() == 1U, "runtime mesh catalog count");
    const world::ResourceId resource{*mesh_id};
    const auto* record = catalog.find(resource);
    require(record != nullptr, "runtime mesh catalog resolves stable ID");
    require(record->asset.indices.size() == 3U, "runtime mesh catalog keeps mesh data");

    const auto duplicate_mesh = root / "content" / "meshes" / "duplicate.vmeshc";
    require(
        tools::mesh_cooker::write_asset(duplicate_mesh, cooked.bytes, &diagnostics),
        "duplicate mesh writes"
    );
    require(!catalog.load(root / "content"), "duplicate stable ID rejected");
    require(
        catalog.diagnostics().find(mesh_id->to_string()) != std::string::npos,
        "duplicate stable ID diagnostic identifies resource"
    );
    std::filesystem::remove(duplicate_mesh, error);
    require(!error, "duplicate mesh removal");
    require(catalog.load(root / "content"), "catalog reload after duplicate removal");

    world::World world_state;
    world::RegionDesc region;
    region.id = world::RegionId{1U};
    region.simulation_island = world::SimulationIslandId{1U};
    region.fidelity = world::RegionFidelity::full;
    region.bounds = world::RegionBounds{
        .min = {-100.0F, -100.0F, -100.0F},
        .max = {100.0F, 100.0F, 100.0F},
    };
    require(world_state.register_region(region), "runtime scene region registers");
    require(world_state.create_resource(resource).has_value(), "runtime mesh resource registers");

    const auto entity_id = world::PersistentId::parse("52000000-0000-4000-8000-000000000002");
    require(entity_id.has_value(), "runtime entity ID parse");
    const auto entity = world_state.create_entity(*entity_id);
    require(entity.has_value(), "runtime scene entity creates");
    require(
        world_state.set_spatial_state(
            *entity,
            world::SpatialState{
                .global_position = {2.0, 3.0, 4.0},
                .region = region.id,
            }
        ),
        "runtime scene spatial state"
    );
    require(
        world_state.set_authored_transform(
            *entity,
            world::AuthoredTransform{
                .rotation = {},
                .scale = {2.0F, 3.0F, 4.0F},
            }
        ),
        "runtime scene authored transform"
    );
    require(
        world_state.set_resource_reference(
            *entity,
            world::ResourceReference{
                .role = world::ResourceRole::render_mesh,
                .resource = resource,
            }
        ),
        "runtime scene mesh reference"
    );

    auto instances = runtime::collect_render_mesh_instances(world_state);
    require(instances.size() == 1U, "one runtime render instance");
    require(instances.front().entity == *entity_id, "render instance entity identity");
    require(instances.front().mesh == resource, "render instance mesh identity");
    auto model = runtime::runtime_model_matrix(instances.front());
    require(model(0, 0) == 2.0F && model(1, 1) == 3.0F && model(2, 2) == 4.0F, "model scale");
    require(model(0, 3) == 2.0F && model(1, 3) == 3.0F && model(2, 3) == 4.0F, "model position");

    require(
        world_state.set_spatial_state(
            *entity,
            world::SpatialState{
                .global_position = {8.0, 9.0, 10.0},
                .region = region.id,
            }
        ),
        "runtime scene movement update"
    );
    instances = runtime::collect_render_mesh_instances(world_state);
    require(instances.size() == 1U, "render instance remains after movement");
    model = runtime::runtime_model_matrix(instances.front());
    require(model(0, 3) == 8.0F && model(1, 3) == 9.0F && model(2, 3) == 10.0F, "current World position drives model");

    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
}
