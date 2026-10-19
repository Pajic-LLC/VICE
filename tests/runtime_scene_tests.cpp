#include <vice/runtime/scene.hpp>
#include <vice/tools/mesh_cooker.hpp>

#include <cmath>
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

[[nodiscard]] bool matrix_differs(
    const vice::math::Mat4& left,
    const vice::math::Mat4& right
) noexcept {
    for (std::size_t index = 0; index < left.values.size(); ++index) {
        if (std::abs(left.values[index] - right.values[index]) > 0.0001F) return true;
    }
    return false;
}

[[nodiscard]] bool matrix_matches(
    const vice::math::Mat4& left,
    const vice::math::Mat4& right,
    const float epsilon = 0.000001F
) noexcept {
    for (std::size_t index = 0; index < left.values.size(); ++index) {
        if (std::abs(left.values[index] - right.values[index]) > epsilon) return false;
    }
    return true;
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

    require(!runtime::primary_camera_view(world_state, 16.0F / 9.0F).has_value(), "legacy World has no authored camera view");

    const auto camera_id = world::PersistentId::parse("52000000-0000-4000-8000-000000000003");
    require(camera_id.has_value(), "camera entity ID parse");
    const auto camera_entity = world_state.create_entity(*camera_id);
    require(camera_entity.has_value(), "camera entity creates");
    require(
        world_state.set_spatial_state(
            *camera_entity,
            world::SpatialState{
                .global_position = {0.0, 2.0, 8.0},
                .region = region.id,
            }
        ),
        "camera spatial state"
    );
    require(
        world_state.set_camera(
            *camera_entity,
            world::Camera{
                .projection = world::CameraProjection::perspective,
                .vertical_fov_radians = 1.0471975512F,
                .near_plane = 0.1F,
                .far_plane = 500.0F,
                .primary = true,
            }
        ),
        "primary camera authored"
    );

    const auto first_camera = runtime::primary_camera_view(world_state, 16.0F / 9.0F);
    require(first_camera.has_value(), "runtime resolves authored primary camera");
    require(first_camera->entity == *camera_id, "runtime camera identity");
    require(first_camera->camera.primary, "runtime camera retains primary flag");
    require(first_camera->aspect_ratio == 16.0F / 9.0F, "runtime camera retains aspect ratio");
    require(
        !runtime::directional_shadow_view(
            *first_camera, runtime::collect_runtime_lights(world_state)
        ).has_value(),
        "World without directional light has no shadow view"
    );

    require(
        world_state.set_spatial_state(
            *camera_entity,
            world::SpatialState{
                .global_position = {3.0, 4.0, 9.0},
                .region = region.id,
            }
        ),
        "gameplay moves camera entity"
    );
    const auto moved_camera = runtime::primary_camera_view(world_state, 16.0F / 9.0F);
    require(moved_camera.has_value(), "runtime camera remains after movement");
    require(
        matrix_differs(first_camera->view_projection, moved_camera->view_projection),
        "current World camera position drives view matrix"
    );

    require(
        world_state.set_authored_transform(
            *camera_entity,
            world::AuthoredTransform{
                .rotation = {0.0F, 0.25881904F, 0.0F, 0.96592583F},
                .scale = {1.0F, 1.0F, 1.0F},
            }
        ),
        "gameplay rotates camera entity"
    );
    const auto rotated_camera = runtime::primary_camera_view(world_state, 16.0F / 9.0F);
    require(rotated_camera.has_value(), "runtime camera remains after rotation");
    require(
        matrix_differs(moved_camera->view_projection, rotated_camera->view_projection),
        "current authored camera rotation drives view matrix"
    );

    const auto light_id = world::PersistentId::parse("52000000-0000-4000-8000-000000000004");
    require(light_id.has_value(), "shadow light ID parse");
    const auto light_entity = world_state.create_entity(*light_id);
    require(light_entity.has_value(), "shadow light entity creates");
    require(
        world_state.set_spatial_state(
            *light_entity,
            world::SpatialState{
                .global_position = {0.0, 10.0, 0.0},
                .region = region.id,
            }
        ),
        "shadow light spatial state"
    );
    require(
        world_state.set_light(
            *light_entity,
            world::Light{
                .type = world::LightType::directional,
                .color = {1.0F, 1.0F, 1.0F},
                .intensity = 3.0F,
                .range = 0.0F,
                .enabled = true,
            }
        ),
        "directional shadow light authored"
    );
    auto shadow_lights = runtime::collect_runtime_lights(world_state);
    const auto first_shadow = runtime::directional_shadow_view(*rotated_camera, shadow_lights);
    require(first_shadow.has_value(), "directional shadow view resolves");
    require(first_shadow->light_entity == *light_id, "shadow view keeps light identity");
    require(first_shadow->light_index == 0U, "first directional light is selected deterministically");
    const auto splits = runtime::runtime_shadow_cascade_splits(*rotated_camera);
    require(splits[0] > rotated_camera->camera.near_plane, "first cascade split follows camera near plane");
    require(splits[0] < splits[1] && splits[1] < splits[2] && splits[2] < splits[3], "cascade splits increase");
    require(splits[3] <= runtime::runtime_shadow_distance, "cascade distance is bounded");
    const auto interior_blend = runtime::runtime_shadow_cascade_blend(splits[0] * 0.5F, splits);
    require(interior_blend.primary == 0U, "interior depth selects first cascade");
    require(interior_blend.secondary == 0U, "interior depth does not double-sample");
    require(interior_blend.secondary_weight == 0.0F, "interior depth has zero blend weight");
    const float first_transition_start = splits[0] * (1.0F - runtime::runtime_shadow_transition_fraction);
    const auto transition_blend = runtime::runtime_shadow_cascade_blend(
        first_transition_start + (splits[0] - first_transition_start) * 0.5F,
        splits
    );
    require(transition_blend.primary == 0U && transition_blend.secondary == 1U,
        "first cascade transition blends into second cascade");
    require(std::abs(transition_blend.secondary_weight - 0.5F) < 0.001F,
        "cascade transition weight is continuous");
    const auto boundary_blend = runtime::runtime_shadow_cascade_blend(splits[0], splits);
    require(boundary_blend.primary == 0U && boundary_blend.secondary == 1U,
        "split boundary retains adjacent cascade pair");
    require(std::abs(boundary_blend.secondary_weight - 1.0F) < 0.001F,
        "split boundary resolves fully to next cascade");
    const auto final_blend = runtime::runtime_shadow_cascade_blend(splits[3], splits);
    require(final_blend.primary == 3U && final_blend.secondary == 3U,
        "final cascade does not request a nonexistent neighbor");
    for (std::size_t cascade_index = 0; cascade_index < first_shadow->cascades.size(); ++cascade_index) {
        const auto& cascade = first_shadow->cascades[cascade_index];
        require(cascade.split_far == splits[cascade_index], "cascade stores split far distance");
        require(cascade.split_far > cascade.split_near, "cascade split range is valid");
        if (cascade_index > 0U) {
            require(
                cascade.split_near < splits[cascade_index - 1U],
                "adjacent cascade overlaps the transition band"
            );
        }
        for (const float value : cascade.view_projection.values) {
            require(std::isfinite(value), "cascade shadow matrix is finite");
        }
    }

    require(
        world_state.set_authored_transform(
            *light_entity,
            world::AuthoredTransform{
                .rotation = {0.0F, 0.70710678F, 0.0F, 0.70710678F},
                .scale = {1.0F, 1.0F, 1.0F},
            }
        ),
        "shadow light rotation updates"
    );
    shadow_lights = runtime::collect_runtime_lights(world_state);
    const auto rotated_shadow = runtime::directional_shadow_view(*rotated_camera, shadow_lights);
    require(rotated_shadow.has_value(), "rotated directional shadow view resolves");
    require(
        matrix_differs(
            first_shadow->cascades.front().view_projection,
            rotated_shadow->cascades.front().view_projection
        ),
        "authored directional rotation drives shadow view"
    );

    runtime::RuntimeCameraView stable_camera{
        .camera = world::Camera{
            .projection = world::CameraProjection::perspective,
            .vertical_fov_radians = 1.0471975512F,
            .near_plane = 0.1F,
            .far_plane = 500.0F,
            .primary = true,
        },
        .position = {0.0F, 2.0F, 8.0F},
        .forward = {0.0F, 0.0F, -1.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .aspect_ratio = 16.0F / 9.0F,
    };
    auto stable_lights = shadow_lights;
    stable_lights.front().direction = {0.0F, -1.0F, 0.0F};
    const auto stable_shadow = runtime::directional_shadow_view(stable_camera, stable_lights);
    require(stable_shadow.has_value(), "stable shadow view resolves");
    stable_camera.position.x += 0.001F;
    const auto sub_texel_shadow = runtime::directional_shadow_view(stable_camera, stable_lights);
    require(sub_texel_shadow.has_value(), "sub-texel moved shadow view resolves");
    for (std::size_t cascade_index = 0; cascade_index < runtime::runtime_shadow_cascade_count; ++cascade_index) {
        require(
            matrix_matches(
                stable_shadow->cascades[cascade_index].view_projection,
                sub_texel_shadow->cascades[cascade_index].view_projection
            ),
            "texel snapping stabilizes sub-texel camera translation"
        );
    }

    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
}
