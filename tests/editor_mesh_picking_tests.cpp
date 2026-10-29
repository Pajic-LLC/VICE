#include <vice/editor/asset_index.hpp>
#include <vice/editor/mesh_picking.hpp>
#include <vice/tools/mesh_cooker.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/world.hpp>

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

[[nodiscard]] vice::world::PersistentId parse_id(const char* text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    assert(parsed.has_value());
    return *parsed;
}

void write_mesh(
    const std::filesystem::path& source_path,
    const std::filesystem::path& cooked_path,
    const char* name,
    const char* id
) {
    const std::string source =
        "vice-mesh-source 1\n"
        "name " + std::string{name} + "\n"
        "id " + std::string{id} + "\n"
        "vertex -2 -1 -0.5 1 0 0\n"
        "vertex 2 -1 -0.5 0 1 0\n"
        "vertex 2 1 0.5 0 0 1\n"
        "vertex -2 1 0.5 1 1 1\n"
        "triangle 0 1 2\n"
        "triangle 0 2 3\n";

    {
        std::ofstream output{source_path};
        output << source;
        assert(output.good());
    }

    const auto parsed = vice::tools::mesh_cooker::parse_source_manifest(source);
    assert(parsed.source.has_value());
    const auto cooked = vice::tools::mesh_cooker::cook(*parsed.source);
    assert(cooked.success);
    std::string diagnostics;
    assert(vice::tools::mesh_cooker::write_asset(cooked_path, cooked.bytes, &diagnostics));
    assert(diagnostics.empty());
}

[[nodiscard]] vice::world::EntityId add_mesh_entity(
    vice::world::World& world,
    const vice::world::PersistentId persistent_id,
    const vice::world::ResourceId resource,
    const vice::world::GlobalPosition position,
    const std::optional<vice::world::AuthoredTransform>& transform = std::nullopt
) {
    if (!world.find_resource(resource).has_value()) {
        assert(world.create_resource(resource).has_value());
    }
    const auto entity = world.create_entity(persistent_id);
    assert(entity.has_value());
    assert(world.set_spatial_state(*entity, vice::world::SpatialState{
        .global_position = position,
        .region = vice::world::RegionId{1},
    }));
    if (transform.has_value()) {
        assert(world.set_authored_transform(*entity, *transform));
    }
    assert(world.set_resource_reference(*entity, vice::world::ResourceReference{
        .role = vice::world::ResourceRole::render_mesh,
        .resource = resource,
    }));
    return *entity;
}

} // namespace

int main() {
    using namespace vice;

    const auto root = std::filesystem::temp_directory_path() / "vice_editor_mesh_picking_test";
    const auto source_root = root / "source";
    const auto cooked_root = root / "cooked";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(source_root / "meshes", error);
    assert(!error);
    std::filesystem::create_directories(cooked_root / "meshes", error);
    assert(!error);

    constexpr const char* near_resource_text = "50000000-0000-4000-8000-000000000101";
    constexpr const char* far_resource_text = "50000000-0000-4000-8000-000000000102";
    write_mesh(
        source_root / "meshes" / "near.vmesh",
        cooked_root / "meshes" / "near.vmeshc",
        "near",
        near_resource_text
    );
    write_mesh(
        source_root / "meshes" / "far.vmesh",
        cooked_root / "meshes" / "far.vmeshc",
        "far",
        far_resource_text
    );

    editor::AssetIndex assets;
    assert(assets.refresh(source_root, cooked_root));
    assert(assets.diagnostics().empty());

    const world::ResourceId near_resource{parse_id(near_resource_text)};
    const world::ResourceId far_resource{parse_id(far_resource_text)};
    assert(assets.find_resource(near_resource) != nullptr);
    assert(assets.find_resource(far_resource) != nullptr);
    assert(assets.find_resource(near_resource)->mesh_bounds.has_value());

    world::World world;
    assert(world.register_region(world::RegionDesc{
        .id = world::RegionId{1},
        .simulation_island = world::SimulationIslandId{1},
        .origin = {},
        .streaming_cell = {},
        .fidelity = world::RegionFidelity::full,
        .bounds = world::RegionBounds{
            .min = {-100.0F, -100.0F, -100.0F},
            .max = {100.0F, 100.0F, 100.0F},
        },
    }));

    const auto near_id = parse_id("10000000-0000-4000-8000-000000000101");
    const auto far_id = parse_id("10000000-0000-4000-8000-000000000102");
    add_mesh_entity(world, near_id, near_resource, {0.0, 0.0, 5.0});
    add_mesh_entity(world, far_id, far_resource, {0.0, 0.0, 10.0});

    const auto nearest = editor::pick_render_mesh(
        world,
        assets,
        math::Vec3{0.0F, 0.0F, 0.0F},
        math::Vec3{0.0F, 0.0F, 1.0F}
    );
    assert(nearest.has_value());
    assert(nearest->persistent_id == near_id);
    assert(nearest->resource == near_resource);
    assert(nearest->distance > 4.4F && nearest->distance < 4.6F);

    const world::AuthoredTransform transformed{
        .rotation = {0.0F, 0.70710678F, 0.0F, 0.70710678F},
        .scale = {1.5F, 2.0F, 0.5F},
    };
    const auto transformed_id = parse_id("10000000-0000-4000-8000-000000000103");
    add_mesh_entity(world, transformed_id, near_resource, {6.0, 0.0, 8.0}, transformed);

    const auto transformed_hit = editor::pick_render_mesh(
        world,
        assets,
        math::Vec3{6.0F, 0.0F, 0.0F},
        math::Vec3{0.0F, 0.0F, 1.0F}
    );
    assert(transformed_hit.has_value());
    assert(transformed_hit->persistent_id == transformed_id);
    assert(transformed_hit->distance > 4.9F && transformed_hit->distance < 5.1F);

    const auto transformed_miss = editor::pick_render_mesh(
        world,
        assets,
        math::Vec3{6.0F, 4.5F, 0.0F},
        math::Vec3{0.0F, 0.0F, 1.0F}
    );
    assert(!transformed_miss.has_value());

    const world::ResourceId missing_resource{
        parse_id("50000000-0000-4000-8000-000000000199")
    };
    const auto missing_id = parse_id("10000000-0000-4000-8000-000000000199");
    add_mesh_entity(world, missing_id, missing_resource, {20.0, 0.0, 3.0});
    const auto missing_asset_hit = editor::pick_render_mesh(
        world,
        assets,
        math::Vec3{20.0F, 0.0F, 0.0F},
        math::Vec3{0.0F, 0.0F, 1.0F}
    );
    assert(!missing_asset_hit.has_value());

    assert(!editor::pick_render_mesh(world, assets, {}, {}).has_value());

    const std::array displayed{
        editor::MeshPickGeometry{transformed_id, {{-1, -1, -1}, {1, 1, 1}}, {{30, 0, 5}, {1}}, {}}};
    const auto deformed_hit =
        editor::pick_render_mesh(world, assets, {30, 0, 0}, {0, 0, 1}, displayed);
    assert(deformed_hit && deformed_hit->persistent_id == transformed_id &&
           std::abs(deformed_hit->distance - 4) < 0.001F);
    assert(!editor::pick_render_mesh(world, assets, {30, 0, 0}, {0, 0, 1}));

    std::filesystem::remove_all(root, error);
    return 0;
}
