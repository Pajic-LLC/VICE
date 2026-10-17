#include <vice/project/project.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace vice::project {

bool save_manifest(
    const std::filesystem::path& path,
    const Manifest& manifest,
    std::string* const diagnostics
) {
    if (path.empty()) {
        if (diagnostics != nullptr) *diagnostics = "Project manifest path is empty.";
        return false;
    }

    const std::string text = serialize_manifest(manifest);
    if (text.empty()) {
        if (diagnostics != nullptr) *diagnostics = "Project manifest is not valid.";
        return false;
    }

    std::error_code error;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            if (diagnostics != nullptr) {
                *diagnostics = "Could not create project manifest directory: " + error.message();
            }
            return false;
        }
    }

    const auto temporary = std::filesystem::path{path.string() + ".tmp"};
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        if (!output) {
            if (diagnostics != nullptr) *diagnostics = "Could not create temporary project manifest.";
            return false;
        }
        output << text;
        if (!output) {
            if (diagnostics != nullptr) *diagnostics = "Could not write temporary project manifest.";
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    std::filesystem::copy_file(
        temporary,
        path,
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    std::error_code cleanup_error;
    std::filesystem::remove(temporary, cleanup_error);
    if (error) {
        if (diagnostics != nullptr) *diagnostics = "Could not replace project manifest: " + error.message();
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::project
