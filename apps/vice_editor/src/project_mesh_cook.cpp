#include <vice/editor/project_mesh_cook.hpp>

#include <vice/tools/mesh_cooker.hpp>

#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace vice::editor {
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
        diagnostics = "Could not read mesh source `" + path_text(path) + "`.";
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.eof() && input.fail()) {
        diagnostics = "Could not finish reading mesh source `" + path_text(path) + "`.";
        return std::nullopt;
    }
    return contents.str();
}

} // namespace

ProjectMeshCookResult cook_project_meshes(const ProjectSession& session) {
    ProjectMeshCookResult result;
    const auto& source_root = session.asset_root();
    const auto& cooked_root = session.cooked_root();

    std::error_code error;
    if (source_root.empty() || !std::filesystem::is_directory(source_root, error)) {
        result.diagnostics = error
            ? "Could not inspect project asset root: " + error.message()
            : "Project asset root is not available.";
        return result;
    }

    std::filesystem::recursive_directory_iterator iterator{
        source_root,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        result.diagnostics = "Could not enumerate project assets: " + error.message();
        return result;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            result.diagnostics = "Could not enumerate project assets: " + error.message();
            return result;
        }
        if (!iterator->is_regular_file() || iterator->path().extension() != ".vmesh") continue;

        const auto relative = std::filesystem::relative(iterator->path(), source_root, error);
        if (error) {
            result.diagnostics = "Could not resolve project mesh path: " + error.message();
            return result;
        }

        std::string diagnostics;
        const auto source = read_text(iterator->path(), diagnostics);
        if (!source.has_value()) {
            result.diagnostics = std::move(diagnostics);
            return result;
        }

        const auto parsed = tools::mesh_cooker::parse_source_manifest(*source);
        if (!parsed.source.has_value()) {
            result.diagnostics = "Could not parse project mesh `" + path_text(iterator->path()) +
                "`: " + parsed.diagnostics;
            return result;
        }
        const auto cooked = tools::mesh_cooker::cook(*parsed.source);
        if (!cooked.success) {
            result.diagnostics = "Could not cook project mesh `" + path_text(iterator->path()) +
                "`: " + cooked.diagnostics;
            return result;
        }

        auto cooked_relative = relative;
        cooked_relative.replace_extension(".vmeshc");
        const auto cooked_path = (cooked_root / cooked_relative).lexically_normal();
        if (!tools::mesh_cooker::write_asset(cooked_path, cooked.bytes, &diagnostics)) {
            result.diagnostics = "Could not write cooked project mesh `" + path_text(cooked_path) +
                "`: " + diagnostics;
            return result;
        }
        result.meshes.push_back(CookedProjectMesh{
            .relative_path = std::move(cooked_relative),
            .cooked_path = cooked_path,
        });
    }

    result.success = true;
    return result;
}

} // namespace vice::editor
