#include <vice/platform/process.hpp>

#import <Foundation/Foundation.h>

#include <string>
#include <utility>

namespace vice::platform {
namespace {

[[nodiscard]] NSString* ns_string(const std::string& value) {
    return [NSString stringWithUTF8String:value.c_str()];
}

[[nodiscard]] NSString* ns_path(const std::filesystem::path& value) {
    return ns_string(value.string());
}

[[nodiscard]] ProcessLaunchResult fail(std::string message) {
    return ProcessLaunchResult{.launched = false, .diagnostics = std::move(message)};
}

} // namespace

ProcessLaunchResult launch_process(const ProcessLaunchRequest& request) {
    if (request.executable.empty()) {
        return fail("Process executable path is empty.");
    }

    @autoreleasepool {
        NSString* const executable = ns_path(request.executable);
        if (executable == nil) {
            return fail("Process executable path is not valid UTF-8.");
        }

        NSMutableArray<NSString*>* arguments = [NSMutableArray arrayWithCapacity:request.arguments.size()];
        for (const auto& argument : request.arguments) {
            NSString* const value = ns_string(argument);
            if (value == nil) {
                return fail("Process argument is not valid UTF-8.");
            }
            [arguments addObject:value];
        }

        NSTask* task = [[NSTask alloc] init];
        task.executableURL = [NSURL fileURLWithPath:executable];
        task.arguments = arguments;

        if (!request.working_directory.empty()) {
            NSString* const working_directory = ns_path(request.working_directory);
            if (working_directory == nil) {
                return fail("Process working directory is not valid UTF-8.");
            }
            task.currentDirectoryURL = [NSURL fileURLWithPath:working_directory isDirectory:YES];
        }

        NSError* error = nil;
        if (![task launchAndReturnError:&error]) {
            const char* const description = error.localizedDescription.UTF8String;
            return fail(
                description != nullptr
                    ? "Could not launch process: " + std::string{description}
                    : "Could not launch process."
            );
        }
    }

    return ProcessLaunchResult{.launched = true};
}

} // namespace vice::platform
