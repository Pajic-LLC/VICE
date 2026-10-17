#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vice::platform {

enum class PathDialogStatus {
    selected,
    cancelled,
    failed,
};

struct PathDialogResult {
    PathDialogStatus status = PathDialogStatus::cancelled;
    std::filesystem::path path;
    std::string diagnostics;

    [[nodiscard]] bool selected() const noexcept {
        return status == PathDialogStatus::selected && !path.empty();
    }
};

struct FileDialogFilter {
    std::string label;
    std::vector<std::string> extensions;
};

struct OpenFileDialogDesc {
    std::string title;
    std::filesystem::path initial_directory;
    std::vector<FileDialogFilter> filters;
};

struct SaveFileDialogDesc {
    std::string title;
    std::filesystem::path initial_directory;
    std::string suggested_name;
    std::vector<FileDialogFilter> filters;
};

struct FolderDialogDesc {
    std::string title;
    std::filesystem::path initial_directory;
};

[[nodiscard]] PathDialogResult open_file_dialog(const OpenFileDialogDesc& desc);
[[nodiscard]] PathDialogResult save_file_dialog(const SaveFileDialogDesc& desc);
[[nodiscard]] PathDialogResult select_folder_dialog(const FolderDialogDesc& desc);

} // namespace vice::platform
