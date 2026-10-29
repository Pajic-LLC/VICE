#include <vice/presentation/system.hpp>
#include <vice/tools/project_workflow.hpp>

#include <vice/tools/project_environment_cook.hpp>
#include <vice/tools/project_mesh_cook.hpp>
#include <vice/tools/presentation_cooker.hpp>
#include <vice/tools/project_render_material_cook.hpp>
#include <vice/tools/project_texture_cook.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace vice::tools {
namespace {

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(value.data()),
        value.size()
    };
}

[[nodiscard]] std::optional<std::string> read_text(
    const std::filesystem::path& path,
    std::string& diagnostics
) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        diagnostics = "Could not read startup World `" + path_text(path) + "`.";
        return std::nullopt;
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.eof() && input.fail()) {
        diagnostics = "Could not finish reading startup World `" + path_text(path) + "`.";
        return std::nullopt;
    }
    return contents.str();
}

[[nodiscard]] ProjectAssetCookResult fail(std::string diagnostics) {
    ProjectAssetCookResult result;
    result.diagnostics = std::move(diagnostics);
    return result;
}

} // namespace

ProjectAssetCookResult cook_project_assets(const project::Workspace& workspace) {
    const auto& source_path = workspace.startup_world_source;
    const auto& output_path = workspace.cooked_startup_world;
    if (source_path.empty()) {
        return fail("Project startup World source path is empty.");
    }
    if (output_path.empty()) {
        return fail("Project cooked startup World path is empty.");
    }

    std::string diagnostics;
    const auto source = read_text(source_path, diagnostics);
    if (!source.has_value()) {
        return fail(std::move(diagnostics));
    }

    const auto parsed = world_cooker::parse_source_manifest(*source);
    if (!parsed.snapshot.has_value()) {
        return fail(
            "Could not cook startup World `" + path_text(source_path) + "`: " + parsed.diagnostics
        );
    }

    std::error_code error;
    const auto parent = output_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return fail(
                "Could not create project cooked asset directory `" + path_text(parent) + "`: " +
                error.message()
            );
        }
    }

    std::string save_diagnostics;
    if (!world::save_world_snapshot(output_path, *parsed.snapshot, &save_diagnostics)) {
        return fail(
            "Could not write cooked startup World `" + path_text(output_path) + "`: " + save_diagnostics
        );
    }

    const auto presentation_cook=cook_project_presentation(workspace);
    if(!presentation_cook)return fail(presentation_cook.diagnostics);
    const auto mesh_cook = cook_project_meshes(workspace);
    if (!mesh_cook) return fail(mesh_cook.diagnostics);

    const auto texture_cook = cook_project_textures(workspace);
    if (!texture_cook) return fail(texture_cook.diagnostics);

    const auto environment_cook = cook_project_environments(workspace);
    if (!environment_cook) return fail(environment_cook.diagnostics);

    const auto texture_ids = cooked_texture_ids(texture_cook);
    const auto material_cook = cook_project_render_materials(workspace, texture_ids);
    if (!material_cook) return fail(material_cook.diagnostics);

    world::World presentation_world;
    if(!presentation_world.restore(*parsed.snapshot))return fail("Could not restore presentation validation World.");
    presentation::System presentation;
    if(!presentation.reset(presentation_world,workspace.cooked_root,&diagnostics))return fail("Presentation validation: "+diagnostics);

    ProjectAssetCookResult result;
    result.success = true;
    result.cooked_startup_world = output_path;
    result.entity_count = parsed.snapshot->entities.size();
    result.region_count = parsed.snapshot->regions.size();
    result.mesh_count = mesh_cook.meshes.size();
    result.texture_count = texture_cook.textures.size();
    result.environment_count = environment_cook.environments.size();
    result.material_count = material_cook.materials.size();
    return result;
}

} // namespace vice::tools
