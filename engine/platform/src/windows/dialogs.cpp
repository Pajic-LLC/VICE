#include <vice/platform/services.hpp>

#include <windows.h>
#include <shobjidl.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace vice::platform {
namespace {

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
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            input_size,
            result.data(),
            required
        ) != required) {
        return {};
    }
    return result;
}

class ComApartment final {
public:
    ComApartment() noexcept {
        const HRESULT result = CoInitializeEx(
            nullptr,
            COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE
        );
        if (SUCCEEDED(result)) {
            initialized_ = true;
            usable_ = true;
        } else if (result == RPC_E_CHANGED_MODE) {
            usable_ = true;
        }
    }

    ~ComApartment() {
        if (initialized_) CoUninitialize();
    }

    [[nodiscard]] bool usable() const noexcept { return usable_; }

private:
    bool initialized_ = false;
    bool usable_ = false;
};

[[nodiscard]] PathDialogResult selected_path(IFileDialog& dialog) {
    IShellItem* item = nullptr;
    const HRESULT item_result = dialog.GetResult(&item);
    if (FAILED(item_result) || item == nullptr) {
        return {.diagnostics = "Windows file dialog returned no selected item."};
    }

    PWSTR native_path = nullptr;
    const HRESULT path_result = item->GetDisplayName(SIGDN_FILESYSPATH, &native_path);
    item->Release();
    if (FAILED(path_result) || native_path == nullptr) {
        return {.diagnostics = "Windows file dialog returned no filesystem path."};
    }

    std::filesystem::path path{native_path};
    CoTaskMemFree(native_path);
    return {.path = std::move(path)};
}

[[nodiscard]] PathDialogResult show_dialog(
    const std::string_view title,
    const bool choose_folder,
    const std::string_view extension
) {
    ComApartment apartment;
    if (!apartment.usable()) {
        return {.diagnostics = "Windows could not initialize COM for the native file dialog."};
    }

    IFileOpenDialog* dialog = nullptr;
    const HRESULT create_result = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog)
    );
    if (FAILED(create_result) || dialog == nullptr) {
        return {.diagnostics = "Windows could not create the native file dialog."};
    }

    FILEOPENDIALOGOPTIONS options{};
    if (FAILED(dialog->GetOptions(&options))) {
        dialog->Release();
        return {.diagnostics = "Windows could not configure the native file dialog."};
    }
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    options |= choose_folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
    if (FAILED(dialog->SetOptions(options))) {
        dialog->Release();
        return {.diagnostics = "Windows could not configure the native file dialog."};
    }

    const std::wstring wide_title = utf8_to_wide(title);
    if (!wide_title.empty()) {
        (void)dialog->SetTitle(wide_title.c_str());
    }

    std::wstring filter_name;
    std::wstring filter_pattern;
    if (!choose_folder && !extension.empty()) {
        const std::wstring wide_extension = utf8_to_wide(extension);
        if (!wide_extension.empty()) {
            filter_name = L"VICE Project (*." + wide_extension + L")";
            filter_pattern = L"*." + wide_extension;
            const COMDLG_FILTERSPEC filter{
                filter_name.c_str(),
                filter_pattern.c_str(),
            };
            (void)dialog->SetFileTypes(1U, &filter);
            (void)dialog->SetFileTypeIndex(1U);
        }
    }

    const HRESULT show_result = dialog->Show(nullptr);
    if (show_result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        dialog->Release();
        return {};
    }
    if (FAILED(show_result)) {
        dialog->Release();
        return {.diagnostics = "Windows could not complete the native file dialog."};
    }

    PathDialogResult result = selected_path(*dialog);
    dialog->Release();
    return result;
}

} // namespace

PathDialogResult PlatformServices::open_file_dialog(const OpenFileDialogDesc& desc) {
    return show_dialog(desc.title, false, desc.extension);
}

PathDialogResult PlatformServices::select_folder_dialog(const FolderDialogDesc& desc) {
    return show_dialog(desc.title, true, {});
}

} // namespace vice::platform
