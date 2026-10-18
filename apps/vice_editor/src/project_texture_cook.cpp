#include <vice/editor/project_texture_cook.hpp>

#include <vice/tools/texture_cooker.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <unordered_set>
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

ProjectTextureCookResult cook_project_textures(const ProjectSession& session) {
    ProjectTextureCookResult result;
    const auto& asset_root = session.asset_root();
    const auto& cooked_root = session.cooked_root();
    if (asset_root.empty() || cooked_root.empty()) {
        result.diagnostics = "Project texture roots are unavailable.";
        return result;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(asset_root, error) || error) {
        result.diagnostics = error
            ? "Could not inspect project asset root: " + error.message()
            : "Project asset root does not exist: " + path_text(asset_root);
        return result;
    }

    std::unordered_set<world::ResourceId, world::ResourceIdHash> seen;
    std::filesystem::recursive_directory_iterator iterator{
        asset_root,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        result.diagnostics = "Could not enumerate project textures: " + error.message();
        return result;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            result.diagnostics = "Could not enumerate project textures: " + error.message();
            return result;
        }
        if (!iterator->is_regular_file() || iterator->path().extension() != ".vtex") continue;

        const auto source_path = iterator->path();
        const std::string source_text = read_text(source_path);
        if (source_text.empty()) {
            result.diagnostics = "Could not read project texture source `" + path_text(source_path) + "`.";
            return result;
        }
        const auto parsed = tools::texture_cooker::parse_source_manifest(source_text);
        if (!parsed.source.has_value()) {
            result.diagnostics = "Could not parse project texture `" + path_text(source_path) + "`: " +
                parsed.diagnostics;
            return result;
        }

        const world::ResourceId resource{world::PersistentId{parsed.source->stable_id}};
        if (!resource.valid() || !seen.insert(resource).second) {
            result.diagnostics = "Duplicate or invalid project texture stable ID `" +
                resource.value.to_string() + "`.";
            return result;
        }

        const auto image_path = (asset_root / parsed.source->image_source).lexically_normal();
        if (!inside_root(image_path, asset_root) || !std::filesystem::is_regular_file(image_path, error) || error) {
            result.diagnostics = error
                ? "Could not inspect texture image `" + path_text(image_path) + "`: " + error.message()
                : "Texture image is outside the project asset root or missing: " + path_text(image_path);
            return result;
        }
        const auto image_bytes = read_bytes(image_path);
        if (image_bytes.empty()) {
            result.diagnostics = "Could not read texture image `" + path_text(image_path) + "`.";
            return result;
        }
        const auto cooked = tools::texture_cooker::cook(*parsed.source, image_bytes);
        if (!cooked.success) {
            result.diagnostics = "Could not cook project texture `" + path_text(source_path) + "`: " +
                cooked.diagnostics;
            return result;
        }

        const auto decoded = render::decode_texture_asset(cooked.bytes);
        if (!decoded.asset.has_value()) {
            result.diagnostics = "Cooked project texture failed validation: " + decoded.diagnostics;
            return result;
        }

        auto relative = std::filesystem::relative(source_path, asset_root, error);
        if (error) {
            result.diagnostics = "Could not resolve project texture path: " + error.message();
            return result;
        }
        relative.replace_extension(".vtexc");
        const auto output_path = (cooked_root / relative).lexically_normal();
        std::string write_diagnostics;
        if (!tools::texture_cooker::write_asset(output_path, cooked.bytes, &write_diagnostics)) {
            result.diagnostics = "Could not write cooked project texture `" + path_text(output_path) + "`: " +
                write_diagnostics;
            return result;
        }

        result.textures.push_back(ProjectTextureCookRecord{
            .source = source_path,
            .cooked = output_path,
            .resource_id = resource,
            .width = decoded.asset->width,
            .height = decoded.asset->height,
        });
    }

    std::sort(result.textures.begin(), result.textures.end(), [](const auto& left, const auto& right) {
        return left.source.generic_string() < right.source.generic_string();
    });
    result.success = true;
    return result;
}

std::vector<world::ResourceId> cooked_texture_ids(const ProjectTextureCookResult& result) {
    std::vector<world::ResourceId> ids;
    ids.reserve(result.textures.size());
    for (const auto& texture : result.textures) ids.push_back(texture.resource_id);
    return ids;
}

} // namespace vice::editor
