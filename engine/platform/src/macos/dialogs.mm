#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <vice/platform/services.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace vice::platform {
namespace {

[[nodiscard]] NSString* ns_string(const std::string_view text) {
    NSString* value = [[NSString alloc]
        initWithBytes:text.data()
        length:text.size()
        encoding:NSUTF8StringEncoding];
    return value != nil ? value : @"";
}

[[nodiscard]] std::filesystem::path path_from_url(NSURL* url) {
    if (url == nil) return {};
    NSString* value = [url path];
    if (value == nil) return {};
    const char* utf8 = [value UTF8String];
    return utf8 != nullptr ? std::filesystem::path{std::string{utf8}} : std::filesystem::path{};
}

} // namespace

PathDialogResult PlatformServices::open_file_dialog(const OpenFileDialogDesc& desc) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        [panel setResolvesAliases:YES];
        if (!desc.title.empty()) {
            [panel setTitle:ns_string(desc.title)];
            [panel setMessage:ns_string(desc.title)];
        }
        if (!desc.extension.empty()) {
            UTType* type = [UTType typeWithFilenameExtension:ns_string(desc.extension)];
            if (type != nil) {
                [panel setAllowedContentTypes:@[type]];
            }
        }

        const NSModalResponse response = [panel runModal];
        if (response == NSModalResponseCancel) return {};
        if (response != NSModalResponseOK) {
            return {.diagnostics = "macOS could not complete the native file dialog."};
        }

        const auto path = path_from_url([panel URL]);
        if (path.empty()) {
            return {.diagnostics = "macOS file dialog returned no filesystem path."};
        }
        return {.path = path};
    }
}

PathDialogResult PlatformServices::select_folder_dialog(const FolderDialogDesc& desc) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:NO];
        [panel setCanChooseDirectories:YES];
        [panel setCanCreateDirectories:YES];
        [panel setAllowsMultipleSelection:NO];
        [panel setResolvesAliases:YES];
        if (!desc.title.empty()) {
            [panel setTitle:ns_string(desc.title)];
            [panel setMessage:ns_string(desc.title)];
        }

        const NSModalResponse response = [panel runModal];
        if (response == NSModalResponseCancel) return {};
        if (response != NSModalResponseOK) {
            return {.diagnostics = "macOS could not complete the native folder dialog."};
        }

        const auto path = path_from_url([panel URL]);
        if (path.empty()) {
            return {.diagnostics = "macOS folder dialog returned no filesystem path."};
        }
        return {.path = path};
    }
}

} // namespace vice::platform
