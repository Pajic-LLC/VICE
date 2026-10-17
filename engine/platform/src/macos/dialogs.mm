#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <vice/platform/dialogs.hpp>

#include <string>
#include <string_view>

namespace vice::platform {
namespace {

[[nodiscard]] NSString* make_ns_string(const std::string_view text) {
    if (text.empty()) return @"";
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

[[nodiscard]] NSURL* directory_url(const std::filesystem::path& path) {
    if (path.empty()) return nil;
    const std::string utf8 = path.string();
    NSString* value = make_ns_string(utf8);
    return value.length > 0U ? [NSURL fileURLWithPath:value isDirectory:YES] : nil;
}

[[nodiscard]] NSArray<UTType*>* allowed_types(const std::vector<FileDialogFilter>& filters) {
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    for (const auto& filter : filters) {
        for (const auto& extension : filter.extensions) {
            std::string normalized = extension;
            while (!normalized.empty() && normalized.front() == '.') {
                normalized.erase(normalized.begin());
            }
            if (normalized.empty()) continue;
            UTType* type = [UTType typeWithFilenameExtension:make_ns_string(normalized)];
            if (type != nil && ![types containsObject:type]) {
                [types addObject:type];
            }
        }
    }
    return types;
}

void configure_panel(NSSavePanel* panel, const std::string& title, const std::filesystem::path& directory) {
    if (!title.empty()) [panel setTitle:make_ns_string(title)];
    if (NSURL* url = directory_url(directory); url != nil) {
        [panel setDirectoryURL:url];
    }
}

[[nodiscard]] PathDialogResult result_for_response(NSSavePanel* panel, const NSModalResponse response) {
    if (response == NSModalResponseCancel) {
        return PathDialogResult{.status = PathDialogStatus::cancelled};
    }
    if (response != NSModalResponseOK) {
        return PathDialogResult{
            .status = PathDialogStatus::failed,
            .diagnostics = "The native file dialog did not complete successfully.",
        };
    }
    const auto path = path_from_url([panel URL]);
    if (path.empty()) {
        return PathDialogResult{
            .status = PathDialogStatus::failed,
            .diagnostics = "The native file dialog returned no filesystem path.",
        };
    }
    return PathDialogResult{
        .status = PathDialogStatus::selected,
        .path = path.lexically_normal(),
    };
}

} // namespace

PathDialogResult open_file_dialog(const OpenFileDialogDesc& desc) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        [panel setResolvesAliases:YES];
        configure_panel(panel, desc.title, desc.initial_directory);
        NSArray<UTType*>* types = allowed_types(desc.filters);
        if (types.count > 0U) [panel setAllowedContentTypes:types];
        return result_for_response(panel, [panel runModal]);
    }
}

PathDialogResult save_file_dialog(const SaveFileDialogDesc& desc) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setCanCreateDirectories:YES];
        configure_panel(panel, desc.title, desc.initial_directory);
        if (!desc.suggested_name.empty()) {
            [panel setNameFieldStringValue:make_ns_string(desc.suggested_name)];
        }
        NSArray<UTType*>* types = allowed_types(desc.filters);
        if (types.count > 0U) [panel setAllowedContentTypes:types];
        return result_for_response(panel, [panel runModal]);
    }
}

PathDialogResult select_folder_dialog(const FolderDialogDesc& desc) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:NO];
        [panel setCanChooseDirectories:YES];
        [panel setAllowsMultipleSelection:NO];
        [panel setCanCreateDirectories:YES];
        [panel setResolvesAliases:YES];
        configure_panel(panel, desc.title, desc.initial_directory);
        return result_for_response(panel, [panel runModal]);
    }
}

} // namespace vice::platform
