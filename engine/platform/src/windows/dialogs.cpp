#include <vice/platform/dialogs.hpp>

#include <windows.h>
#include <shobjidl.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vice::platform {
namespace {

class ComApartment final {
public:
    ComApartment() noexcept
        : result_{CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)},
          owns_{SUCCEEDED(result_)} {}

    ~ComApartment() {
        if (owns_) CoUninitialize();
    }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

    [[nodiscard]] bool ready() const noexcept {
        return SUCCEEDED(result_);
    }

private:
    HRESULT result_ = E_FAIL;
    bool owns_ = false;
};

[[nodiscard]] std::wstring utf8_to_wide(const std::string_view text) {
    if (text.empty()) return {};
    const int input_size = static_cast<int>(text.size());
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        input_size,
        nullptr,
        0
    );
    if (required <= 0) return {};
    std::wstring output(static_cast<std::size_t>(required), L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        input_size,
        output.data(),
        required
    );
    return converted == required ? output : std::wstring{};
}

[[nodiscard]] std::string normalized_extension(std::string extension) {
    while (!extension.empty() && extension.front() == '.') extension.erase(extension.begin());
    return extension;
}

struct NativeFilters {
    std::vector<std::wstring> labels;
    std::vector<std::wstring> patterns;
    std::vector<COMDLG_FILTERSPEC> specs;
};

[[nodiscard]] NativeFilters native_filters(const std::vector<FileDialogFilter>& filters) {
    NativeFilters result;
    result.labels.reserve(filters.size());
    result.patterns.reserve(filters.size());

    for (const auto& filter : filters) {
        std::wstring pattern;
        for (const auto& extension : filter.extensions) {
            const std::string normalized = normalized_extension(extension);
            if (normalized.empty()) continue;
            if (!pattern.empty()) pattern += L';';
            pattern += L"*.";
            pattern += utf8_to_wide(normalized);
        }
        if (pattern.empty()) continue;
        std::wstring label = utf8_to_wide(filter.label);
        if (label.empty()) label = L"Files";
        result.labels.push_back(std::move(label));
        result.patterns.push_back(std::move(pattern));
    }

    result.specs.reserve(result.labels.size());
    for (std::size_t index = 0; index < result.labels.size(); ++index) {
        result.specs.push_back(COMDLG_FILTERSPEC{
            result.labels[index].c_str(),
            result.patterns[index].c_str(),
        });
    }
    return result;
}

void set_title(IFileDialog& dialog, const std::string& title) {
    if (title.empty()) return;
    const auto wide = utf8_to_wide(title);
    if (!wide.empty()) (void)dialog.SetTitle(wide.c_str());
}

void set_initial_directory(IFileDialog& dialog, const std::filesystem::path& directory) {
    if (directory.empty()) return;
    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(
            directory.c_str(),
            nullptr,
            IID_PPV_ARGS(&item)
        )) && item != nullptr) {
        (void)dialog.SetFolder(item);
        item->Release();
    }
}

void set_filters(IFileDialog& dialog, NativeFilters& filters) {
    if (filters.specs.empty()) return;
    if (SUCCEEDED(dialog.SetFileTypes(
            static_cast<UINT>(filters.specs.size()),
            filters.specs.data()
        ))) {
        (void)dialog.SetFileTypeIndex(1U);
    }
}

[[nodiscard]] PathDialogResult selected_result(IFileDialog& dialog) {
    IShellItem* item = nullptr;
    if (FAILED(dialog.GetResult(&item)) || item == nullptr) {
        return PathDialogResult{
            .status = PathDialogStatus::failed,
            .diagnostics = "The native file dialog returned no filesystem item.",
        };
    }

    PWSTR raw_path = nullptr;
    const HRESULT path_result = item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path);
    item->Release();
    if (FAILED(path_result) || raw_path == nullptr) {
        if (raw_path != nullptr) CoTaskMemFree(raw_path);
        return PathDialogResult{
            .status = PathDialogStatus::failed,
            .diagnostics = "The native file dialog returned no filesystem path.",
        };
    }

    std::filesystem::path path{raw_path};
    CoTaskMemFree(raw_path);
    return PathDialogResult{
        .status = PathDialogStatus::selected,
        .path = path.lexically_normal(),
    };
}

[[nodiscard]] PathDialogResult show_dialog(IFileDialog& dialog) {
    const HRESULT shown = dialog.Show(nullptr);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return PathDialogResult{.status = PathDialogStatus::cancelled};
    }
    if (FAILED(shown)) {
        return PathDialogResult{
            .status = PathDialogStatus::failed,
            .diagnostics = "The Windows file dialog could not be displayed.",
        };
    }
    return selected_result(dialog);
}

[[nodiscard]] PathDialogResult com_failure() {
    return PathDialogResult{
        .status = PathDialogStatus::failed,
        .diagnostics = "Windows could not initialize the file-dialog COM apartment.",
    };
}

} // namespace

PathDialogResult open_file_dialog(const OpenFileDialogDesc& desc) {
    ComApartment apartment;
    if (!apartment.ready()) return com_failure();

    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog)
        )) || dialog == nullptr) {
        return PathDialogResult{
            .status = PathDialogStatus::failed,
            .diagnostics = "Windows could not create the Open File dialog.",
        };
    }

    FILEOPENDIALOGOPTIONS options{};
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        (void)dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    }
    set_title(*dialog, desc.title);
    set_initial_directory(*dialog, desc.initial_directory);
    auto filters = native_filters(desc.filters);
    set_filters(*dialog, filters);
    auto result = show_dialog(*dialog);
    dialog->Release();
    return result;
}

PathDialogResult save_file_dialog(const SaveFileDialogDesc& desc) {
    ComApartment apartment;
    if (!apartment.ready()) return com_failure();

    IFileSaveDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_FileSaveDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog)
        )) || dialog == nullptr) {
        return PathDialogResult{
            .status = PathDialogStatus::failed,
            .diagnostics = "Windows could not create the Save File dialog.",
        };
    }

    FILEOPENDIALOGOPTIONS options{};
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        (void)dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT);
    }
    set_title(*dialog, desc.title);
    set_initial_directory(*dialog, desc.initial_directory);
    if (!desc.suggested_name.empty()) {
        const auto name = utf8_to_wide(desc.suggested_name);
        if (!name.empty()) (void)dialog->SetFileName(name.c_str());
    }
    auto filters = native_filters(desc.filters);
    set_filters(*dialog, filters);
    if (!desc.filters.empty() && !desc.filters.front().extensions.empty()) {
        const auto extension = utf8_to_wide(normalized_extension(desc.filters.front().extensions.front()));
        if (!extension.empty()) (void)dialog->SetDefaultExtension(extension.c_str());
    }
    auto result = show_dialog(*dialog);
    dialog->Release();
    return result;
}

PathDialogResult select_folder_dialog(const FolderDialogDesc& desc) {
    ComApartment apartment;
    if (!apartment.ready()) return com_failure();

    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog)
        )) || dialog == nullptr) {
        return PathDialogResult{
            .status = PathDialogStatus::failed,
            .diagnostics = "Windows could not create the Select Folder dialog.",
        };
    }

    FILEOPENDIALOGOPTIONS options{};
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        (void)dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    set_title(*dialog, desc.title);
    set_initial_directory(*dialog, desc.initial_directory);
    auto result = show_dialog(*dialog);
    dialog->Release();
    return result;
}

} // namespace vice::platform
