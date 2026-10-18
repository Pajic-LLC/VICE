from pathlib import Path


def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)

cmake_path = Path("apps/vice_editor/CMakeLists.txt")
cmake = cmake_path.read_text()
cmake = once(
    cmake,
    "   TARGET vice_render_material_cooker_lib AND\n   TARGET vice_shader_cooker_lib)",
    "   TARGET vice_render_material_cooker_lib AND\n   TARGET vice_shader_cooker_lib AND\n   TARGET vice_texture_cooker_lib)",
    "editor build condition",
)
cmake = once(
    cmake,
    "      src/project_material_cook.cpp\n      src/project_mesh_cook.cpp\n",
    "      src/project_material_cook.cpp\n      src/project_mesh_cook.cpp\n      src/project_texture_cook.cpp\n",
    "editor texture source",
)
cmake = once(
    cmake,
    "      VICE::ShaderCooker\n      VICE::World\n",
    "      VICE::ShaderCooker\n      VICE::TextureCooker\n      VICE::World\n",
    "editor texture link",
)
cmake_path.write_text(cmake)

build_path = Path("apps/vice_editor/src/project_build.cpp")
build = build_path.read_text()
build = once(
    build,
    "#include <vice/editor/project_mesh_cook.hpp>\n",
    "#include <vice/editor/project_mesh_cook.hpp>\n#include <vice/editor/project_texture_cook.hpp>\n",
    "project texture include",
)
build = once(
    build,
    "    const auto material_cook = cook_project_render_materials(session);\n    if (!material_cook) {\n        return fail_build(material_cook.diagnostics);\n    }\n",
    "    const auto texture_cook = cook_project_textures(session);\n    if (!texture_cook) {\n        return fail_build(texture_cook.diagnostics);\n    }\n    const auto texture_ids = cooked_texture_ids(texture_cook);\n    const auto material_cook = cook_project_render_materials(session, texture_ids);\n    if (!material_cook) {\n        return fail_build(material_cook.diagnostics);\n    }\n",
    "texture before material cook",
)
build = once(
    build,
    "    result.mesh_count = mesh_cook.meshes.size();\n    result.material_count = material_cook.materials.size();\n",
    "    result.mesh_count = mesh_cook.meshes.size();\n    result.texture_count = texture_cook.textures.size();\n    result.material_count = material_cook.materials.size();\n",
    "texture build count",
)

anchor = '''[[nodiscard]] bool stage_project_render_materials(
'''
texture_stage = r'''[[nodiscard]] bool stage_project_textures(
    const ProjectSession& session,
    const std::filesystem::path& staged_project_content_root,
    std::string& diagnostics
) {
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator{
        session.asset_root(),
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        diagnostics = "Could not enumerate project textures for staging: " + error.message();
        return false;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            diagnostics = "Could not enumerate project textures for staging: " + error.message();
            return false;
        }
        if (!iterator->is_regular_file() || iterator->path().extension() != ".vtex") continue;

        auto relative = std::filesystem::relative(iterator->path(), session.asset_root(), error);
        if (error) {
            diagnostics = "Could not resolve project texture path for staging: " + error.message();
            return false;
        }
        relative.replace_extension(".vtexc");
        const auto source = (session.cooked_root() / relative).lexically_normal();
        const auto destination = (staged_project_content_root / relative).lexically_normal();
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) {
            diagnostics = "Could not create staged texture directory `" +
                path_text(destination.parent_path()) + "`: " + error.message();
            return false;
        }
        std::filesystem::copy_file(
            source,
            destination,
            std::filesystem::copy_options::overwrite_existing,
            error
        );
        if (error) {
            diagnostics = "Could not stage cooked texture `" + path_text(source) + "`: " + error.message();
            return false;
        }
    }

    diagnostics.clear();
    return true;
}

'''
build = once(build, anchor, texture_stage + anchor, "texture staging helper")

build = once(
    build,
    '''    std::string material_stage_diagnostics;
''',
    '''    std::string texture_stage_diagnostics;
    if (!stage_project_textures(session, staged_project_content_root, texture_stage_diagnostics)) {
        cleanup_staging();
        return fail_build(std::move(texture_stage_diagnostics));
    }
    std::string material_stage_diagnostics;
''',
    "texture staging invocation",
)
build_path.write_text(build)
