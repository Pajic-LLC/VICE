#include <vice/editor/project_environment_cook.hpp>

#include <vice/tools/environment_cooker.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace vice::editor {
namespace {

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) return {};
    std::ostringstream contents;
    contents << input.rdbuf();
    return input.eof() || !input.fail() ? contents.str() : std::string{};
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) return {};
    const std::vector<char> raw{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    if (!input.eof() && input.fail()) return {};
    const auto bytes = std::as_bytes(std::span<const char>{raw});
    return {bytes.begin(), bytes.end()};
}

[[nodiscard]] bool inside_root(
    const std::filesystem::path& candidate,
    const std::filesystem::path& root
) {
    std::error_code error;
    const auto relative = std::filesystem::relative(candidate, root, error);
    if (error || relative.empty()) return false;
    for (const auto& part : relative) {
        if (part == "..") return false;
    }
    return true;
}

} // namespace

ProjectEnvironmentCookResult cook_project_environments(const ProjectSession& session) {
    ProjectEnvironmentCookResult result;
    const auto& asset_root = session.asset_root();
    const auto& cooked_root = session.cooked_root();
    if (asset_root.empty() || cooked_root.empty()) {
        result.diagnostics = "Project environment roots are unavailable.";
        return result;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(asset_root, error) || error) {
        result.diagnostics = error
            ? "Could not inspect project asset root: " + error.message()
            : "Project asset root does not exist: " + path_text(asset_root);
        return result;
    }

    std::filesystem::recursive_directory_iterator iterator{
        asset_root,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        result.diagnostics = "Could not enumerate project environments: " + error.message();
        return result;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            result.diagnostics = "Could not enumerate project environments: " + error.message();
            return result;
        }
        if (!iterator->is_regular_file() || iterator->path().extension() != ".venv") continue;
        if (!result.environments.empty()) {
            result.diagnostics = "VICE currently supports one global environment source per project.";
            return result;
        }

        const auto source_path = iterator->path();
        const std::string source_text = read_text(source_path);
        if (source_text.empty()) {
            result.diagnostics = "Could not read project environment source `" + path_text(source_path) + "`.";
            return result;
        }
        const auto parsed = tools::environment_cooker::parse_source_manifest(source_text);
        if (!parsed.source.has_value()) {
            result.diagnostics = "Could not parse project environment `" + path_text(source_path) + "`: " +
                parsed.diagnostics;
            return result;
        }

        const auto image_path = (asset_root / parsed.source->image_source).lexically_normal();
        if (!inside_root(image_path, asset_root) || !std::filesystem::is_regular_file(image_path, error) || error) {
            result.diagnostics = error
                ? "Could not inspect environment image `" + path_text(image_path) + "`: " + error.message()
                : "Environment image is outside the project asset root or missing: " + path_text(image_path);
            return result;
        }
        const auto image_bytes = read_bytes(image_path);
        if (image_bytes.empty()) {
            result.diagnostics = "Could not read environment image `" + path_text(image_path) + "`.";
            return result;
        }

        const auto cooked = tools::environment_cooker::cook(*parsed.source, image_bytes);
        if (!cooked.success) {
            result.diagnostics = "Could not cook project environment `" + path_text(source_path) + "`: " +
                cooked.diagnostics;
            return result;
        }
        const auto decoded = render::decode_environment_asset(cooked.bytes);
        if (!decoded.asset.has_value()) {
            result.diagnostics = "Cooked project environment failed validation: " + decoded.diagnostics;
            return result;
        }

        auto relative = std::filesystem::relative(source_path, asset_root, error);
        if (error) {
            result.diagnostics = "Could not resolve project environment path: " + error.message();
            return result;
        }
        relative.replace_extension(".venvc");
        const auto output_path = (cooked_root / relative).lexically_normal();
        std::string write_diagnostics;
        if (!tools::environment_cooker::write_asset(output_path, cooked.bytes, &write_diagnostics)) {
            result.diagnostics = "Could not write cooked project environment `" + path_text(output_path) + "`: " +
                write_diagnostics;
            return result;
        }
        result.environments.push_back(CookedProjectEnvironment{
            .source = source_path,
            .cooked = output_path,
        });
    }

    result.success = true;
    return result;
}

} // namespace vice::editor
