#pragma once

#include <vice/platform/event.hpp>
#include <vice/platform/window.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vice::platform {

struct PlatformPaths {
    std::filesystem::path executable;
    std::filesystem::path user_data;
    std::filesystem::path temporary;
};

struct OpenFileDialogDesc {
    std::string title;
    std::string extension;
};

struct FolderDialogDesc {
    std::string title;
};

struct PathDialogResult {
    std::optional<std::filesystem::path> path;
    std::string diagnostics;

    [[nodiscard]] bool cancelled() const noexcept {
        return !path.has_value() && diagnostics.empty();
    }
};

class PlatformServices {
public:
    virtual ~PlatformServices() = default;

    [[nodiscard]] static std::unique_ptr<PlatformServices> create();

    [[nodiscard]] virtual std::unique_ptr<Window> create_window(const WindowDesc& desc) = 0;
    virtual bool poll_event(Event& event) = 0;
    virtual void request_quit() = 0;

    [[nodiscard]] virtual PlatformPaths paths() const = 0;

    [[nodiscard]] PathDialogResult open_file_dialog(const OpenFileDialogDesc& desc);
    [[nodiscard]] PathDialogResult select_folder_dialog(const FolderDialogDesc& desc);
};

} // namespace vice::platform
